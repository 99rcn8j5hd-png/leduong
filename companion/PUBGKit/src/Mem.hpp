// Mem.hpp - memory access helpers; successful reads do not guarantee object lifetime
#pragma once
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <mach/mach_vm.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

bool memRead(uintptr_t addr, void* out, size_t len);
bool memWrite(uintptr_t addr, const void* in, size_t len);
bool memValid(uintptr_t addr, size_t len = 8);

template <typename T>
static inline T rd(uintptr_t addr, T def = T{}) {
    T v;
    return memRead(addr, (void*)&v, sizeof(T)) ? v : def;
}

template <typename T>
static inline bool wr(uintptr_t addr, T v) {
    return memWrite(addr, &v, sizeof(T));
}

static inline uintptr_t rdPtr(uintptr_t addr) { return rd<uintptr_t>(addr, 0); }
static inline bool looksLikePtr(uintptr_t p) {
    return p > 0x100000000ULL && p < 0x0000800000000000ULL && (p & 7) == 0;
}
