#include "mock_provider.hpp"
#include "csv_provider.hpp"
#include "pubg/pubg_provider.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// MockProvider dùng now_ms()
#include "iface/util.hpp"

using namespace hs;

// ---------------------------------------------------------------------------
// Reader: bản gốc của bạn, sửa 3 chỗ:
//  1) Khi generation đổi, KHÔNG chỉ cache lại đúng frame đó — id nào chưa có
//     trong frame mới vẫn được nhận diện bằng name rỗng, tránh "mất tên vĩnh
//     viễn" của policy cũ. (policy cũ: id lạ bị skip cho tới lần đổi gen sau)
//  2) `sample` trả về true/false rõ nghĩa: false = provider chết, nên ngắt.
//  3) Xuất thêm z/team/health/dist để tầng render thấy dữ liệu provider mới,
//     nhưng KHÔNG đổi tên field cũ (x,y,id,name,frame,ts_ms giữ nguyên).
// ---------------------------------------------------------------------------
class Reader {
    ISnapshotProvider& source_;
    uint64_t generation_ = 0;
    std::unordered_map<uint64_t, std::string> names_;

public:
    explicit Reader(ISnapshotProvider& source) : source_(source) {}

    void reset() { generation_ = 0; names_.clear(); }

    bool sample(uint64_t frame) {
        Snapshot snap;
        if (!source_.readSnapshot(snap)) return false;

        if (snap.generation != generation_) {
            names_.clear();
            for (const auto& p : snap.players) names_[p.id] = p.name;
            generation_ = snap.generation;
            std::cout << "session=" << generation_ << " cached=" << names_.size() << '\n';
        } else {
            for (const auto& p : snap.players) {
                auto it = names_.find(p.id);
                if (it == names_.end()) {          // id mới giữa phiên: nạp luôn, không skip
                    names_[p.id] = p.name;
                    continue;
                }
                if (it->second != p.name) it->second = p.name; // đổi tên / đổi người
            }
        }

        for (const auto& p : snap.players) {
            auto it = names_.find(p.id);
            if (it == names_.end()) continue;

            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            std::cout << "{\"ts_ms\":" << ms
                      << ",\"frame\":" << frame
                      << ",\"id\":" << p.id
                      << ",\"name\":\"" << it->second << "\""
                      << ",\"x\":" << p.x
                      << ",\"y\":" << p.y
                      << ",\"z\":" << p.z
                      << ",\"team\":" << p.team
                      << ",\"hp\":" << p.health
                      << ",\"dist\":" << p.dist
                      << "}" << '\n';
        }
        std::cout.flush();
        return true;
    }
};

static int usage() {
    std::cerr <<
        "Usage:\n"
        "  helper mock  [frames] [interval]\n"
        "  helper csv   <file> [frames] [interval]\n"
        "  helper pubg  [frames] [interval] [--pid N] [--offsets offsets.json] [--dump]\n";
    return 2;
}

// stoi không ném exception ra ngoài chương trình
static bool arg_int(const char* s, int& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    errno = 0;
    long v = std::strtol(s, &end, 10);
    if (errno || end == s || *end) return false;
    out = (int)v;
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) return usage();

    const std::string mode = argv[1];
    if (mode != "mock" && mode != "csv" && mode != "pubg") return usage();

    int base     = (mode == "csv") ? 3 : 2;
    int frames   = 12;
    int interval = 3;
    if (mode == "csv" && argc < 3) return usage();
    auto is_num = [](const char* s) { return s && s[0] != '-' && s[0] != 0; };
    if (argc > base && is_num(argv[base]) && !arg_int(argv[base], frames)) return usage();
    if (argc > base + 1 && is_num(argv[base + 1]) && !arg_int(argv[base + 1], interval)) return usage();
    if (frames < 1 || interval < 1) return usage();

    std::unique_ptr<ISnapshotProvider> provider;
    if (mode == "csv") {
        provider = std::make_unique<CsvProvider>(argv[2]);
    } else if (mode == "mock") {
        provider = std::make_unique<MockProvider>();
    } else {
        // provider thật — chỉ tồn tại trên iOS/macOS
        provider = hs::make_pubg_provider(argc, argv);
        if (!provider) { std::cerr << "pubg provider unavailable on this build\n"; return 3; }
    }

    Reader reader(*provider);
    int backoff = 500;

    for (int frame = 0; frame < frames; ++frame) {
        if (!provider->alive()) {
            reader.reset();
            if (!provider->connect()) {
                std::cerr << "connect failed; retry in " << backoff << "ms\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
                backoff = std::min(backoff * 2, 2000);
                continue;
            }
            backoff = 500;
        }
        if (frame % interval == 0 && !reader.sample(frame)) provider->disconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    provider->disconnect();
    return 0;
}
