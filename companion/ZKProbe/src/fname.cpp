// fname.cpp — implementation: quet FNamePool + doc ten (experimental name-pool model; not verified for a game build)
#include "fname.hpp"
#include "entry_parser.hpp"
#include <string.h>

namespace zk {

// ---------------------------------------------------------------------------
// Parse 1 FNameEntry tai byte-offset `off` trong block 64 KB.
// Header: uint16 { bIsWide:1, Len:15 }
// Ten nam ngay sau header (2 byte). Entry ke tiep = Align2(2 + Len * (wide?2:1))
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
struct BlockReader {
    uintptr_t blocks = 0;
    uint16_t  loaded = 0xFFFF;
    std::vector<unsigned char> buf;

    explicit BlockReader(uintptr_t b) : blocks(b), buf(kBlockSize) {}

    bool load(uint16_t bi) {
        if (bi >= kMaxBlocks) return false;
        if (bi == loaded) return true;
        loaded = 0xFFFF;
        uintptr_t blk = rdv<uintptr_t>(blocks + (uintptr_t)bi * 8);
        if (!blk) return false;
        if (!rd(blk, buf.data(), kBlockSize)) return false;
        loaded = bi;
        return true;
    }

    bool name(uint32_t id, std::string &out) {
        uint16_t bi = (uint16_t)(id >> 16);
        if (!load(bi)) return false;
        uint32_t next = 0;
        return parseEntry(buf.data(), buf.size(), id & 0xFFFF, out, next);
    }
};

std::string fnameById(uintptr_t blocks, uint32_t id) {
    BlockReader r(blocks);
    std::string s;
    if ((id >> 16) >= kMaxBlocks) return {};
    if (!r.name(id, s)) return std::string();
    return s;
}

// ---------------------------------------------------------------------------
// Cham diem 1 dia chi mang Blocks[]: entry[0] phai la "None" va chuoi entry
// lien tiep phai parse duoc. -1 = khong hop le.
// ---------------------------------------------------------------------------
int poolScore(uintptr_t blocksAddr) {
    uintptr_t blk0 = rdv<uintptr_t>(blocksAddr);
    if (blk0 < 0x1000) return -1;

    std::vector<unsigned char> b(kBlockSize);
    if (!rd(blk0, b.data(), kBlockSize)) return -1;

    uint32_t off = 0, next = 0;
    int n = 0;
    std::string nm;

    while (off + 2 <= kBlockSize) {
        if (!parseEntry(b.data(), b.size(), off, nm, next)) break;
        if (n == 0 && nm != "None") return -1;
        n++;
        off = next;
        if (n >= 200) break;
    }
    if (n < 50) return -1;
    return 100;
}

// walk n entry lien tiep tu 1 buffer nho -> dem entry hop le
static int walkCount(const unsigned char *buf, size_t size, int limit) {
    uint32_t off = 0, next = 0;
    int n = 0;
    std::string nm;
    while (off + 2 <= size && parseEntry(buf, size, off, nm, next)) {
        n++;
        off = next;
        if (n >= limit) break;
    }
    return n;
}

// ---------------------------------------------------------------------------
FNamePool findFNamePool() {
    FNamePool out;

    // "None" entry: 08 00 'N' 'o' 'n' 'e'  (so 6 byte dau, 2 byte sau la entry ke tiep)
    const uint64_t kMask = 0x0000FFFFFFFFFFFFULL;
    const uint64_t kNone = 0x0000656E6F4E0008ULL;

    std::vector<uintptr_t> cands;
    std::vector<unsigned char> probe(0x2000);

    auto tryCandidate = [&](uintptr_t a) {
        if (cands.size() >= 64) return;
        if (!rd(a, probe.data(), probe.size())) return;
        if (walkCount(probe.data(), probe.size(), 40) < 40) return;
        if (std::find(cands.begin(),cands.end(),a)==cands.end()) cands.push_back(a);
    };

    // --- pass 1: tim block0 (signature "None") trong vung RW ---
    scanWritable([&](uintptr_t base, unsigned char *p, size_t n) {
        for (size_t i = 0; i + 8 <= n; i += 8) {
            uint64_t v;
            memcpy(&v, p + i, 8);
            if ((v & kMask) != kNone) continue;
            tryCandidate(base + i);
        }
    });

    // fallback: scan two-byte aligned entries
    if (cands.empty()) {
        scanWritable([&](uintptr_t base, unsigned char *p, size_t n) {
            for (size_t i = 0; i + 6 <= n; i += 2) {
                if (p[i] != 0x08 || p[i + 1] != 0x00) continue;
                if (p[i + 2] != 'N' || p[i + 3] != 'o' || p[i + 4] != 'n' || p[i + 5] != 'e') continue;
                tryCandidate(base + i);
            }
        });
    }

    fprintf(stderr,"[ZKProbe] block0 candidates=%zu\n",cands.size());
    if (cands.empty()) return out;

    // --- pass 2: tim mang Blocks[] (con tro tro ve block0), chi quet __DATA main image ---
    struct Hit {
        uintptr_t blocksAddr;
        int names;
    };
    std::vector<Hit> hits;

    uintptr_t mb = mainBase();

    auto checkSlot = [&](uintptr_t blocksAddr) {
        if (poolScore(blocksAddr) < 0) return;

        // dem so ten that su doc duoc tren vai block dau
        BlockReader r(blocksAddr);
        std::string nm;
        int total = 0;
        for (uint16_t bi = 0; bi < 16; bi++) {
            if (!rdv<uintptr_t>(blocksAddr + (uintptr_t)bi * 8)) break;
            if (!r.load(bi)) break;
            uint32_t off = 0, next = 0;
            int n = 0;
            while (off + 2 <= kBlockSize && parseEntry(r.buf.data(), r.buf.size(), off, nm, next)) {
                n++;
                total++;
                off = next;
            }
            if (n == 0) break;
        }
        if (total < 500) return;
        Hit h;
        h.blocksAddr = blocksAddr;
        h.names = total;
        hits.push_back(h);
    };

    auto scanPointers = [&](bool onlyMainImage) {
        const std::vector<Region> &regs = allRegions();
        std::vector<unsigned char> buf(kScanStep + 16);
        for (size_t ri = 0; ri < regs.size(); ri++) {
            const Region &r = regs[ri];
            if (!r.writable) continue;

            if (onlyMainImage) {
                ImgRange ir;
                if (!imageRangeContaining(r.start, ir) || ir.base != mb) continue;
            }

            for (uintptr_t a = r.start; a < r.end; a += kScanStep) {
                size_t want = (size_t)std::min<uintptr_t>(kScanStep, r.end - a);
                if (want < 8) break;
                if (!allowScan(want)) return;
                if (!rd(a, buf.data(), want)) continue;

                for (size_t i = 0; i + 8 <= want; i += 8) {
                    uintptr_t v;
                    memcpy(&v, buf.data() + i, 8);
                    bool match = false;
                    for (size_t k = 0; k < cands.size(); k++) {
                        if (v == cands[k]) { match = true; break; }
                    }
                    if (!match) continue;
                    checkSlot(a + i);
                }
            }
        }
    };

    scanPointers(true);
    if (hits.empty()) scanPointers(false);
    if (hits.empty()) return out;

    Hit *best = &hits[0];
    for (size_t i = 0; i < hits.size(); i++) {
        if (belongsToMainImage(hits[i].blocksAddr)) { best = &hits[i]; break; }
    }

    out.blocksAddr = best->blocksAddr;
    out.poolAddr   = 0; // The pool header displacement has not been validated.
    out.rvaBlocks  = belongsToMainImage(best->blocksAddr) ? best->blocksAddr - mb : 0;
    out.rvaPool    = 0;
    return out;
}

// ---------------------------------------------------------------------------
uint32_t findNameId(uintptr_t blocks, const std::string &name) {
    BlockReader r(blocks);
    for (uint16_t bi = 0; bi < kMaxBlocks; bi++) {
        if (!rdv<uintptr_t>(blocks + (uintptr_t)bi * 8)) break;
        if (!r.load(bi)) break;

        uint32_t off = 0, next = 0;
        int guard = 0;
        std::string nm;
        while (off + 2 <= kBlockSize && parseEntry(r.buf.data(), r.buf.size(), off, nm, next)) {
            if (nm == name) return ((uint32_t)bi << 16) | off;
            off = next;
            if (++guard > 400000) break;
        }
    }
    return 0;
}

void forEachName(uintptr_t blocks, const std::function<bool(uint32_t, const std::string &)> &fn) {
    BlockReader r(blocks);
    for (uint16_t bi = 0; bi < kMaxBlocks; bi++) {
        if (!rdv<uintptr_t>(blocks + (uintptr_t)bi * 8)) break;
        if (!r.load(bi)) break;

        uint32_t off = 0, next = 0;
        int guard = 0;
        std::string nm;
        while (off + 2 <= kBlockSize && parseEntry(r.buf.data(), r.buf.size(), off, nm, next)) {
            uint32_t id = ((uint32_t)bi << 16) | off;
            if (!fn(id, nm)) return;
            off = next;
            if (++guard > 400000) break;
        }
    }
}

} // namespace zk
