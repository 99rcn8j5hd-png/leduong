#pragma once
#include <cstdint>
#define PROC_PIDTBSDINFO 3
struct proc_bsdinfo { uint32_t pbi_flags; uint32_t pbi_status; uint32_t pbi_xstatus; uint32_t pbi_pid; uint32_t pbi_ppid; uint32_t pbi_uid; uint32_t pbi_gid; };
