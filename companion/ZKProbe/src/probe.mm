// probe.mm — entry point: do GNames / GObjects ngay trong tien trinh game
// Build -> PUBGKit.dylib -> inject (TrollFools / ESign / Theos)
// Chay: tu dong sau ~12s, hoac goi tu Frida:  zkProbe()

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include <mutex>

#include "fname.hpp"
#include "objects.hpp"

using namespace zk;

static FILE *gLog = NULL;
static std::mutex probeMutex;

static const char *logPath(void) {
    static char path[512];
    if (path[0]) return path;

    // uu tien /var/mobile/Documents (lay ra bang Filza / Files)
    const char *cands[] = {"/var/mobile/Documents/zkprobe.txt",
                           "/var/jb/var/mobile/Documents/zkprobe.txt", NULL};
    for (int i = 0; cands[i]; i++) {
        FILE *f = fopen(cands[i], "a");
        if (f) {
            snprintf(path, sizeof(path), "%s", cands[i]);
            fclose(f);
            return path;
        }
    }
    // fallback: Documents cua app
    NSString *dir = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *p = [dir stringByAppendingPathComponent:@"zkprobe.txt"];
    snprintf(path, sizeof(path), "%s", p.UTF8String);
    return path;
}

static void zkLog(const char *fmt, ...) {
    if (!gLog) gLog = fopen(logPath(), "a");
    if (!gLog) return;

    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    fputs(buf, gLog);
    fputc('\n', gLog);
    fflush(gLog);
    NSLog(@"[ZK] %s", buf);
}

static const char *kKeywords[] = {
    "World", "GameEngine", "PlayerController", "PlayerCameraManager",
    "PlayerState", "GameState", "Character", "Pawn", "Player", "LocalPlayer",
    "GameInstance", "PersistentLevel", "Level", "Actor", "Mesh",
    "SkeletalMeshComponent", "Controller", "CameraComponent", NULL
};

static bool matchKeyword(const std::string &s) {
    for (int i = 0; kKeywords[i]; i++)
        if (s.find(kKeywords[i]) != std::string::npos) return true;
    return false;
}

extern "C" void zkProbe(void) {
    std::unique_lock<std::mutex> lock(probeMutex,std::try_to_lock);
    if (!lock.owns_lock()) return;
    @autoreleasepool {
    refreshRegions();
    resetScanBudget();
    struct CloseLog { ~CloseLog() { if(gLog) { fclose(gLog); gLog=nullptr; } } } closeLog;
    gLog = fopen(logPath(), "a");

    uintptr_t base = mainBase();
    uintptr_t slide=0;
    for(uint32_t i=0;i<_dyld_image_count();i++)
        if((uintptr_t)_dyld_get_image_header(i)==base) { slide=_dyld_get_image_vmaddr_slide(i); break; }

    zkLog("\n================ ZK PROBE ================");
    zkLog("[env] log      = %s", logPath());
    zkLog("[env] mainBase = %p   slide = %p", (void *)base, (void *)slide);
    zkLog("[env] images   = %u", _dyld_image_count());
    zkLog("[env] main img = %s", imageNameContaining(base));

    if(!base) { zkLog("[error] executable image not found"); return; }
    zkLog("[model] EXPERIMENTAL: headerShift=1, idOffsetUnit=1, blockBytes=65536. NOT verified for PUBG 4.6.");
    // ---------------- GNames ----------------
    FNamePool pool = findFNamePool();
    if (!pool.found()) {
        zkLog("[names] >>> GNames NOT FOUND (thu lai sau khi vao lobby)");
    } else {
        zkLog("[names] blocks  = %p", (void *)pool.blocksAddr);
        zkLog("[names] rvaBlocks = 0x%llx   (FNamePool::Blocks = base + rva nay)",
              (unsigned long long)pool.rvaBlocks);
        zkLog("[names] rvaPool UNVERIFIED (do not use) = 0x%llx",
              (unsigned long long)pool.rvaPool);
        zkLog("[names] img of blocks = %s", imageNameContaining(pool.blocksAddr));

        // in cau truc quanh Blocks[] de doi chieu layout
        zkLog("[names] -- struct dump quanh Blocks[] --");
        for (int d = -0x20; d < 0x18; d += 4) {
            uintptr_t a = pool.blocksAddr + d;
            zkLog("[names]   %+03x @%p : %08x", d, (void *)a, rdv<uint32_t>(a));
        }

        int total = 0;
        forEachName(pool.blocksAddr, [&](uint32_t id, const std::string &nm) {
            if (total < 25) zkLog("[names]   #%u (0x%x) %s", id, id, nm.c_str());
            total++;
            return total < 3000000;
        });
        zkLog("[names] total names = %d", total);

        zkLog("[names] -- id cua cac ten quan trong --");
        for (int i = 0; kKeywords[i]; i++) {
            uint32_t id = findNameId(pool.blocksAddr, kKeywords[i]);
            zkLog("[names]   %-22s id=0x%08x %s", kKeywords[i], id, id ? "OK" : "NOT FOUND");
        }
    }

    // ---------------- GObjects ----------------
    UObjectArray obj;
    if (pool.found()) obj = findObjectArray(pool);

    if (!obj.found()) {
        zkLog("[objs] >>> GObjects NOT FOUND");
    } else {
        zkLog("[objs] array     = %p", (void *)obj.arrayAddr);
        zkLog("[objs] rvaArray  = 0x%llx", (unsigned long long)obj.rva);
        zkLog("[objs] objectsPtr= %p", (void *)obj.objectsPtr);
        zkLog("[objs] numElements = %d   maxElements = %d", obj.numElements, obj.maxElements);
        zkLog("[objs] numChunks = %d   itemSize = 0x%x", obj.numChunks, obj.itemSize);

        int shown = 0;
        for (uint32_t i = 1; i < (uint32_t)obj.numElements && shown < 260; i++) {
            uintptr_t o = objectAt(obj, i);
            if (!o) continue;
            std::string cls = objectClassName(pool, o);
            if (cls.empty()) continue;
            if (!matchKeyword(cls)) continue;

            std::string nm = objectName(pool, o);
            zkLog("[objs]   [%6u] %-32s %-40s %p", i,
                  cls.c_str(), nm.c_str(), (void *)o);
            shown++;
        }
        zkLog("[objs] printed %d instance", shown);
    }

    zkLog("[scan] bytes=%llu incomplete=%d",(unsigned long long)scanBudget().bytes,scanBudget().exhausted?1:0);
    // ---------------- ket luan ----------------
    zkLog("\n===== EXPERIMENTAL CANDIDATES - DO NOT COPY TO PLIST =====");
    if (pool.found() && belongsToMainImage(pool.blocksAddr)) {
        zkLog("GNAMES_BLOCKS_RVA = 0x%llx", (unsigned long long)pool.rvaBlocks);
        zkLog("resolve: entry = *(uint8**)(base + GNAMES_BLOCKS_RVA + (idx>>16)*8)");
        zkLog("         hdr   = *(uint16*)(entry + (idx & 0xFFFF))");
        zkLog("         len   = hdr >> 1 ; wide = hdr & 1");
    }
    if (obj.found() && belongsToMainImage(obj.arrayAddr))
        zkLog("GOBJECTS_RVA      = 0x%llx", (unsigned long long)obj.rva);
    zkLog("==============================================\n");
    }
}

__attribute__((constructor)) static void zkInit(void) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        // doi app init xong (canh bao: qua som thi pool chua tao)
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 12ull * NSEC_PER_SEC),
                       dispatch_get_global_queue(QOS_CLASS_UTILITY,0), ^{
            zkProbe();
        });
    });
}
