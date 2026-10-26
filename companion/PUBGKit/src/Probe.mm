// Probe.mm - log + load offsets.plist + dump chẩn đoán
#include "Core.hpp"
#include "Offsets.hpp"
#include "Mem.hpp"
#import <Foundation/Foundation.h>
#import <mach-o/dyld.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <mutex>
#import <rootless.h>

static FILE* g_logf = NULL;
static char  g_workdir[256] = {0};
static char  g_logpath[320] = {0};

const char* workDir(void) {
    static std::once_flag once;
    std::call_once(once, [] {
        @autoreleasepool {
            NSString *base=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
            if (!base) base=NSTemporaryDirectory();
            NSString *dir=[base stringByAppendingPathComponent:@"PUBGKit"];
            NSError *error=nil;
            BOOL ok=[[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:&error];
            NSString *chosen=ok ? dir : NSTemporaryDirectory();
            snprintf(g_workdir,sizeof(g_workdir),"%s",chosen.fileSystemRepresentation);
        }
    });
    return g_workdir;
}

const char* offsetsPath(void) {
    static char path[512];
    static std::once_flag once;
    std::call_once(once, [] { snprintf(path,sizeof(path),"%s/offsets.plist",workDir()); });
    return path;
}

void zkLog(const char* fmt, ...) {
    static std::mutex logMutex;
    std::lock_guard<std::mutex> logLock(logMutex);
    if (!g_logf) {
        snprintf(g_logpath, sizeof(g_logpath), "%s/probe.log", workDir());
        g_logf = fopen(g_logpath, "a");
        if (!g_logf) return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_logf, fmt, ap);
    va_end(ap);
    fputc('\n', g_logf);
    fflush(g_logf);
}

// ---------------------------------------------------------------- offsets
static uint64_t num(NSDictionary* d, NSString* k, uint64_t def) {
    id v = d[k];
    if ([v isKindOfClass:[NSNumber class]]) return [v unsignedLongLongValue];
    if ([v isKindOfClass:[NSString class]]) {
        const char *text=[v UTF8String]; char *end=nullptr;
        errno=0; unsigned long long value=strtoull(text,&end,0);
        if (!errno && end!=text && *end=='\0' && text[0]!='-') return value;
    }
    return def;
}

bool loadOffsets() {
    std::lock_guard<std::recursive_mutex> runtimeLock(g_coreMutex);
    @autoreleasepool {
        NSDictionary* d = [NSDictionary dictionaryWithContentsOfFile:@(offsetsPath())];
        if (!d) d=[NSDictionary dictionaryWithContentsOfFile:ROOT_PATH_NS(@"/Library/Application Support/PUBGKit/offsets.plist")];
        if (!d) {
            zkLog("[off] khong doc duoc %s -> dung auto-detect", offsetsPath());
            g_off.loaded = false;
            return false;
        }
        g_off.GNames  = num(d, @"GNames", 0);
        g_off.GObjects = num(d, @"GObjects", 0);
        g_off.GEngine = num(d, @"GEngine", 0);

        g_off.className        = (int32_t)num(d, @"className", 0);
        g_off.objItemStride    = (int32_t)num(d, @"objItemStride", 0x18);
        g_off.nameChunkSize    = (int32_t)num(d, @"nameChunkSize", 16384);

        g_off.worldPersistentLevel = (int32_t)num(d, @"worldPersistentLevel", 0);
        g_off.levelActors          = (int32_t)num(d, @"levelActors", 0);
        g_off.controllerCameraMgr  = (int32_t)num(d, @"controllerCameraMgr", 0);
        g_off.cameraMgrCache       = (int32_t)num(d, @"cameraMgrCache", 0);
        g_off.controllerPawn       = (int32_t)num(d, @"controllerPawn", 0);

        g_off.actorRootComponent = (int32_t)num(d, @"actorRootComponent", 0);
        g_off.sceneLocation      = (int32_t)num(d, @"sceneLocation", 0);
        g_off.actorPlayerState   = (int32_t)num(d, @"actorPlayerState", 0);
        g_off.psTeam             = (int32_t)num(d, @"psTeam", 0);
        g_off.psName             = (int32_t)num(d, @"psName", 0);
        g_off.charHealth         = (int32_t)num(d, @"charHealth", 0);
        g_off.healthIsInt        = (int32_t)num(d, @"healthIsInt", 0);
        g_off.charMesh           = (int32_t)num(d, @"charMesh", 0);
        g_off.meshBoneArray      = (int32_t)num(d, @"meshBoneArray", 0);
        g_off.boneStride         = (int32_t)num(d, @"boneStride", 0x30);
        g_off.boneTransOff       = (int32_t)num(d, @"boneTransOff", 0x10);

        g_off.controllerControlRotation = (int32_t)num(d, @"controllerControlRotation", 0);
        g_off.loaded = true;

        zkLog("[off] loaded from %s", offsetsPath());
        return true;
    }
}

// ---------------------------------------------------------------- dump
static void dumpImages(FILE* f) {
    uint32_t cnt = _dyld_image_count();
    fprintf(f, "== IMAGES (%u) ==\n", cnt);
    for (uint32_t i = 0; i < cnt; i++) {
        const char* p = _dyld_get_image_name(i);
        uintptr_t b = (uintptr_t)_dyld_get_image_header(i);
        intptr_t sl = _dyld_get_image_vmaddr_slide(i);
        fprintf(f, "[%3u] base=%p slide=%p %s\n", i, (void*)b, (void*)sl, p ? p : "?");
    }
}

void probeLogDump(bool verbose) {
    std::lock_guard<std::recursive_mutex> runtimeLock(g_coreMutex);
    @autoreleasepool {
        NSMutableString* ms = [NSMutableString string];
        char line[512];

        // log vào cùng file để user chỉ cần gửi 1 file
        [ms appendString:@"\n===== PUBGKit PROBE =====\n"];
        snprintf(line, sizeof(line), "time=%s\n", [[[NSDate date] description] UTF8String]);
        [ms appendString:@(line)];
        coreInit(true);

        bool okN = (g_off.GNames != 0);
        bool okO = (g_off.GObjects != 0);
        snprintf(line, sizeof(line),
                 "GNames  address = 0x%llx %s\nGObjects address = 0x%llx %s\nclassName=0x%x stride=0x%x\n",
                 (unsigned long long)g_off.GNames, okN ? "(auto)" : "(MISS)",
                 (unsigned long long)g_off.GObjects, okO ? "(auto)" : "(MISS)",
                 g_off.className, g_off.objItemStride);
        [ms appendString:@(line)];

        int n = objCount();
        snprintf(line, sizeof(line), "\n== OBJECTS: %d ==\n", n);
        [ms appendString:@(line)];

        static const char* keys[] = { "World", "CameraManager", "PlayerController",
                                      "Character", "GameEngine", "PlayerState" };
        for (int k = 0; k < 6; k++) {
            snprintf(line, sizeof(line), "-- class contains \"%s\" --\n", keys[k]);
            [ms appendString:@(line)];
            int shown = 0;
            for (int i = 0; i < n && shown < 12; i++) {
                uintptr_t o = objAt(i);
                char nm[64];
                if (!o || !classNameOf(o, nm, sizeof(nm))) continue;
                if (!strstr(nm, keys[k])) continue;
                snprintf(line, sizeof(line), "  [%6d] %p %s\n", i, (void*)o, nm);
                [ms appendString:@(line)];
                shown++;
            }
        }

        // camera
        FMinimalViewInfo cam;
        bool haveCam = getCamera(cam);
        snprintf(line, sizeof(line),
                 "\n== CAMERA %s ==\nloc=%.1f %.1f %.1f\nrot=%.2f %.2f %.2f fov=%.1f\n",
                 haveCam ? "OK" : "MISS",
                 cam.Location.X, cam.Location.Y, cam.Location.Z,
                 cam.Rotation.Pitch, cam.Rotation.Yaw, cam.Rotation.Roll, cam.FOV);
        [ms appendString:@(line)];

        // actors
        std::vector<ActorInfo> list;
        bool haveActors = snapshotActors(list);
        snprintf(line, sizeof(line), "\n== ACTORS: %zu %s ==\n", list.size(),
                 haveActors ? "" : "(KHONG DOC DUOC - offset levelActors/root sai)");
        [ms appendString:@(line)];
        int shown = 0;
        for (size_t i = 0; i < list.size() && shown < 40; i++) {
            ActorInfo& a = list[i];
            snprintf(line, sizeof(line), "  %-42s d=%7.1fm hp=%6.1f team=%3d %s\n",
                     a.cls, a.dist, a.hp, a.team, a.isSelf ? "<SELF>" : "");
            [ms appendString:@(line)];
            shown++;
        }

        snprintf(line, sizeof(line),
                 "\n== DISCOVERED OFFSETS (dán vào offsets.plist) ==\n"
                 "worldPersistentLevel = 0x%x\nlevelActors = 0x%x\n"
                 "controllerCameraMgr = 0x%x\ncameraMgrCache = 0x%x\n"
                 "controllerPawn = 0x%x\nactorRootComponent = 0x%x\nsceneLocation = 0x%x\n"
                 "className = 0x%x\nobjItemStride = 0x%x\n"
                 "GNames = 0x%llx\nGObjects = 0x%llx\n",
                 g_off.worldPersistentLevel, g_off.levelActors,
                 g_off.controllerCameraMgr, g_off.cameraMgrCache,
                 g_off.controllerPawn, g_off.actorRootComponent, g_off.sceneLocation,
                 g_off.className, g_off.objItemStride,
                 (unsigned long long)g_off.GNames, (unsigned long long)g_off.GObjects);
        [ms appendString:@(line)];

        if (verbose) {
            NSString* path = [NSString stringWithFormat:@"%s/probe_detail.txt", workDir()];
            [ms writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];
        } else {
            NSString* path = [NSString stringWithFormat:@"%s/probe.txt", workDir()];
            [ms writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];
        }
        zkLog("[probe] written. GNames=%p GObjects=%p world=%p pc=%p cam=%d actors=%zu",
              (void*)g_off.GNames, (void*)g_off.GObjects, (void*)getWorld(),
              (void*)getLocalPC(), haveCam ? 1 : 0, list.size());

        // in ảnh images ra file riêng cho gọn
        NSString* ip = [NSString stringWithFormat:@"%s/images.txt", workDir()];
        FILE* f = fopen([ip UTF8String], "w");
        if (f) { dumpImages(f); fclose(f); }
    }
}
