// snapshot.hpp — hợp đồng dùng chung giữa mọi provider và Reader.
// Nguyên tắc: APPEND-ONLY. 4 field đầu của Player và 2 field của Snapshot
// giữ nguyên thứ tự y như bản bạn gửi, nên MockProvider / CsvProvider của bạn
// biên dịch lại không phải sửa một dòng nào.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hs {

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

struct Player {
    // ---- v1 contract (không đổi thứ tự) ----
    uint64_t    id   = 0;
    std::string name;
    float       x    = 0.f;   // world X (cm)
    float       y    = 0.f;   // world Y (cm)

    // ---- v2 mở rộng (append-only) ----
    float   z     = 0.f;
    float   yaw   = 0.f;      // độ, 0 = +X
    int32_t team  = -1;       // -1 = chưa đọc được
    float   health= 0.f;
    uint8_t bones = 0;        // số bone đọc được (0 = không có skeleton)
    float   dist  = 0.f;      // khoảng cách tới self (cm)
    Vec3    head{};           // đỉnh đầu (bone head) — 0 nếu không đọc được
    Vec3    root{};           // vị trí chân
    uint64_t actor = 0;       // địa chỉ AActor* — dùng để render bám mục tiêu
};

struct Snapshot {
    uint64_t            generation = 0;
    std::vector<Player> players;
    // ---- v2 mở rộng ----
    Vec3     self{};          // vị trí người chơi mình
    uint64_t stamp_ms = 0;    // thời điểm đọc xong (monotonic ms)
};

// Reader CHỈ biết interface này. Không nhánh if nào biết bên dưới là mock/csv/pubg.
class ISnapshotProvider {
public:
    virtual ~ISnapshotProvider() = default;
    virtual bool connect()                        = 0;
    virtual bool readSnapshot(Snapshot& out)      = 0;
    virtual bool alive() const                    = 0;
    virtual void disconnect()                     = 0;
    // thêm, có default → provider cũ không cần override
    virtual const char* name() const { return "unknown"; }
    // 0 = không có khái niệm "roster"; >0 = số actor đọc được frame gần nhất
    virtual uint32_t rosterSize() const { return 0; }
};

} // namespace hs
