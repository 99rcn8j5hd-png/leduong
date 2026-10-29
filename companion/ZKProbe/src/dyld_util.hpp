// dyld_util.hpp — doc memory an toan + liet ke vung nho + base/range cua main image
#pragma once

#include <mach/mach.h>
#include <mach/vm_region.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

namespace zk {

// Shared budget for the complete diagnostic run; partial scans are not conclusive.
struct ScanBudget {
    size_t bytes=0;
    bool exhausted=false;
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
};
inline ScanBudget& scanBudget() { static ScanBudget state; return state; }
inline void resetScanBudget() { scanBudget()=ScanBudget{}; }
inline bool allowScan(size_t bytes) {
    auto &b=scanBudget();
    if(b.exhausted) return false;
    if(bytes>size_t(2ull<<30)-b.bytes ||
       std::chrono::steady_clock::now()-b.start>std::chrono::seconds(90)) {
        b.exhausted=true;
        fprintf(stderr,"[ZKProbe] scan budget exhausted; result INCOMPLETE\n");
        return false;
    }
    b.bytes+=bytes; return true;
}

// ---------- read an toan (khong crash khi dia chi rac) ----------
inline bool rd(uintptr_t addr, void *out, size_t n) {
    if (!addr || !out || !n || n > UINTPTR_MAX-addr) return false;
    vm_size_t got = 0;
    kern_return_t kr = vm_read_overwrite(mach_task_self(), (vm_address_t)addr,
                                         (vm_size_t)n, (vm_address_t)out, &got);
    return kr == KERN_SUCCESS && got == n;
}

template <class T> inline T rdv(uintptr_t addr) {
    T v;
    memset(&v, 0, sizeof(T));
    rd(addr, &v, sizeof(T));
    return v;
}

// ---------- vung nho ----------
struct Region {
    uintptr_t start;
    uintptr_t end;
    bool writable;
};

inline std::vector<Region>& regionCache() { static std::vector<Region> cache; return cache; }
inline void refreshRegions() { regionCache().clear(); }
inline const std::vector<Region> &allRegions() {
    auto &cache=regionCache();
    if (!cache.empty()) return cache;

    vm_address_t addr = 0;
    while (true) {
        vm_size_t size = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t cnt = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t obj = MACH_PORT_NULL;

        kern_return_t kr = vm_region_64(mach_task_self(), &addr, &size,
                                        VM_REGION_BASIC_INFO_64,
                                        (vm_region_info_t)&info, &cnt, &obj);
        if (obj != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(),obj);
        if (kr != KERN_SUCCESS || size == 0 || size > UINTPTR_MAX-addr) break;

        if (info.protection & VM_PROT_READ) {
            Region r;
            r.start = (uintptr_t)addr;
            r.end = (uintptr_t)(addr + size);
            r.writable = (info.protection & VM_PROT_WRITE) != 0;
            cache.push_back(r);
        }
        addr += size;
        if (cache.size() > 8000) break;
    }
    std::sort(cache.begin(), cache.end(),
              [](const Region &a, const Region &b) { return a.start < b.start; });
    return cache;
}

inline bool inReadableRegion(uintptr_t a) {
    const std::vector<Region> &rs = allRegions();
    size_t lo = 0, hi = rs.size();
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (rs[mid].end <= a) lo = mid + 1;
        else hi = mid;
    }
    return lo < rs.size() && rs[lo].start <= a;
}

// ---------- range that cua tung image ----------
struct ImgRange {
    uintptr_t base = 0;
    uintptr_t end = 0;
    const char *path = nullptr;
};

inline bool imageRange(uint32_t idx, ImgRange &rg) {
    const struct mach_header_64 *mh =
        (const struct mach_header_64 *)_dyld_get_image_header(idx);
    if (!mh || mh->magic != MH_MAGIC_64) return false;

    intptr_t slide = _dyld_get_image_vmaddr_slide(idx);
    uintptr_t minA = UINTPTR_MAX, maxA = 0;

    const uint8_t *p = (const uint8_t *)mh + sizeof(struct mach_header_64);
    for (uint32_t c = 0; c < mh->ncmds; c++) {
        const struct load_command *lc = (const struct load_command *)p;
        if (lc->cmdsize < sizeof(struct load_command)) break;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            if (sg->initprot == VM_PROT_NONE || sg->filesize==0) { p += lc->cmdsize; continue; }
            uintptr_t s = (uintptr_t)slide + (uintptr_t)sg->vmaddr;
            uintptr_t e = s + (uintptr_t)sg->vmsize;
            if (s < minA) minA = s;
            if (e > maxA) maxA = e;
        }
        p += lc->cmdsize;
    }
    if (maxA <= minA) return false;
    rg.base = (uintptr_t)mh;
    rg.end = maxA;
    rg.path = _dyld_get_image_name(idx);
    return true;
}

