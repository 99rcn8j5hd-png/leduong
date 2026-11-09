#pragma once
#include <cstdint>
#include <sys/proc_info.h>
#define PROC_PIDPATHINFO_MAXSIZE 4096
int proc_pidpath(int, void*, uint32_t);
int proc_regionfilename(int, uint64_t, void*, uint32_t);
int proc_pidinfo(int, int, uint64_t, void*, int);
