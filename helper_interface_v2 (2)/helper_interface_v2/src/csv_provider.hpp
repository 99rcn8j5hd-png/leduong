// csv_provider.hpp — bản gốc của bạn + 3 sửa nhỏ:
//  1) generation lấy theo dòng đầu, các dòng gen khác bị SKIP (log 1 lần)
//     thay vì trả false làm reader ngắt kết nối cả frame.
//  2) đọc file 1 lần/frame vẫn giữ (đúng ngữ nghĩa "file snapshot"), nhưng
//     parse bằng strtod/stoull bọc try để không ném exception ra ngoài.
//  3) nếu file rỗng/không có dòng hợp lệ → false (giữ nguyên).
#pragma once

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "iface/snapshot.hpp"
#include "iface/util.hpp"

namespace hs {

class CsvProvider final : public ISnapshotProvider {
    std::string path_;
    bool        connected_ = false;
    bool        warned_mixed_ = false;

    static bool parse_u64(const std::string& s, uint64_t& out) {
        if (s.empty()) return false;
        errno = 0;
        char* end = nullptr;
        unsigned long long v = std::strtoull(s.c_str(), &end, 10);
        if (errno || end == s.c_str() || *end != '\0') return false;
        out = (uint64_t)v;
        return true;
    }
    static bool parse_f32(const std::string& s, float& out) {
        if (s.empty()) return false;
        errno = 0;
        char* end = nullptr;
        float v = std::strtof(s.c_str(), &end);
        if (errno || end == s.c_str() || *end != '\0') return false;
        out = v;
        return true;
    }

public:
    explicit CsvProvider(std::string path) : path_(std::move(path)) {}

    bool connect() override {
        std::ifstream f(path_);
        connected_ = f.good();
        warned_mixed_ = false;
        return connected_;
    }

    bool readSnapshot(Snapshot& out) override {
        if (!connected_) return false;
        std::ifstream f(path_);
        if (!f) { connected_ = false; return false; }

        Snapshot     next{};
        bool         have_gen = false;
        std::string  line;
        size_t       lineno = 0;
        std::string  parts[5];

        while (std::getline(f, line)) {
            ++lineno;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;

            std::stringstream ss(line);
            bool ok = true;
            for (int i = 0; i < 5; ++i)
                if (!std::getline(ss, parts[i], ',')) { ok = false; break; }
            if (!ok) continue;                      // dòng hỏng → bỏ, không giết cả frame

            uint64_t gen = 0, id = 0;
            float    x = 0.f, y = 0.f;
            if (!parse_u64(parts[0], gen) || !parse_u64(parts[1], id) ||
                !parse_f32(parts[3], x) || !parse_f32(parts[4], y))
                continue;

            if (!have_gen) { next.generation = gen; have_gen = true; }
            else if (gen != next.generation) {
                if (!warned_mixed_) {
                    std::fprintf(stderr, "[csv] %s: trộn generation (dòng %zu cũ=%llu mới=%llu), bỏ dòng lệch\n",
                                 path_.c_str(), lineno,
                                 (unsigned long long)next.generation, (unsigned long long)gen);
                    warned_mixed_ = true;
                }
                continue;
            }

            Player p;
            p.id = id;
            p.name = parts[2];
            p.x = x; p.y = y; p.z = 0.f;
            p.root = {x, y, 0.f};
            p.head = {x, y, 0.f};
            next.players.push_back(std::move(p));
        }

        if (!have_gen || next.players.empty()) return false;
        next.stamp_ms = now_ms();
        out = std::move(next);
        return true;
    }

    bool alive() const override { return connected_; }
    void disconnect() override { connected_ = false; }
    const char* name() const override { return "csv"; }
    uint32_t rosterSize() const override { return 0; }
};

} // namespace hs