inline bool imageRangeContaining(uintptr_t addr, ImgRange &out) {
    for (uint32_t i=0;i<_dyld_image_count();i++) {
        const auto *mh=(const mach_header_64 *)_dyld_get_image_header(i);
        if (!mh || mh->magic!=MH_MAGIC_64) continue;
        const uint8_t *p=(const uint8_t *)mh+sizeof(*mh);
        const uint8_t *end=p+mh->sizeofcmds;
        intptr_t slide=_dyld_get_image_vmaddr_slide(i);
        for (uint32_t c=0;c<mh->ncmds && p+sizeof(load_command)<=end;c++) {
            const auto *lc=(const load_command *)p;
            if (lc->cmdsize<sizeof(*lc) || (size_t)(end-p)<lc->cmdsize) break;
            if (lc->cmd==LC_SEGMENT_64 && lc->cmdsize>=sizeof(segment_command_64)) {
                const auto *sg=(const segment_command_64 *)p;
                if (sg->initprot!=VM_PROT_NONE && sg->vmsize) {
                    uintptr_t start=(uintptr_t)sg->vmaddr+slide;
                    if (addr>=start && addr-start<sg->vmsize) {
                        out.base=(uintptr_t)mh; out.end=start+sg->vmsize;
                        out.path=_dyld_get_image_name(i); return true;
                    }
                }
            }
            p+=lc->cmdsize;
        }
    }
    return false;
}

// ---------- base cua main executable (ShadowTrackerExtra) ----------
inline uintptr_t mainBase() {
    for (uint32_t i=0;i<_dyld_image_count();i++) {
        const auto *mh=_dyld_get_image_header(i);
        if (mh && mh->filetype==MH_EXECUTE) return (uintptr_t)mh;
    }
    return 0;
}

inline const char *imageNameContaining(uintptr_t addr) {
    ImgRange r;
    if (imageRangeContaining(addr, r) && r.path) return r.path;
    return "heap/other";
}

inline bool belongsToMainImage(uintptr_t addr) {
    ImgRange r;
    if (!imageRangeContaining(addr, r)) return false;
    return r.base == mainBase();
}

// ---------- quet vung RW theo tung khoi (tranh cap phat GB) ----------
const size_t kScanStep = 256u << 10; // 256 KB per read; bounded buffers

template <class F> void scanWritable(F cb) {
    const std::vector<Region> &regs = allRegions();
    std::vector<unsigned char> buf(kScanStep + 16);
    for (size_t ri = 0; ri < regs.size(); ri++) {
        const Region &r = regs[ri];
        if (!r.writable) continue;
        for (uintptr_t a = r.start; a < r.end; a += kScanStep) {
            size_t want = (size_t)std::min<uintptr_t>(kScanStep, r.end - a);
            if (want < 8) break;
            if (!allowScan(want)) return;
            if (!rd(a, buf.data(), want)) continue;
            cb(a, buf.data(), want);
        }
    }
}

// quet chi trong cac vung writable cua mot image (vd: __DATA cua main binary)
template <class F> void scanImageData(uintptr_t imgBase, F cb) {
    const std::vector<Region> &regs = allRegions();
    std::vector<unsigned char> buf(kScanStep + 16);
    for (size_t ri = 0; ri < regs.size(); ri++) {
        const Region &r = regs[ri];
        if (!r.writable) continue;

        ImgRange ir;
        if (!imageRangeContaining(r.start, ir) || ir.base != imgBase) continue;

        for (uintptr_t a = r.start; a < r.end; a += kScanStep) {
            size_t want = (size_t)std::min<uintptr_t>(kScanStep, r.end - a);
            if (want < 8) break;
            if (!allowScan(want)) return;
            if (!rd(a, buf.data(), want)) continue;
            cb(a, buf.data(), want);
        }
    }
}

} // namespace zk
