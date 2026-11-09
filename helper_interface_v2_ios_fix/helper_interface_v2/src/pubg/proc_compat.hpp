// Real Darwin ABI declarations; no fake SDK headers or stub implementations.
// Interfaces: apple-oss-distributions/xnu, libsyscall/wrappers/libproc/libproc.h
// Layout: apple-oss-distributions/xnu, bsd/sys/proc_info.h
#pragma once
#include <cstdint>
#include <cstddef>
#include <cerrno>
#include <dlfcn.h>

namespace hs::proc_compat {
constexpr int PROC_PIDTBSDINFO = 3;
constexpr unsigned PROC_PIDPATHINFO_MAXSIZE = 4096;
constexpr uint32_t PROC_FLAG_TRACED = 2;
struct proc_bsdinfo {
    uint32_t pbi_flags, pbi_status, pbi_xstatus, pbi_pid, pbi_ppid;
    uint32_t pbi_uid, pbi_gid, pbi_ruid, pbi_rgid, pbi_svuid, pbi_svgid;
    uint32_t rfu_1;
    char pbi_comm[16];
    char pbi_name[32];
    uint32_t pbi_nfiles, pbi_pgid, pbi_pjobc, e_tdev, e_tpgid;
    int32_t pbi_nice;
    uint64_t pbi_start_tvsec, pbi_start_tvusec;
};
static_assert(sizeof(proc_bsdinfo) == 136, "Darwin proc_bsdinfo ABI mismatch");
static_assert(offsetof(proc_bsdinfo, pbi_start_tvsec) == 120);

inline int proc_pidinfo(int pid, int flavor, uint64_t arg, void* out, int size) {
    using Fn = int (*)(int, int, uint64_t, void*, int);
    static const auto fn = reinterpret_cast<Fn>(dlsym(RTLD_DEFAULT, "proc_pidinfo"));
    if (!fn) { errno = ENOSYS; return -1; }
    return fn(pid, flavor, arg, out, size);
}
inline int proc_pidpath(int pid, void* out, uint32_t size) {
    using Fn = int (*)(int, void*, uint32_t);
    static const auto fn = reinterpret_cast<Fn>(dlsym(RTLD_DEFAULT, "proc_pidpath"));
    if (!fn) { errno = ENOSYS; return -1; }
    return fn(pid, out, size);
}
inline int proc_regionfilename(int pid, uint64_t address, void* out, uint32_t size) {
    using Fn = int (*)(int, uint64_t, void*, uint32_t);
    static const auto fn = reinterpret_cast<Fn>(dlsym(RTLD_DEFAULT, "proc_regionfilename"));
    if (!fn) { errno = ENOSYS; return -1; }
    return fn(pid, address, out, size);
}
} // namespace hs::proc_compat
