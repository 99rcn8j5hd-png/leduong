// mach_reader.hpp — tách "cách đọc bộ nhớ" khỏi "logic đọc game".
// Đây chính là điểm cắm: nếu task port bị chặn (P_TRACED / hardened), chỉ cần
// đổi implementation của IMemReader sang kernel read (kfd/physrw), phần còn lại
// của provider không phải sửa một dòng.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <sys/types.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace hs {

class IMemReader {
public:
    virtual ~IMemReader() = default;
    virtual bool read(uint64_t addr, void* buf, size_t len) = 0;
    virtual bool valid(uint64_t addr, size_t len = 8) = 0;
    virtual uint64_t imageBase() const = 0;
    virtual bool alive() = 0;
    virtual const char* backendName() const = 0;

    template <class T>
    bool rd(uint64_t a, T& v) { return read(a, &v, sizeof(T)); }

    // chuỗi ASCII kết thúc NUL, cắt ở cap
    bool rd_str(uint64_t a, char* out, size_t cap) {
        if (!out || cap < 2) return false;
        out[0] = 0;
        std::string s;
        char chunk[32];
        for (size_t off = 0; off < cap - 1; off += sizeof(chunk)) {
            if (!read(a + off, chunk, sizeof(chunk))) break;
            for (size_t i = 0; i < sizeof(chunk); ++i) {
                if (chunk[i] == 0) { std::memcpy(out, s.c_str(), s.size()); out[s.size()] = 0; return true; }
                if (chunk[i] < 0x20 || chunk[i] > 0x7E) break;
                s.push_back(chunk[i]);
                if (s.size() >= cap - 1) { std::memcpy(out, s.c_str(), s.size()); out[s.size()] = 0; return true; }
            }
        }
        return false;
    }
};

#if defined(__APPLE__)

// --- backend 1: task port + mach_vm_read_overwrite (ngoài process, không inject)
class MachTaskReader final : public IMemReader {
public:
    bool attach(pid_t pid);
    ~MachTaskReader() override;

    bool read(uint64_t addr, void* buf, size_t len) override;
    bool valid(uint64_t addr, size_t len = 8) override;
    uint64_t imageBase() const override { return base_; }
    bool alive() override;
    const char* backendName() const override { return "task_port"; }

    pid_t pid() const { return pid_; }
    // đổi image base sau khi tự tìm / hoặc set thủ công
    void setImageBase(uint64_t b) { base_ = b; }
    bool resolveExecBase(const std::string& wantName);

private:
    pid_t       pid_  = 0;
    mach_port_t task_ = 0;
    uint64_t    base_ = 0;
};

// --- backend 2: kernel read (kfd / physrw) — bạn cắm hàm đọc vật lý vào đây
// Trên roothide, nếu proc_pidinfo báo P_TRACED thì đường task port sẽ chết;
// implementation này để sẵn chỗ thay: chỉ cần một hàm
//   bool (*)(uint64_t kaddr_or_paddr, void* buf, size_t len)
// rồi map địa chỉ ảo -> vật lý bằng bảng trang của process (kfd đã có sẵn).
class KernelReader final : public IMemReader {
public:
    using PhysReadFn = bool (*)(void* ud, uint64_t addr, void* buf, size_t len);

    KernelReader(void* ud, PhysReadFn fn) : ud_(ud), fn_(fn) {}
    bool read(uint64_t addr, void* buf, size_t len) override {
        if (!fn_) return false;
        return fn_(ud_, addr, buf, len);
    }
    bool valid(uint64_t, size_t = 8) override { return fn_ != nullptr; }
    uint64_t imageBase() const override { return base_; }
    void setImageBase(uint64_t b) { base_ = b; }
    bool alive() override { return fn_ != nullptr; }
    const char* backendName() const override { return "kernel_read"; }

private:
    void*      ud_   = nullptr;
    PhysReadFn fn_   = nullptr;
    uint64_t   base_ = 0;
};

#else  // ---- host build (Linux/mac không mach) : stub để biên dịch kiểm tra ----

class MachTaskReader final : public IMemReader {
public:
    bool attach(pid_t) { return false; }
    bool read(uint64_t, void*, size_t) override { return false; }
    bool valid(uint64_t, size_t = 8) override { return false; }
    uint64_t imageBase() const override { return 0; }
    bool alive() override { return false; }
    const char* backendName() const override { return "stub"; }
    pid_t pid() const { return 0; }
    void setImageBase(uint64_t) {}
    bool resolveExecBase(const std::string&) { return false; }
};

class KernelReader final : public IMemReader {
public:
    using PhysReadFn = bool (*)(void* ud, uint64_t addr, void* buf, size_t len);
    KernelReader(void* = nullptr, PhysReadFn = nullptr) {}
    bool read(uint64_t, void*, size_t) override { return false; }
    bool valid(uint64_t, size_t = 8) override { return false; }
    uint64_t imageBase() const override { return 0; }
    void setImageBase(uint64_t) {}
    bool alive() override { return false; }
    const char* backendName() const override { return "stub"; }
};

#endif

} // namespace hs
