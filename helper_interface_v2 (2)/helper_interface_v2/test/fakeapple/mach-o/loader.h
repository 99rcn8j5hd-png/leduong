#pragma once
#include <cstdint>
#define MH_MAGIC_64 0xfeedfacf
#define MH_EXECUTE 2
#define LC_SEGMENT_64 0x19
struct mach_header_64 { uint32_t magic; int cputype; int cpusubtype; uint32_t filetype; uint32_t ncmds; uint32_t sizeofcmds; uint32_t flags; uint32_t reserved; };
struct load_command { uint32_t cmd; uint32_t cmdsize; };
struct segment_command_64 { uint32_t cmd; uint32_t cmdsize; char segname[16]; uint64_t vmaddr; uint64_t vmsize; uint64_t fileoff; uint64_t filesize; int maxprot; int initprot; uint32_t nsects; uint32_t flags; };
