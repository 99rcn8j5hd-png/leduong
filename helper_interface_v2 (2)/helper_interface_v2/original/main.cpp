#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct Player { uint64_t id; std::string name; float x, y; };
struct Snapshot { uint64_t generation; std::vector<Player> players; };

// Contract shared by every provider. The reader knows only this interface.
class ISnapshotProvider {
public:
    virtual ~ISnapshotProvider() = default;
    virtual bool connect() = 0;
    virtual bool readSnapshot(Snapshot& out) = 0;
    virtual bool alive() const = 0;
    virtual void disconnect() = 0;
};

class MockProvider final : public ISnapshotProvider {
    uint64_t tick_ = 0;
    bool connected_ = false;
public:
    bool connect() override { connected_ = true; tick_ = 0; return true; }
    bool readSnapshot(Snapshot& out) override {
        if (!connected_) return false;
        out = {1, {{101, "Alpha", float(tick_), 10.f}, {102, "Bravo", 20.f, float(tick_)}}};
        ++tick_;
        return true;
    }
    bool alive() const override { return connected_; }
    void disconnect() override { connected_ = false; }
};

// A second, genuinely different provider: reads a snapshot from a user-owned CSV file.
// CSV rows: generation,id,name,x,y
class CsvProvider final : public ISnapshotProvider {
    std::string path_;
    bool connected_ = false;
public:
    explicit CsvProvider(std::string path) : path_(std::move(path)) {}
    bool connect() override { std::ifstream f(path_); connected_ = f.good(); return connected_; }
    bool readSnapshot(Snapshot& out) override {
        if (!connected_) return false;
        std::ifstream f(path_);
        if (!f) { connected_ = false; return false; }
        Snapshot next{};
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string parts[5];
            bool ok = true;
            for (int i = 0; i < 5; ++i) if (!std::getline(ss, parts[i], ',')) { ok = false; break; }
            if (!ok) return false;
            try {
                uint64_t gen = std::stoull(parts[0]);
                if (!next.players.empty() && gen != next.generation) return false;
                next.generation = gen;
                next.players.push_back({std::stoull(parts[1]), parts[2], std::stof(parts[3]), std::stof(parts[4])});
            } catch (...) { return false; }
        }
        if (next.players.empty()) return false;
        out = std::move(next);
        return true;
    }
    bool alive() const override { return connected_; }
    void disconnect() override { connected_ = false; }
};

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
        }
        for (const auto& p : snap.players) {
            auto it = names_.find(p.id);
            if (it == names_.end()) continue; // Explicit policy: refresh names only on generation change.
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            std::cout << "{\"ts_ms\":" << ms << ",\"frame\":" << frame
                      << ",\"id\":" << p.id << ",\"x\":" << p.x
                      << ",\"y\":" << p.y << "}" << '\n';
        }
        return true;
    }
};

int main(int argc, char** argv) {
    if (argc < 2 || (std::string(argv[1]) != "mock" && std::string(argv[1]) != "csv")) {
        std::cerr << "Usage: helper mock [frames] [interval] | helper csv [file] [frames] [interval]\n";
        return 2;
    }
    bool csv = std::string(argv[1]) == "csv";
    if (csv && argc < 3) return 2;
    int base = csv ? 3 : 2;
    int frames = argc > base ? std::stoi(argv[base]) : 12;
    int interval = argc > base + 1 ? std::stoi(argv[base + 1]) : 3;
    if (frames < 1 || interval < 1) return 2;
    std::unique_ptr<ISnapshotProvider> provider;
    if (csv) provider = std::make_unique<CsvProvider>(argv[2]);
    else provider = std::make_unique<MockProvider>();
    Reader reader(*provider); // Same Reader class, no provider-specific branches.
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
}
