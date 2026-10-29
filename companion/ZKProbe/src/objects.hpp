// objects.hpp — tim FUObjectArray (GObjects) + duyet instance
#pragma once

#include "fname.hpp"

namespace zk {

struct UObjectArray {
    uintptr_t arrayAddr = 0;   // dia chi struct TUObjectArray (GObjects)
    uintptr_t objectsPtr = 0;  // mang con tro chunk: FUObjectItem**
    int32_t maxElements = 0;
    int32_t numElements = 0;
    int32_t maxChunks = 0;
    int32_t numChunks = 0;
    int32_t itemSize = 0x10;   // 0x10 hoac 0x18
    uintptr_t rva = 0;         // arrayAddr - mainBase
    bool found() const { return arrayAddr != 0; }
};

UObjectArray findObjectArray(const FNamePool &pool);

inline uintptr_t objectAt(const UObjectArray &a, uint32_t idx) {
    if (idx >= (uint32_t)a.numElements) return 0;
    uintptr_t chunk = rdv<uintptr_t>(a.objectsPtr + (uintptr_t)(idx >> 16) * 8);
    if (!chunk) return 0;
    return rdv<uintptr_t>(chunk + (uintptr_t)(idx & 0xFFFF) * (uint32_t)a.itemSize);
}

std::string objectClassName(const FNamePool &pool, uintptr_t obj);
std::string objectName(const FNamePool &pool, uintptr_t obj);

} // namespace zk
