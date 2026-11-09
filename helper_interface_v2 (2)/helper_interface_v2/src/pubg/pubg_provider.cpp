// pubg_provider.cpp — provider thật, cắm vào đúng ISnapshotProvider của bạn.
//
// Đặc tính (quan trọng cho tầng chống phát hiện của bạn):
//   - chỉ đọc bằng mach_vm_read_overwrite từ một binary riêng, KHÔNG inject,
//     KHÔNG tạo thread trong process game, KHÔNG image lạ trong game.
//   - mọi offset nạp từ offsets.json (dump ra), không hardcode.
//   - fail-early: world chain sai → dừng ngay, không đọc tiếp suốt trận.
//   - đọc chia frame (mặc định 4 lượt/vòng, 30Hz) để giảm mật độ truy vấn.
#include "pubg_provider.hpp"
#include "offsets.hpp"
#include "mach_reader.hpp"

#include "iface/util.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#if defined(__APPLE__)
#include <libproc.h>
#include <sys/proc_info.h>
#include <unistd.h>
#endif

namespace hs {
namespace {

// ---------------------------------------------------------------------------
// Ctx: toàn bộ trạng thái đọc. read_mem nằm sau IMemReader nên đổi backend
// (task port -> kernel read) không phải sửa logic dưới đây.
// ---------------------------------------------------------------------------
struct Ctx {
    std::unique_ptr<IMemReader> rd;
    Offsets  off;
    uint64_t base = 0;

    // cache trong 1 vòng
    uint64_t actorsData = 0;
    int32_t  actorsNum  = 0;
    uint64_t world = 0, level = 0;

