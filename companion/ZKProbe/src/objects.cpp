// objects.cpp — tim GObjects (TUObjectArray) va duyet instance
#include "objects.hpp"
#include <string.h>
#include <ctype.h>

namespace zk {

static std::string fnameOf(const FNamePool &pool, uintptr_t addr) {
    int32_t idx = rdv<int32_t>(addr);
    int32_t num = rdv<int32_t>(addr + 4);
    if (idx <= 0) return std::string();
    std::string s = fnameById(pool.blocksAddr, (uint32_t)idx);
    if (s.empty()) return s;
    if (num > 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "_%d", num - 1);
        s += buf;
    }
    return s;
}

std::string objectClassName(const FNamePool &pool, uintptr_t obj) {
    if (!obj) return std::string();
    uintptr_t cls = rdv<uintptr_t>(obj + 0x10); // UObject::ClassPrivate
    if (!cls) return std::string();
    return fnameOf(pool, cls + 0x18);           // UObject::NamePrivate
}

std::string objectName(const FNamePool &pool, uintptr_t obj) {
    if (!obj) return std::string();
    return fnameOf(pool, obj + 0x18);
}

static bool validClassString(const std::string &cls) {
    if (cls.size() < 2 || cls.size() > 96) return false;
    if (!isalpha((unsigned char)cls[0])) return false;
    for (size_t i = 0; i < cls.size(); i++) {
        unsigned char c = (unsigned char)cls[i];
        if (!(isalnum(c) || c == '_')) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
UObjectArray findObjectArray(const FNamePool &pool) {
    UObjectArray best;
    int bestScore = 0;

    static const uint32_t probes[] = {1, 2, 3, 10, 50, 200, 1000, 5000,
                                      20000, 60000, 70000, 130000};

    scanWritable([&](uintptr_t base, unsigned char *p, size_t n) {
        for (size_t i = 0; i + 0x20 <= n; i += 8) {
            // --- filter re truoc (khong syscall) ---
            uintptr_t objectsPtr;
            memcpy(&objectsPtr, p + i, 8);
            if (objectsPtr < 0x10000000ULL) continue;
            if (objectsPtr & 7) continue;
            if (objectsPtr > 0x0000200000000000ULL) continue;

            int32_t maxEl, numEl, maxCh, numCh;
            memcpy(&maxEl, p + i + 8, 4);
            memcpy(&numEl, p + i + 12, 4);
            memcpy(&maxCh, p + i + 16, 4);
            memcpy(&numCh, p + i + 20, 4);

            if (numEl < 1000 || numEl > maxEl) continue;
            if (maxEl > 5000000) continue;
            if (maxCh <= 0 || maxCh > 4096) continue;
            if (numCh <= 0 || numCh > maxCh) continue;

            // --- filter vua: con tro phai nam trong vung doc duoc ---
            if (!inReadableRegion(objectsPtr)) continue;
            uintptr_t chunk0 = rdv<uintptr_t>(objectsPtr);
            if (!chunk0 || !inReadableRegion(chunk0)) continue;

            uintptr_t arrayAddr = base + i;

            for (int itemSize = 0x10; itemSize <= 0x18; itemSize += 8) {
                int ok = 0, tried = 0;
                for (size_t k = 0; k < sizeof(probes) / sizeof(probes[0]); k++) {
                    uint32_t idx = probes[k];
                    if (idx >= (uint32_t)numEl) continue;
                    tried++;

                    uintptr_t chunk = rdv<uintptr_t>(objectsPtr + (uintptr_t)(idx >> 16) * 8);
                    if (!chunk) continue;

                    uintptr_t obj = rdv<uintptr_t>(chunk + (uintptr_t)(idx & 0xFFFF) * (uint32_t)itemSize);
                    if (!obj || !inReadableRegion(obj)) continue;

                    std::string cls = objectClassName(pool, obj);
                    if (validClassString(cls)) ok++;
                }
                if (tried == 0) continue;

                int score = ok * 100 / tried;
                if (score >= 80 && ok >= 4 && score > bestScore) {
                    bestScore = score;
                    best.arrayAddr = arrayAddr;
                    best.objectsPtr = objectsPtr;
                    best.maxElements = maxEl;
                    best.numElements = numEl;
                    best.maxChunks = maxCh;
                    best.numChunks = numCh;
                    best.itemSize = itemSize;
                }
            }
        }
    });

    if (best.arrayAddr && belongsToMainImage(best.arrayAddr)) best.rva = best.arrayAddr - mainBase();
    return best;
}

} // namespace zk
