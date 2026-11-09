// dump_offsets.cpp — tìm offset bằng cách QUÉT + KIỂM CHỨNG, không đoán.
// Chạy trên máy (Mac hoặc chính iPhone qua SSH). Kết quả ghi ra offsets.json.
//
//   ./dump_offsets --pid <pid> [--out offsets.json] [--image <tên exec>]
//
// Nguyên tắc: mỗi bước chỉ nhận kết quả khi nó vượt qua kiểm chứng ngữ nghĩa
// (đọc ra chuỗi FName đúng, name của ULevel phải là "PersistentLevel", ...).
// Không đạt thì IN RA LÝ DO và dừng, tuyệt đối không nhét số rác vào json.
#include "../src/pubg/mach_reader.hpp"
#include "../src/pubg/offsets.hpp"
#include "../iface/util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#if defined(__APPLE__)

#include <libproc.h>
#include <mach-o/loader.h>
#include <unistd.h>

using namespace hs;

namespace {

struct Seg {
    uint64_t start = 0, end = 0;
    std::string name;
};

// --- đọc segment của main image từ load commands ---
std::vector<Seg> image_segments(IMemReader& rd, uint64_t base) {
    std::vector<Seg> segs;
    mach_header_64 hdr{};
    if (!rd.rd(base, hdr)) return segs;
    if (hdr.magic != MH_MAGIC_64) return segs;

    std::vector<uint8_t> lcs(hdr.sizeofcmds);
    if (!rd.read(base + sizeof(mach_header_64), lcs.data(), lcs.size())) return segs;

    size_t off = 0;
    for (uint32_t i = 0; i < hdr.ncmds && off + 8 <= lcs.size(); ++i) {
        auto* lc = (load_command*)(lcs.data() + off);
        if (lc->cmd == LC_SEGMENT_64 && off + sizeof(segment_command_64) <= lcs.size()) {
            auto* sc = (segment_command_64*)lc;
            Seg s;
            s.name  = std::string(sc->segname);
            s.start = base + sc->vmaddr;
            s.end   = s.start + sc->vmsize;
            if (sc->vmsize && sc->initprot != 0) segs.push_back(s);
        }
        if (lc->cmdsize == 0) break;
        off += lc->cmdsize;
    }
    return segs;
}

// --- đọc FName với một bộ tham số layout cho trước ---
bool name_try(IMemReader& rd, uint64_t gnames, uint32_t shift, uint32_t stride,
              uint32_t hdr, int32_t idx, char* out, size_t cap) {
    out[0] = 0;
    uint64_t blkPtr = 0;
    if (!rd.rd(gnames + (uint64_t)((uint32_t)idx >> shift) * 8, blkPtr) || !blkPtr) return false;
    const uint32_t per = 1u << shift;
    const uint32_t in  = (uint32_t)idx & (per - 1);
    if (stride == 8) {
        uint64_t entry = 0;
        if (!rd.rd(blkPtr + (uint64_t)in * 8, entry) || !entry) return false;
        return rd.rd_str(entry + hdr, out, cap) && out[0];
    }
    const uint64_t entry = blkPtr + (uint64_t)in * stride;
    uint16_t h = 0;
    if (!rd.rd(entry, h)) return false;
    if (h & 1) return false;
    const uint32_t len = h >> 6;
    if (!len || len >= cap) return false;
    if (!rd.read(entry + 2, out, len)) return false;
    out[len] = 0;
    for (uint32_t i = 0; i < len; ++i)
        if ((unsigned char)out[i] < 0x20) return false;
    return true;
}

int score_names(IMemReader& rd, uint64_t gnames, uint32_t shift, uint32_t stride, uint32_t hdr,
                std::string* sample, int limit = 64) {
    int ok = 0;
    char buf[128];
    for (int32_t i = 0; i < limit; ++i) {
        if (name_try(rd, gnames, shift, stride, hdr, i, buf, sizeof buf)) {
            ++ok;
            if (sample && sample->size() < 200) { *sample += buf; *sample += '|'; }
        }
    }
    return ok;
}

struct NameLayout { uint32_t shift, stride, hdr; };

// --- 1. GNames ---
bool find_gnames(IMemReader& rd, const std::vector<Seg>& segs, uint64_t& outOff,
                 NameLayout& outLayout, uint32_t& outDeref, uint64_t base,
                 uint64_t maxScanBytes = 96ull << 20) {
    static const NameLayout layouts[] = {
        {14, 8, 8}, {14, 8, 4},          // TNameEntryArray (UE4.18)
        {16, 2, 0}, {15, 2, 0},          // FNamePool (UE4.23+)
    };
    int best = 0;
    uint64_t scanned = 0;

    for (const auto& s : segs) {
        if (s.name.rfind("__DATA", 0) != 0) continue;
        for (uint64_t a = s.start; a + 8 <= s.end; a += 8) {
            if (++scanned > maxScanBytes / 8) {
                std::printf("[gnames] chạm giới hạn quét, dừng ở 0x%llx\n", (unsigned long long)a);
                return best >= 40;
            }
            uint64_t v = 0;
            if (!rd.rd(a, v) || v < 0x100000000ULL) continue;

            for (uint32_t deref = 0; deref <= 1; ++deref) {
                uint64_t gnames = v;
                if (deref) {
                    if (!rd.rd(gnames, gnames) || gnames < 0x100000000ULL) continue;
                }
                for (const auto& L : layouts) {
                    // prefilter rẻ: 8 entry đầu
                    if (score_names(rd, gnames, L.shift, L.stride, L.hdr, nullptr, 8) < 6) continue;
                    std::string sample;
                    int sc = score_names(rd, gnames, L.shift, L.stride, L.hdr, &sample, 64);
                    if (sc > best && sc >= 40) {
                        best      = sc;
                        outOff    = a - base;
                        outLayout = L;
                        outDeref  = deref;
                        std::printf("[gnames] off=0x%llx deref=%u layout{shift=%u,stride=%u,hdr=%u} "
                                    "score=%d/64\n         sample: %.100s\n",
                                    (unsigned long long)outOff, deref, L.shift, L.stride, L.hdr, sc,
                                    sample.c_str());
                    }
                }
            }
            if (best >= 56) return true;   // đủ tốt, không cần quét tiếp
        }
    }
    return best >= 40;
}

// --- 2. GObjects + layout item ---
struct ObjLayout {
    uint32_t chunk_shift = 13;
    uint32_t item_stride = 0x10;
    uint32_t item_object = 0;
    uint32_t num_offset  = 0xC;
    uint32_t class_off   = 0x10;
    uint32_t name_off    = 0x18;
};

bool obj_at(IMemReader& rd, uint64_t gobjectsAddr, const ObjLayout& L, int32_t i, uint64_t& obj) {
    uint64_t chunks = 0;
    if (!rd.rd(gobjectsAddr, chunks) || !chunks) return false;
    const uint32_t per = 1u << L.chunk_shift;
    uint64_t blk = 0;
    if (!rd.rd(chunks + (uint64_t)((uint32_t)i >> L.chunk_shift) * 8, blk) || !blk) return false;
    return rd.rd(blk + (uint64_t)((uint32_t)i & (per - 1)) * L.item_stride + L.item_object, obj) && obj;
}

int score_objects(IMemReader& rd, uint64_t gobjectsAddr, uint64_t gnames, const NameLayout& nl,
                  const ObjLayout& L) {
    int ok = 0;
    char buf[128];
    for (int32_t i = 0; i < 64; ++i) {
        uint64_t obj = 0;
        if (!obj_at(rd, gobjectsAddr, L, i, obj)) continue;
        int32_t nidx = 0;
        if (!rd.rd(obj + L.name_off, nidx)) continue;
        if (!name_try(rd, gnames, nl.shift, nl.stride, nl.hdr, nidx, buf, sizeof buf)) continue;
        uint64_t cls = 0;
        if (!rd.rd(obj + L.class_off, cls) || !cls) continue;
        int32_t cidx = 0;
        char cn[128];
        if (!rd.rd(cls + L.name_off, cidx)) continue;
        if (!name_try(rd, gnames, nl.shift, nl.stride, nl.hdr, cidx, cn, sizeof cn)) continue;
        ++ok;
    }
    return ok;
}

bool find_gobjects(IMemReader& rd, const std::vector<Seg>& segs, uint64_t gnames,
                   const NameLayout& nl, uint64_t& outOff, ObjLayout& out, uint32_t& outDeref,
                   uint64_t base, uint64_t maxScanBytes = 96ull << 20) {
    int best = 0;
    uint64_t scanned = 0;
    for (const auto& s : segs) {
        if (s.name.rfind("__DATA", 0) != 0) continue;
        for (uint64_t a = s.start; a + 0x20 <= s.end; a += 8) {
            if (++scanned > maxScanBytes / 8) return best >= 50;
            uint64_t v = 0;
            if (!rd.rd(a, v) || v < 0x100000000ULL) continue;

            for (uint32_t deref = 0; deref <= 1; ++deref) {
                uint64_t gaddr = a;
                if (deref && (!rd.rd(gaddr, gaddr) || gaddr < 0x100000000ULL)) continue;

                for (uint32_t numOff : {0xCu, 0x10u, 0x14u}) {
                    int32_t num = 0;
                    if (!rd.rd(gaddr + numOff, num)) continue;
                    if (num < 1000 || num > 20000000) continue;

                    for (uint32_t cshift : {13u, 14u}) {
                        for (uint32_t stride : {0x10u, 0x18u, 0x20u}) {
                            for (uint32_t iobj : {0u, 8u}) {
                                for (auto cn : {std::pair<uint32_t, uint32_t>{0x10, 0x18},
                                                std::pair<uint32_t, uint32_t>{0x18, 0x20},
                                                std::pair<uint32_t, uint32_t>{0x20, 0x28}}) {
                                    ObjLayout L;
                                    L.chunk_shift = cshift;
                                    L.item_stride = stride;
                                    L.item_object = iobj;
                                    L.num_offset  = numOff;
                                    L.class_off   = cn.first;
                                    L.name_off    = cn.second;
                                    int sc = score_objects(rd, gaddr, gnames, nl, L);
                                    if (sc > best && sc >= 50) {
                                        best     = sc;
                                        outOff   = a - base;
                                        outDeref = deref;
                                        out      = L;
                                        std::printf("[gobjects] off=0x%llx deref=%u num_off=0x%x num=%d "
                                                    "chunk_shift=%u stride=0x%x obj=0x%x class=0x%x "
                                                    "name=0x%x score=%d/64\n",
                                                    (unsigned long long)outOff, deref, numOff, num,
                                                    cshift, stride, iobj, cn.first, cn.second, sc);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (best >= 62) return true;
        }
    }
    return best >= 50;
}

bool obj_name_of(IMemReader& rd, uint64_t gnames, const NameLayout& nl, uint64_t obj,
                 uint32_t name_off, char* out, size_t cap) {
    int32_t idx = 0;
    if (!rd.rd(obj + name_off, idx)) return false;
    return name_try(rd, gnames, nl.shift, nl.stride, nl.hdr, idx, out, cap);
}

// --- 4. UWorld ---
bool find_gworld(IMemReader& rd, const std::vector<Seg>& segs, uint64_t gobjectsAddr,
                 const ObjLayout& ol, uint64_t gnames, const NameLayout& nl, uint64_t base,
                 uint64_t& outOff, uint64_t& worldObj) {
    int32_t num = 0;
    if (!rd.rd(gobjectsAddr + ol.num_offset, num) || num <= 0) return false;

    std::vector<uint64_t> worlds;
    char nm[128];
    for (int32_t i = 0; i < num && worlds.size() < 8; ++i) {
        uint64_t obj = 0;
        if (!obj_at(rd, gobjectsAddr, ol, i, obj)) continue;
        uint64_t cls = 0;
        if (!rd.rd(obj + ol.class_off, cls) || !cls) continue;
        if (!obj_name_of(rd, gnames, nl, cls, ol.name_off, nm, sizeof nm)) continue;
        if (std::strcmp(nm, "World") != 0) continue;
        worlds.push_back(obj);
    }
    if (worlds.empty()) { std::printf("[gworld] không có object nào class=World\n"); return false; }
    std::printf("[gworld] tìm thấy %zu UWorld instance, quét __DATA tìm biến global...\n", worlds.size());

    for (const auto& s : segs) {
        if (s.name.rfind("__DATA", 0) != 0) continue;
        for (uint64_t a = s.start; a + 8 <= s.end; a += 8) {
            uint64_t v = 0;
            if (!rd.rd(a, v)) continue;
            for (uint64_t w : worlds) {
                if (v == w) {
                    outOff  = a - base;
                    worldObj = w;
                    std::printf("[gworld] off=0x%llx (world=0x%llx)\n",
                                (unsigned long long)outOff, (unsigned long long)w);
                    return true;
                }
            }
        }
    }
    std::printf("[gworld] không tìm được biến global trỏ tới UWorld\n");
    return false;
}

// --- 5. UWorld::PersistentLevel — quét object, không đoán offset ---
bool find_persist_level(IMemReader& rd, uint64_t world, uint64_t gnames, const NameLayout& nl,
                        const ObjLayout& ol, uint32_t& outOff, uint64_t& levelObj) {
    char nm[128];
    for (uint32_t off = 0; off < 0x800; off += 8) {
        uint64_t v = 0;
        if (!rd.rd(world + off, v) || !v) continue;
        if (!obj_name_of(rd, gnames, nl, v, ol.name_off, nm, sizeof nm)) continue;
        if (std::strcmp(nm, "PersistentLevel") == 0) {
            outOff = off; levelObj = v;
            std::printf("[persist_level] off=0x%x (level=0x%llx, name='%s')\n",
                        off, (unsigned long long)v, nm);
            return true;
        }
    }
    return false;
}

// --- 6. ULevel::Actors — TArray<AActor*> hợp lệ ---
bool find_level_actors(IMemReader& rd, uint64_t level, uint64_t gnames, const NameLayout& nl,
                       const ObjLayout& ol, uint32_t& outOff, uint64_t& actorsData, int32_t& actorsNum) {
    for (uint32_t off = 0; off < 0x1000; off += 8) {
        uint64_t data = 0;
        int32_t  n    = 0;
        if (!rd.rd(level + off, data) || !rd.rd(level + off + 8, n)) continue;
        if (!data || n <= 0 || n > 50000) continue;

        int ok = 0;
        char nm[128];
        for (int32_t i = 0; i < std::min(n, 32); ++i) {
            uint64_t actor = 0;
            if (!rd.rd(data + (uint64_t)i * 8, actor) || !actor) continue;
            int32_t nidx = 0;
            if (!rd.rd(actor + ol.name_off, nidx)) continue;
            if (name_try(rd, gnames, nl.shift, nl.stride, nl.hdr, nidx, nm, sizeof nm)) ++ok;
        }
        if (ok >= std::min(n, 32) / 2) {
            outOff = off; actorsData = data; actorsNum = n;
            std::printf("[level_actors] off=0x%x data=0x%llx num=%d (%d/%d name ok)\n",
                        off, (unsigned long long)data, n, ok, std::min(n, 32));
            return true;
        }
    }
    return false;
}

// --- 7. AActor::RootComponent + USceneComponent::ComponentToWorld ---
bool quat_ok(const float q[4]) {
    float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    return n > 0.9f && n < 1.1f;
}
bool coord_ok(float x, float y, float z) {
    auto f = [](float v) { return v == v && std::fabs(v) < 3.0e6f; };
    return (f(x) && f(y) && f(z)) && !(x == 0.f && y == 0.f && z == 0.f);
}

bool find_actor_root(IMemReader& rd, uint64_t actorsData, int32_t actorsNum, uint64_t gnames,
                     const NameLayout& nl, const ObjLayout& ol, uint32_t& rootOff, uint32_t& xformOff,
                     uint64_t& sampleRoot, float outPos[3]) {
    // chọn vài actor "trông giống nhân vật" — có component con
    for (int32_t i = 0; i < std::min(actorsNum, 48); ++i) {
        uint64_t actor = 0;
        if (!rd.rd(actorsData + (uint64_t)i * 8, actor) || !actor) continue;

        for (uint32_t roff = 0x80; roff < 0x400; roff += 8) {
            uint64_t comp = 0;
            if (!rd.rd(actor + roff, comp) || !comp) continue;

            for (uint32_t xoff = 0; xoff < 0x600; xoff += 4) {
                float q[4];
                if (!rd.read(comp + xoff, q, sizeof q)) continue;
                if (!quat_ok(q)) continue;
                float t[3];
                if (!rd.read(comp + xoff + 0x10, t, sizeof t)) continue;
                if (!coord_ok(t[0], t[1], t[2])) continue;

                rootOff = roff; xformOff = xoff; sampleRoot = comp;
                outPos[0] = t[0]; outPos[1] = t[1]; outPos[2] = t[2];
                std::printf("[actor_root] root=0x%x comp_xform=0x%x  actor=0x%llx comp=0x%llx "
                            "pos=(%.1f, %.1f, %.1f)\n",
                            roff, xoff, (unsigned long long)actor, (unsigned long long)comp,
                            t[0], t[1], t[2]);
                return true;
            }
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    pid_t pid = 0;
    std::string out = "offsets.json";
    std::string image;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--pid" && i + 1 < argc) pid = (pid_t)strtol(argv[++i], nullptr, 10);
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--image" && i + 1 < argc) image = argv[++i];
    }
    if (pid <= 0) {
        std::fprintf(stderr, "usage: dump_offsets --pid <pid> [--image name] [--out offsets.json]\n");
        return 2;
    }

    MachTaskReader rd;
    if (!rd.attach(pid)) return 3;
    if (!rd.resolveExecBase(image)) { std::fprintf(stderr, "exec base not found\n"); return 3; }
    const uint64_t base = rd.imageBase();
    std::printf("[base] 0x%llx\n", (unsigned long long)base);

    auto segs = image_segments(rd, base);
    for (const auto& s : segs)
        std::printf("  seg %-16s 0x%llx - 0x%llx\n", s.name.c_str(),
                    (unsigned long long)s.start, (unsigned long long)s.end);
    if (segs.empty()) { std::fprintf(stderr, "no segments\n"); return 3; }

    Offsets off;
    off.image = image;

    // 1) GNames
    NameLayout nl{};
    uint64_t gnamesOff = 0;
    uint32_t gnDeref = 0;
    if (!find_gnames(rd, segs, gnamesOff, nl, gnDeref, base)) {
        std::fprintf(stderr, "FAIL: gnames\n"); return 4;
    }
    off.gnames            = gnamesOff;
    off.gnames_deref      = gnDeref;
    off.name_block_shift  = nl.shift;
    off.name_entry_stride = nl.stride;
    off.name_entry_hdr    = nl.hdr;
    off.name_block_entries = 1u << nl.shift;
    uint64_t gnamesAddr = base + gnamesOff;
    if (gnDeref) rd.rd(gnamesAddr, gnamesAddr);

    // 2) GObjects
    ObjLayout ol;
    uint64_t gobjectsOff = 0;
    uint32_t goDeref = 0;
    if (!find_gobjects(rd, segs, gnamesAddr, nl, gobjectsOff, ol, goDeref, base)) {
        std::fprintf(stderr, "FAIL: gobjects\n"); return 4;
    }
    off.gobjects         = gobjectsOff;
    off.gobjects_deref   = goDeref;
    off.obj_chunk_shift  = ol.chunk_shift;
    off.obj_item_stride  = ol.item_stride;
    off.obj_item_object  = ol.item_object;
    off.uobj_class       = ol.class_off;
    off.uobj_name_index  = ol.name_off;
    uint64_t gobjectsAddr = base + gobjectsOff;
    if (goDeref) rd.rd(gobjectsAddr, gobjectsAddr);

    // 3) GWorld
    uint64_t worldOff = 0, worldObj = 0;
    if (!find_gworld(rd, segs, gobjectsAddr, ol, gnamesAddr, nl, base, worldOff, worldObj)) {
        std::fprintf(stderr, "FAIL: gworld\n"); return 4;
    }
    off.gworld = worldOff;

    // 4) PersistentLevel
    uint32_t plOff = 0;
    uint64_t levelObj = 0;
    if (!find_persist_level(rd, worldObj, gnamesAddr, nl, ol, plOff, levelObj)) {
        std::fprintf(stderr, "FAIL: persist_level\n"); return 4;
    }
    off.persist_level = plOff;

    // 5) Actors
    uint32_t laOff = 0;
    uint64_t actorsData = 0;
    int32_t  actorsNum = 0;
    if (!find_level_actors(rd, levelObj, gnamesAddr, nl, ol, laOff, actorsData, actorsNum)) {
        std::fprintf(stderr, "FAIL: level_actors\n"); return 4;
    }
    off.level_actors = laOff;

    // 6) Root + ComponentToWorld
    uint32_t rootOff = 0, xformOff = 0;
    uint64_t sampleRoot = 0;
    float pos[3] = {0, 0, 0};
    if (!find_actor_root(rd, actorsData, actorsNum, gnamesAddr, nl, ol, rootOff, xformOff, sampleRoot, pos)) {
        std::fprintf(stderr, "FAIL: actor_root / comp_xform\n"); return 4;
    }
    off.actor_root = rootOff;
    off.comp_xform = xformOff;

    off.dumped_at = wall_ms();
    if (!off.save(out)) { std::fprintf(stderr, "cannot write %s\n", out.c_str()); return 5; }

    std::printf("\nĐã ghi %s. Còn thiếu (phải dò riêng bằng dump_player):\n"
                "  pc_pawn, pawn_player_state, pawn_mesh, mesh_bones, ps_team, ps_health, ps_name,\n"
                "  pc_camera_mgr, cam_pov_loc, bone_head/neck/root\n"
                "Provider vẫn chạy được với phần đã có (vị trí + dist), nhưng thiếu tên/team/HP.\n",
                out.c_str());
    return 0;
}

#else
int main() {
    std::fprintf(stderr, "dump_offsets chỉ chạy trên iOS/macOS\n");
    return 1;
}
#endif
