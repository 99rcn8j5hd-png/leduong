#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
namespace zk {
static bool parseEntry(const unsigned char *blk, size_t capacity, uint32_t off,
                       std::string &out, uint32_t &nextOff) {
    if (off > capacity || capacity - off < 2) return false;

    uint16_t hdr;
    memcpy(&hdr, blk + off, 2);

    uint32_t len  = (uint32_t)(hdr >> 1);
    bool     wide = (hdr & 1) != 0;

    if (len == 0 || len > 512) return false;

    uint32_t bytes = len * (wide ? 2u : 1u);
    if (bytes > capacity - off - 2) return false;

    char tmp[520];
    if (!wide) {
        const unsigned char *p = blk + off + 2;
        for (uint32_t i = 0; i < len; i++) {
            unsigned char c = p[i];
            if (c < 0x20 || c > 0x7E) return false;
            tmp[i] = (char)c;
        }
    } else {
        for (uint32_t i = 0; i < len; i++) {
            uint16_t w;
            memcpy(&w, blk + off + 2 + i * 2, 2);
            if (w < 0x20 || w > 0x7E) return false;
            tmp[i] = (char)w;
        }
    }
    tmp[len] = 0;
    out.assign(tmp, len);
    nextOff = off + ((2u + bytes + 1u) & ~1u);
    return true;
}
}
