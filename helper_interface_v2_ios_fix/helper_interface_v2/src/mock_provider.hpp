// mock_provider.hpp — giữ nguyên logic bản bạn gửi, chỉ thêm field mở rộng.
#pragma once

#include "iface/snapshot.hpp"
#include "iface/util.hpp"

namespace hs {

class MockProvider final : public ISnapshotProvider {
    uint64_t tick_      = 0;
    bool     connected_ = false;

public:
    bool connect() override { connected_ = true; tick_ = 0; return true; }

    bool readSnapshot(Snapshot& out) override {
        if (!connected_) return false;
        out.generation = 1;
        out.players.clear();
        Player a;
        a.id = 101; a.name = "Alpha"; a.x = float(tick_); a.y = 10.f;
        a.z = 0.f; a.team = 1; a.health = 100.f; a.dist = 3000.f;
        a.root = {a.x, a.y, a.z}; a.head = {a.x, a.y, a.z + 180.f};
        Player b;
        b.id = 102; b.name = "Bravo"; b.x = 20.f; b.y = float(tick_);
        b.z = 0.f; b.team = 2; b.health = 87.f; b.dist = 4200.f;
        b.root = {b.x, b.y, b.z}; b.head = {b.x, b.y, b.z + 180.f};
        out.players.push_back(std::move(a));
        out.players.push_back(std::move(b));
        out.stamp_ms = now_ms();
        ++tick_;
        return true;
    }

    bool alive() const override { return connected_; }
    void disconnect() override { connected_ = false; }
    const char* name() const override { return "mock"; }
    uint32_t rosterSize() const override { return connected_ ? 2 : 0; }
};

} // namespace hs