    std::unordered_set<uint64_t> classSet;  // UClass* của PlayerCharacter / ...
    bool     classesReady = false;
};

struct FTransform {                 // 0x30
    float rot[4];                   // +0x00 FQuat
    float tx, ty, tz, _pad0;        // +0x10 FVector Translation
    float sx, sy, sz, _pad1;        // +0x20 FVector Scale3D
};

// ---------------------------------------------------------------------------
// FName → chuỗi. Hỗ trợ cả TNameEntryArray (UE4.18, stride 8, có header)
// và FNamePool (UE4.23+, stride 2, header uint16).
// ---------------------------------------------------------------------------
bool name_of(const Ctx& c, int32_t idx, char* out, size_t cap) {
    if (!out || cap < 2 || idx < 0) return false;
    out[0] = 0;

    const uint32_t shift  = c.off.name_block_shift;
    const uint32_t perBlk = c.off.name_block_entries;
    if (!perBlk) return false;

    uint64_t gbase = c.base + c.off.gnames;
    if (c.off.gnames_deref) {
        if (!c.rd->rd(gbase, gbase) || !gbase) return false;
    }
    uint64_t blockPtr = 0;
    if (!c.rd->rd(gbase + (uint64_t)((uint32_t)idx >> shift) * 8, blockPtr) || !blockPtr)
        return false;

    const uint32_t inBlk = (uint32_t)idx & (perBlk - 1);

    if (c.off.name_entry_stride == 8) {
        // TNameEntryArray: block là mảng con trỏ tới FNameEntry
        uint64_t entry = 0;
        if (!c.rd->rd(blockPtr + (uint64_t)inBlk * 8, entry) || !entry) return false;
        if (!c.rd->rd_str(entry + c.off.name_entry_hdr, out, cap)) return false;
        return out[0] != 0;
    }

    // FNamePool: block là vùng thô, entry mở đầu bằng uint16 header
    const uint64_t entry = blockPtr + (uint64_t)inBlk * c.off.name_entry_stride;
    uint16_t hdr = 0;
    if (!c.rd->rd(entry, hdr)) return false;
    const bool     wide = (hdr & 1) != 0;
    const uint32_t len  = (uint32_t)hdr >> 6;
    if (wide || len == 0 || len >= cap) return false;
    if (!c.rd->read(entry + 2, out, len)) return false;
    out[len] = 0;
    for (uint32_t i = 0; i < len; ++i)
        if ((unsigned char)out[i] < 0x20) return false;
    return true;
}

bool obj_name(const Ctx& c, uint64_t obj, char* out, size_t cap) {
    int32_t nidx = 0;
    if (!c.rd->rd(obj + c.off.uobj_name_index, nidx)) return false;
    return name_of(c, nidx, out, cap);
}

bool obj_class(const Ctx& c, uint64_t obj, uint64_t& cls) {
    return c.rd->rd(obj + c.off.uobj_class, cls) && cls != 0;
}

// Duyệt TUObjectArray
bool object_at(const Ctx& c, int32_t i, uint64_t& obj) {
    uint64_t gaddr = c.base + c.off.gobjects;
    if (c.off.gobjects_deref) {
        if (!c.rd->rd(gaddr, gaddr) || !gaddr) return false;
    }
    uint64_t chunks = 0;
    if (!c.rd->rd(gaddr, chunks) || !chunks) return false;
    const int32_t per = 1 << c.off.obj_chunk_shift;
    uint64_t blk = 0;
    if (!c.rd->rd(chunks + (uint64_t)(i >> c.off.obj_chunk_shift) * 8, blk) || !blk) return false;
    if (!c.rd->rd(blk + (uint64_t)((uint32_t)i & (uint32_t)(per - 1)) * c.off.obj_item_stride +
                     c.off.obj_item_object, obj))
        return false;
    return obj != 0;
}

// Tìm UClass* cho các class quan tâm: quét GObjects, object phải có tên khớp
// và class của chính nó phải tên "Class".
bool build_class_set(Ctx& c) {
    int32_t num = 0;
    uint64_t gaddr = c.base + c.off.gobjects;
    if (c.off.gobjects_deref && (!c.rd->rd(gaddr, gaddr) || !gaddr)) return false;
    if (!c.rd->rd(gaddr + c.off.obj_num_offset, num)) return false;
    if (num <= 0 || num > 4'000'000) {
        std::fprintf(stderr, "[pubg] GObjects::NumElements = %d (vô lý) → sai offset gobjects\n", num);
        return false;
    }

    static const char* kWanted[] = {"PlayerCharacter", "PlayerState", "PlayerController"};

    c.classSet.clear();
    int matched = 0;
    for (int32_t i = 0; i < num && matched < 3; ++i) {
        uint64_t obj = 0;
        if (!object_at(c, i, obj)) continue;
        char nm[128];
        if (!obj_name(c, obj, nm, sizeof nm)) continue;

        bool want = false;
        for (auto w : kWanted) if (!std::strcmp(nm, w)) { want = true; break; }
        if (!want) continue;

        uint64_t cls = 0;
        if (!obj_class(c, obj, cls)) continue;
        char cn[64];
        if (!obj_name(c, cls, cn, sizeof cn) || std::strcmp(cn, "Class")) continue;

        c.classSet.insert(obj);   // obj chính là UClass*
        ++matched;
    }

    if (c.classSet.empty()) {
        std::fprintf(stderr, "[pubg] không tìm được UClass nào trong GObjects → sai layout\n");
        return false;
    }
    c.classesReady = true;
    return true;
}

inline bool is_wanted(const Ctx& c, uint64_t cls) { return c.classSet.count(cls) != 0; }

// FString → UTF-8 (thử TCHAR 4 byte rồi 2 byte)
bool read_fstring(const Ctx& c, uint64_t addr, char* out, size_t cap) {
    if (!out || cap < 2) return false;
    out[0] = 0;
    uint64_t data = 0;
    int32_t  num  = 0;
    if (!c.rd->rd(addr, data) || !c.rd->rd(addr + 8, num)) return false;
    if (!data || num <= 1 || num > 128) return false;

    std::vector<uint8_t> raw((size_t)num * 4, 0);
    if (!c.rd->read(data, raw.data(), raw.size())) return false;

    auto plausible = [](const std::vector<uint8_t>& v, int w) {
        int printable = 0, total = 0;
        for (size_t i = 0; i + w <= v.size(); i += w) {
            uint32_t cp = 0;
            for (int k = 0; k < w; ++k) cp |= (uint32_t)v[i + k] << (8 * k);
            if (cp == 0) break;
            ++total;
            if ((cp >= 0x20 && cp < 0x7F) || (cp >= 0x100 && cp < 0x3000) || cp > 0x4E00) ++printable;
        }
        return total > 0 && printable * 10 >= total * 8;
    };

    const int w = plausible(raw, 4) ? 4 : (plausible(raw, 2) ? 2 : 0);
    if (!w) return false;

    size_t o = 0;
    for (size_t i = 0; i + (size_t)w <= raw.size() && o + 4 < cap; i += w) {
        uint32_t cp = 0;
        for (int k = 0; k < w; ++k) cp |= (uint32_t)raw[i + k] << (8 * k);
        if (!cp) break;
        if (cp < 0x80) out[o++] = (char)cp;
        else if (cp < 0x800) {
            out[o++] = (char)(0xC0 | (cp >> 6));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out[o++] = (char)(0xE0 | (cp >> 12));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
            out[o++] = (char)(0xF0 | (cp >> 18));
            out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[o] = 0;
    return o > 0;
}

bool attach_trace_selftest(pid_t pid) {
#if defined(__APPLE__)
    struct proc_bsdinfo bsd{};
    int rc = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bsd, sizeof bsd);
    if (rc <= 0) {
        std::fprintf(stderr, "[self] proc_pidinfo fail (rc=%d) — pid %d?\n", rc, pid);
        return false;
    }
    const bool traced = (bsd.pbi_flags & 0x800) != 0;   // P_TRACED
    std::fprintf(stderr, "[self] pid=%d status=%d flags=0x%x traced=%s\n",
                 pid, bsd.pbi_status, bsd.pbi_flags, traced ? "YES" : "no");
    return !traced;
#else
    (void)pid;
    return false;
#endif
}

// ---------------------------------------------------------------------------
class PubgProvider final : public ISnapshotProvider {
public:
    explicit PubgProvider(pid_t pid, std::string offsetsPath)
        : pid_(pid), offsetsPath_(std::move(offsetsPath)) {}

    ~PubgProvider() override { disconnect(); }

    bool connect() override {
        if (running_.load()) return true;

#if !defined(__APPLE__)
        std::fprintf(stderr, "[pubg] backend không tồn tại trên host build\n");
        return false;
#else
        if (pid_ <= 0) { std::fprintf(stderr, "[pubg] cần --pid\n"); return false; }

        if (!attach_trace_selftest(pid_)) {
            std::fprintf(stderr,
                "[pubg] P_TRACED đang bật → bỏ đường task port, phải chuyển KernelReader (kfd/physrw)\n");
            // Ở đây cắm: ctx_->rd = make_unique<KernelReader>(ud, phys_read);
            return false;
        }

        if (!ctx_->off.load(offsetsPath_)) {
            std::fprintf(stderr, "[pubg] không mở được offsets: %s\n", offsetsPath_.c_str());
            return false;
        }
        if (!ctx_->off.valid()) {
            std::fprintf(stderr, "[pubg] offsets thiếu field bắt buộc → chạy dump_offsets trước\n");
            return false;
        }

        auto* mach = new MachTaskReader();
        ctx_->rd.reset(mach);
        if (!mach->attach(pid_)) return false;

        // self-check chứ không tin mù: header 16 byte đầu phải là MH_MAGIC_64 + cputype arm64
        if (!mach->resolveExecBase(ctx_->off.image)) {
            std::fprintf(stderr, "[pubg] không tìm thấy exec base (image='%s')\n", ctx_->off.image.c_str());
            return false;
        }
        ctx_->base = mach->imageBase();

        uint8_t hdr[16] = {0};
        if (!mach->read(ctx_->base, hdr, sizeof hdr)) return false;
        if (!(hdr[0] == 0xCF && hdr[1] == 0xFA)) {
            std::fprintf(stderr, "[pubg] base 0x%llx không phải MH_MAGIC_64 → sai image\n",
                         (unsigned long long)ctx_->base);
            return false;
        }

        std::fprintf(stderr, "[pubg] base=0x%llx image=%s ver=%s\n",
                     (unsigned long long)ctx_->base, ctx_->off.image.c_str(),
                     ctx_->off.version.c_str());

        if (!build_class_set(*ctx_)) return false;
        if (!read_world_chain(*ctx_)) {
            std::fprintf(stderr, "[pubg] world chain fail ngay lần đầu → dừng (sai offset)\n");
            return false;
        }

        running_.store(true);
        worker_ = std::thread([this] { loop(); });
        return true;
#endif
    }

    bool readSnapshot(Snapshot& out) override {
        std::lock_guard<std::mutex> lk(mu_);
        if (!have_) return false;
        out = latest_;
        return true;
    }

    bool alive() const override { return running_.load(); }

    void disconnect() override {
        running_.store(false);
        if (worker_.joinable()) worker_.join();
        if (ctx_->rd) ctx_->rd.reset();
        std::lock_guard<std::mutex> lk(mu_);
        have_ = false;
    }

    const char* name() const override { return "pubg"; }
    uint32_t rosterSize() const override { return roster_; }

private:
    // ---- đọc world chain, gọi 1 lần / vòng ----
    bool read_world_chain(Ctx& c) {
        uint64_t world = 0, level = 0, arr = 0;
        if (!c.rd->rd(c.base + c.off.gworld, world) || !world) return false;
        if (!c.rd->rd(world + c.off.persist_level, level) || !level) return false;
        if (!c.rd->rd(level + c.off.level_actors, arr) || !arr) return false;

        // log 1 lần: nếu đọc được tên level mà không phải "PersistentLevel" → sai offset
        static bool logged = false;
        if (!logged) {
            char nm[128];
            if (obj_name(c, level, nm, sizeof nm))
                std::fprintf(stderr, "[pubg] PersistentLevel.Name = '%s'\n", nm);
            logged = true;
        }

        uint64_t data = 0;
        int32_t  num  = 0;
        if (!c.rd->rd(arr, data) || !c.rd->rd(arr + 8, num)) return false;
        if (!data || num <= 0 || num > 50000) return false;

        c.world      = world;
        c.level      = level;
        c.actorsData = data;
        c.actorsNum  = num;
        return true;
    }

    bool read_transform(Ctx& c, uint64_t actor, Vec3& pos) {
        uint64_t root = 0;
        if (!c.rd->rd(actor + c.off.actor_root, root) || !root) return false;
        FTransform t{};
        if (!c.rd->rd(root + c.off.comp_xform, t)) return false;
        pos = {t.tx, t.ty, t.tz};
        return true;
    }

    void read_player(Ctx& c, uint64_t actor, Player& p) {
        p.actor = actor;
        if (!read_transform(c, actor, p.root)) return;
        p.x = p.root.x;
        p.y = p.root.y;
        p.z = p.root.z;

        // PlayerState → tên / team / hp
        uint64_t ps = 0;
        if (c.off.pawn_player_state && c.rd->rd(actor + c.off.pawn_player_state, ps) && ps) {
            if (c.off.ps_name) {
                char nb[96];
                if (read_fstring(c, ps + c.off.ps_name, nb, sizeof nb)) p.name = nb;
            }
            int32_t team = -1;
            if (c.off.ps_team && c.rd->rd(ps + c.off.ps_team, team)) p.team = team;
            float hp = 0.f;
            if (c.off.ps_health && c.rd->rd(ps + c.off.ps_health, hp)) p.health = hp;
        }
        if (p.name.empty()) {
            char nm[96];
            if (obj_name(c, actor, nm, sizeof nm)) p.name = nm;
            else p.name = "?";
        }

        // Bone head
        if (c.off.pawn_mesh && c.off.mesh_bones) {
            uint64_t mesh = 0;
            if (c.rd->rd(actor + c.off.pawn_mesh, mesh) && mesh) {
                uint64_t boneData = 0;
                int32_t  boneNum  = 0;
                if (c.rd->rd(mesh + c.off.mesh_bones, boneData) &&
                    c.rd->rd(mesh + c.off.mesh_bones + 8, boneNum) &&
                    boneData && boneNum > 0 && boneNum < 512) {
                    p.bones = (uint8_t)std::min<int32_t>(boneNum, 255);
                    if (c.off.bone_head >= 0 && c.off.bone_head < boneNum) {
                        FTransform t{};
                        if (c.rd->rd(boneData + (uint64_t)c.off.bone_head * sizeof(FTransform), t))
                            p.head = {t.tx, t.ty, t.tz};
                    }
                }
            }
        }
        if (p.head.x == 0.f && p.head.y == 0.f && p.head.z == 0.f) {
            p.head = {p.root.x, p.root.y, p.root.z + 180.f};   // fallback thô, render tự bù
        }
    }

    // Một vòng: chia actors thành kSlots lượt, mỗi lượt đọc 1/kSlots
    static constexpr int kSlots = 4;

    void loop() {
        const auto period = std::chrono::milliseconds(33);   // ~30Hz
        std::vector<Player> staging;
        staging.reserve(96);

        int  slot = 0;
        auto lastChain = now_ms();

        while (running_.load()) {
            Ctx& c = *ctx_;

            if (now_ms() - lastChain > 500) {          // refresh chain 2Hz
                if (!read_world_chain(c)) {
                    std::fprintf(stderr, "[pubg] mất world chain → dừng\n");
                    running_.store(false);
                    break;
                }
                lastChain = now_ms();
            }

            if (slot == 0) staging.clear();

            const int total = c.actorsNum;
            const int part  = (total + kSlots - 1) / kSlots;
            const int from  = slot * part;
            const int to    = std::min(total, from + part);

            if (slot == 0) { haveSelf_ = read_self(c, self_); }
            const Vec3 self = self_;
            const bool haveSelf = haveSelf_;

            for (int i = from; i < to; ++i) {
                uint64_t actor = 0;
                if (!c.rd->rd(c.actorsData + (uint64_t)i * 8, actor) || !actor) continue;
                uint64_t cls = 0;
                if (!obj_class(c, actor, cls) || !is_wanted(c, cls)) continue;

                Player p;
                p.id = actor;                        // id ổn định theo phiên
                read_player(c, actor, p);
                if (haveSelf) {
                    const float dx = p.root.x - self.x, dy = p.root.y - self.y, dz = p.root.z - self.z;
                    p.dist = std::sqrt(dx * dx + dy * dy + dz * dz) / 100.f;  // cm -> m
                }
                staging.push_back(std::move(p));
            }

            if (++slot >= kSlots) {
                slot = 0;
                publish(staging, self, haveSelf);
            }
            std::this_thread::sleep_for(period);
        }
    }

    bool read_self(Ctx& c, Vec3& self) {
        if (!c.off.ps_name) return false;
        // Camera POV của local PC = vị trí mắt; PC remote không có PlayerCameraManager
        for (int i = 0; i < c.actorsNum; ++i) {
            uint64_t actor = 0;
            if (!c.rd->rd(c.actorsData + (uint64_t)i * 8, actor) || !actor) continue;
            uint64_t cls = 0;
            if (!obj_class(c, actor, cls) || !is_wanted(c, cls)) continue;
            char nm[64];
            if (!obj_name(c, cls, nm, sizeof nm) || std::strcmp(nm, "PlayerController")) continue;

            uint64_t cam = 0;
            if (!c.off.pc_camera_mgr || !c.rd->rd(actor + c.off.pc_camera_mgr, cam) || !cam) continue;
            if (c.rd->rd(cam + c.off.cam_pov_loc, self)) return true;
        }
        return false;
    }

    void publish(std::vector<Player>& players, const Vec3& self, bool haveSelf) {
        // chữ ký roster → generation đổi khi danh sách người chơi đổi
        std::vector<uint64_t> ids;
        ids.reserve(players.size());
        for (const auto& p : players) ids.push_back(p.id);
        std::sort(ids.begin(), ids.end());
        uint64_t h = 1469598103934665603ULL;
        for (uint64_t v : ids) { h ^= v; h *= 1099511628211ULL; }
        if (h != rosterSig_) { rosterSig_ = h; ++generation_; }

        Snapshot s;
        s.generation = generation_;
        s.players    = std::move(players);
        s.self       = self;
        s.stamp_ms   = now_ms();
        if (!haveSelf) s.self = {};

        std::lock_guard<std::mutex> lk(mu_);
        latest_ = std::move(s);
        have_   = true;
        roster_ = (uint32_t)latest_.players.size();
    }

    pid_t                  pid_ = 0;
    std::string            offsetsPath_ = "offsets.json";
    std::unique_ptr<Ctx>   ctx_    = std::make_unique<Ctx>();
    std::atomic<bool>      running_{false};
    std::thread            worker_;
    std::mutex             mu_;
    Snapshot               latest_;
    bool                   have_ = false;
    Vec3                   self_{};
    bool                   haveSelf_ = false;
    uint64_t               rosterSig_ = 0;
    uint64_t               generation_ = 0;
    std::atomic<uint32_t>  roster_{0};
};

} // namespace

std::unique_ptr<ISnapshotProvider> make_pubg_provider(int argc, char** argv) {
#if !defined(__APPLE__)
    (void)argc; (void)argv;
    return nullptr;
#else
    pid_t       pid = 0;
    std::string path = "offsets.json";
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--pid" && i + 1 < argc) pid = (pid_t)strtol(argv[++i], nullptr, 10);
        else if (a == "--offsets" && i + 1 < argc) path = argv[++i];
    }
    if (pid <= 0) {
        std::fprintf(stderr, "[pubg] thiếu --pid <pid của game>\n");
        return nullptr;
    }
    return std::make_unique<PubgProvider>(pid, path);
#endif
}

} // namespace hs
