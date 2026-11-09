#if defined(__APPLE__)

#include "mach_reader.hpp"

#include "proc_compat.hpp"
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <mach-o/loader.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <vector>

namespace hs {

static_assert(sizeof(vm_address_t) == 8, "This reader requires a 64-bit target");

MachTaskReader::~MachTaskReader() {
    if (task_ != MACH_PORT_NULL && task_ != 0) mach_port_deallocate(mach_task_self(), task_);
}

bool MachTaskReader::attach(pid_t pid) {
    pid_ = pid;
    task_ = MACH_PORT_NULL;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &task_);
    if (kr != KERN_SUCCESS) {
        std::fprintf(stderr, "[mach] task_for_pid(%d) failed: %s (%d)\n",
                     pid, mach_error_string(kr), kr);
        return false;
    }
    return true;
}

bool MachTaskReader::read(uint64_t addr, void* buf, size_t len) {
    if (!task_ || !buf || !len) return false;
    vm_size_t got = 0;
    kern_return_t kr = vm_read_overwrite(task_, (vm_address_t)addr,
                                              (vm_size_t)len,
                                              (vm_address_t)buf, &got);
    return kr == KERN_SUCCESS && got == len;
}

bool MachTaskReader::valid(uint64_t addr, size_t len) {
    if (!task_ || !addr) return false;
    // page-align rồi hỏi region chứa địa chỉ
    vm_address_t a = addr & ~0xFFFULL;
    vm_size_t    sz = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t obj = MACH_PORT_NULL;
    kern_return_t kr = vm_region_64(task_, &a, &sz, VM_REGION_BASIC_INFO_64,
                                      (vm_region_info_t)&info, &count, &obj);
    if (obj != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), obj);
    if (kr != KERN_SUCCESS) return false;
    if (!(info.protection & VM_PROT_READ)) return false;
    if (info.max_protection == VM_PROT_NONE && info.protection == VM_PROT_NONE) return false;
    return (addr + len) <= (a + sz);
}

bool MachTaskReader::alive() {
    if (!pid_) return false;
    return kill(pid_, 0) == 0;
}

bool MachTaskReader::resolveExecBase(const std::string& wantName) {
    if (!pid_) return false;

    char execPath[proc_compat::PROC_PIDPATHINFO_MAXSIZE] = {0};
    proc_compat::proc_pidpath(pid_, execPath, sizeof execPath);

    vm_address_t addr = 0;
    uint64_t best = 0;
    bool     matched = false;

    while (true) {
        vm_size_t sz = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t obj = MACH_PORT_NULL;
        vm_address_t cur = addr;
        kern_return_t kr = vm_region_64(task_, &cur, &sz, VM_REGION_BASIC_INFO_64,
                                          (vm_region_info_t)&info, &count, &obj);
        if (obj != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), obj);
        if (kr != KERN_SUCCESS) break;

        if (info.protection & VM_PROT_READ) {
            mach_header_64 hdr{};
            if (read(cur, &hdr, sizeof hdr) && hdr.magic == MH_MAGIC_64 &&
                hdr.filetype == MH_EXECUTE) {
                char path[proc_compat::PROC_PIDPATHINFO_MAXSIZE] = {0};
                proc_compat::proc_regionfilename(pid_, cur, path, sizeof path);
                std::string p        = path;
                std::string baseName = p.substr(p.find_last_of('/') + 1);
                bool ok = wantName.empty() || baseName == wantName ||
                          (!execPath[0] ? false : p == execPath);
                if (ok && (best == 0 || cur < best)) {
                    best    = cur;
                    matched = true;
                }
            }
        }
        addr = cur + sz;
        if (sz == 0) break;
    }

    if (!matched) return false;
    base_ = best;
    return true;
}

} // namespace hs

#endif // __APPLE__
