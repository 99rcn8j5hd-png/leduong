// Core.cpp
// Bootstrap KHONG can offset tinh: do GNames -> GObjects -> UWorld -> PlayerController
// -> CameraManager -> CameraCache -> offsets Actor, tat ca bang validation runtime.
#include "Core.hpp"
#include "Mem.hpp"
#include "Offsets.hpp"
#import <mach-o/dyld.h>
#import <mach-o/loader.h>
#include <vector>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <functional>

Offsets g_off;
Config g_cfg;
std::recursive_mutex g_coreMutex;
std::mutex g_configMutex;
Config configSnapshot() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    return g_cfg;
}

struct Seg { uintptr_t start, end; };
struct Img { uintptr_t base; std::string path; std::vector<Seg> data; };
static std::vector<Img> g_imgs;

static int       g_nameOff    = -1;
static bool      g_wide       = false;
static uintptr_t g_nameChunks = 0;
static uintptr_t g_objField   = 0;
static uintptr_t g_objChunks  = 0;
static int       g_numObjects = 0;
static int       g_classOff   = 0;
static uintptr_t g_world = 0, g_pc = 0, g_camMgr = 0, g_selfPawn = 0;
static uintptr_t g_level = 0;
static int       g_actorCount = 0;
static bool      g_ready = false;

// ------------------------------------------------------------------ images
static void collectImages(void) {
    g_imgs.clear();
    uint32_t cnt = _dyld_image_count();
    for (uint32_t i = 0; i < cnt; i++) {
        const struct mach_header_64* h = (const struct mach_header_64*)_dyld_get_image_header(i);
        const char* path = _dyld_get_image_name(i);
        if (!h || !path) continue;
        if (h->magic != MH_MAGIC_64) continue;
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);

        Img img;
        img.base = (uintptr_t)h;
        img.path = path;

        const uint8_t* p = (const uint8_t*)h + sizeof(struct mach_header_64);
        for (uint32_t c = 0; c < h->ncmds; c++) {
            const struct load_command* lc = (const struct load_command*)p;
            if (lc->cmdsize == 0) break;
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64* sg = (const struct segment_command_64*)lc;
                std::string sn(sg->segname, strnlen(sg->segname, sizeof(sg->segname)));
                bool isData = (sn == "__DATA" || sn == "__DATA_CONST" ||
                               sn == "__DATA_DIRTY" || sn == "__OBJC" || sn == "__OBJC_CONST");
                if (isData && sg->vmsize > 0 && sg->vmsize < 0x60000000ULL) {
                    Seg s;
                    s.start = (uintptr_t)sg->vmaddr + (uintptr_t)slide;
                    s.end   = s.start + (uintptr_t)sg->vmsize;
                    img.data.push_back(s);
                }
            }
            p += lc->cmdsize;
        }
        g_imgs.push_back(img);
    }
}

static void scanDataWords(const std::function<bool(uintptr_t addr, uintptr_t value)>& cb,
                          bool preferUEOnly) {
    for (size_t k = 0; k < g_imgs.size(); k++) {
        Img& img = g_imgs[k];
        if (preferUEOnly && img.path.find("UE4") == std::string::npos) continue;
        for (size_t j = 0; j < img.data.size(); j++) {
            uintptr_t s = img.data[j].start, e = img.data[j].end;
            for (uintptr_t p = (s + 7) & ~(uintptr_t)7; p + 8 <= e; p += 8) {
                uintptr_t v = 0;
                if (!memRead(p, &v, 8)) continue;
                if (!looksLikePtr(v)) continue;
                if (cb(p, v)) return;
            }
        }
    }
}

// ------------------------------------------------------------------ FName
static bool entryAt(uintptr_t chunksBase, uint32_t idx, uintptr_t* out) {
    int cs = (g_off.nameChunkSize > 0) ? g_off.nameChunkSize : 16384;
    uintptr_t chunk = rdPtr(chunksBase + (uintptr_t)(idx / (uint32_t)cs) * 8);
    if (!looksLikePtr(chunk)) return false;
    uintptr_t e = rdPtr(chunk + (uintptr_t)(idx % (uint32_t)cs) * 8);
    if (!looksLikePtr(e)) return false;
    *out = e;
    return true;
}

static bool nameFromEntry(uintptr_t entry, char* out, size_t cap) {
    if (!out || cap == 0) return false;
    out[0] = 0;
    if (g_nameOff < 0 || !entry) return false;
    out[0] = 0;
    if (!g_wide) {
        size_t n = 0;
        char c = 0;
        while (n < cap - 1) {
            if (!memRead(entry + g_nameOff + n, &c, 1) || c == 0) break;
            if (!isprint((unsigned char)c)) return false;
            out[n++] = c;
        }
        out[n] = 0;
        return n > 0;
    }
    size_t n = 0;
    uint16_t c = 0;
    while (n < cap - 1) {
        if (!memRead(entry + g_nameOff + n * 2, &c, 2) || c == 0) break;
        if (c < 0x20 || c > 0x7E) return false;
        out[n++] = (char)c;
    }
    out[n] = 0;
    return n > 0;
}

static bool detectLayout(uintptr_t entry) {
    uint8_t buf[0x60];
    if (!memRead(entry, buf, sizeof(buf))) return false;
    for (int off = 0; off < 0x48; off++) {
        if (buf[off] == 'N' && buf[off + 1] == 'o' && buf[off + 2] == 'n' &&
            buf[off + 3] == 'e' && buf[off + 4] == 0) {
            g_nameOff = off; g_wide = false; return true;
        }
        if (buf[off] == 'N' && buf[off + 1] == 0 && buf[off + 2] == 'o' && buf[off + 3] == 0 &&
            buf[off + 4] == 'n' && buf[off + 5] == 0 && buf[off + 6] == 'e') {
            g_nameOff = off; g_wide = true; return true;
        }
    }
    return false;
}

static bool tryGNames(uintptr_t chunksBase) {
    uintptr_t e0 = 0;
    if (!entryAt(chunksBase, 0, &e0)) return false;
    int saveOff = g_nameOff;
    bool saveWide = g_wide;
    if (g_nameOff < 0 && !detectLayout(e0)) return false;
    char nm[80];
    if (!nameFromEntry(e0, nm, sizeof(nm)) || strcmp(nm, "None") != 0) {
        g_nameOff = saveOff; g_wide = saveWide;
        return false;
    }
    int ok = 0;
    for (int i = 1; i <= 12; i++) {
        uintptr_t e = 0;
        if (!entryAt(chunksBase, (uint32_t)i, &e)) break;
        char s[80];
        if (nameFromEntry(e, s, sizeof(s)) && s[0] &&
            (isalpha((unsigned char)s[0]) || s[0] == '_')) ok++;
    }
    if (ok < 7) { g_nameOff=saveOff; g_wide=saveWide; return false; }
    return true;
}

static bool discoverGNames(void) {
    bool found = false;
    scanDataWords([&](uintptr_t addr, uintptr_t v) -> bool {
        if (tryGNames(v)) { g_nameChunks = v; g_off.GNames = addr; found = true; return true; }
        uintptr_t v2 = rdPtr(v);
        if (looksLikePtr(v2) && tryGNames(v2)) {
            g_nameChunks = v2; g_off.GNames = addr; found = true; return true;
        }
        return false;
    }, false);
    return found;
}

static bool nameOfIndex(int32_t idx, char* out, size_t cap) {
    if (idx <= 0 || idx > 8000000) return false;
    uintptr_t e = 0;
    if (!entryAt(g_nameChunks, (uint32_t)idx, &e)) return false;
    return nameFromEntry(e, out, cap);
}

// ------------------------------------------------------------------ GObjects
static bool isKnownClass(const char* n) {
    static const char* k[] = {
        "Object", "Class", "Field", "Struct", "Function", "Package", "ScriptStruct",
        "World", "Actor", "Component", "SceneComponent", "PlayerController",
        "GameEngine", "IntProperty", "ByteProperty", "BoolProperty", "FloatProperty",
        "ObjectProperty", "NameProperty", "EnumProperty", "StrProperty", "Property",
        "UInt32Property", "Interface", "State", "TextProperty", "ArrayProperty"
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++)
        if (strcmp(n, k[i]) == 0) return true;
    return false;
}

static bool classIdxName(uintptr_t obj, int cOff, char* out, size_t cap) {
    if (!memValid(obj, 0x40)) return false;
    int32_t idx = rd<int32_t>(obj + cOff, -1);
    return nameOfIndex(idx, out, cap);
}

static bool tryObjects(uintptr_t fieldAddr, uintptr_t chunks) {
    static const int strides[] = { 0x18, 0x10, 0x20 };
    static const int coffs[]   = { 0x10, 0x08, 0x18, 0x20 };

    uintptr_t o0 = rdPtr(chunks);
    if (!looksLikePtr(o0)) return false;

    for (int si = 0; si < 3; si++) {
        int stride = strides[si];
        uintptr_t o1 = rdPtr(chunks + stride);
        if (!looksLikePtr(o1)) continue;
        for (int ci = 0; ci < 4; ci++) {
            int cOff = coffs[ci];
            char a[64], b[64];
            if (!classIdxName(o0, cOff, a, sizeof(a))) continue;
            if (!classIdxName(o1, cOff, b, sizeof(b))) continue;
            if (!isKnownClass(a) && !isKnownClass(b)) continue;

            int score = 0;
            for (int i = 0; i < 24; i++) {
                uintptr_t o = rdPtr(chunks + (uintptr_t)(i * stride));
                char nm[64];
                if (looksLikePtr(o) && classIdxName(o, cOff, nm, sizeof(nm))) score++;
            }
            if (score < 10) continue;

            int32_t num  = rd<int32_t>(fieldAddr + 0x14, 0);
            int32_t maxe = rd<int32_t>(fieldAddr + 0x10, 0);
            if (num < 1000 || num > 9000000 || maxe < num || maxe > 20000000) num = 0;

            g_objField   = fieldAddr;
            g_objChunks  = chunks;
            g_classOff   = cOff;
            g_numObjects = num;
            g_off.className     = cOff;
            g_off.objItemStride = stride;
            return true;
        }
    }
    return false;
}

static bool discoverGObjects(void) {
    bool found = false;
    scanDataWords([&](uintptr_t addr, uintptr_t v) -> bool {
        if (tryObjects(addr, v)) { found = true; return true; }
        return false;
    }, false);
    return found;
}

int objCount(void) {
    std::lock_guard<std::recursive_mutex> runtimeLock(g_coreMutex);
    if (g_numObjects > 0) return g_numObjects;
    int i = 0;
    while (i < 4000000) {
        uintptr_t o = 0;
        uintptr_t chunk = rdPtr(g_objChunks + (uintptr_t)(i / 65536) * 8);
        if (!looksLikePtr(chunk)) break;
        o = rdPtr(chunk + (uintptr_t)(i % 65536) * (uintptr_t)g_off.objItemStride);
        if (!looksLikePtr(o)) break;
        i++;
    }
    g_numObjects = i;
    return i;
}

uintptr_t objAt(int i) {
    std::lock_guard<std::recursive_mutex> runtimeLock(g_coreMutex);
    if (!g_objChunks || i < 0) return 0;
    uintptr_t chunk = rdPtr(g_objChunks + (uintptr_t)(i / 65536) * 8);
    if (!looksLikePtr(chunk)) return 0;
    return rdPtr(chunk + (uintptr_t)(i % 65536) * (uintptr_t)g_off.objItemStride);
}

bool classNameOf(uintptr_t obj, char* out, size_t cap) {
    std::lock_guard<std::recursive_mutex> runtimeLock(g_coreMutex);
    if (!out || cap == 0) return false;
    out[0] = 0;
    if (!obj || !g_classOff) return false;
    return classIdxName(obj, g_classOff, out, cap);
}

uintptr_t findObjectByClassContains(const char* needle, int nth) {
    std::lock_guard<std::recursive_mutex> runtimeLock(g_coreMutex);
    int n = objCount();
    int hit = 0;
    for (int i = 0; i < n; i++) {
        uintptr_t o = objAt(i);
        char nm[64];
        if (!o || !classNameOf(o, nm, sizeof(nm))) continue;
        if (!strstr(nm, needle)) continue;
        if (hit++ >= nth) return o;
    }
    return 0;
}

// ------------------------------------------------------------------ World
static bool validateWorld(uintptr_t w) {
    for (int lvOff = 0x18; lvOff <= 0x68; lvOff += 4) {
        uintptr_t lev = rdPtr(w + lvOff);
        if (!looksLikePtr(lev)) continue;
        for (int aOff = 0x20; aOff <= 0x140; aOff += 8) {
            uintptr_t data = rdPtr(lev + aOff);
            int32_t cnt = rd<int32_t>(lev + aOff + 8, 0);
            int32_t mx  = rd<int32_t>(lev + aOff + 0xC, 0);
            if (!looksLikePtr(data) || cnt < 10 || cnt > 500000 || mx < cnt || mx > 3000000) continue;
            int ok = 0;
            for (int i = 0; i < 10; i++) {
                uintptr_t a = rdPtr(data + (uintptr_t)i * 8);
                char nm[64];
                if (looksLikePtr(a) && classNameOf(a, nm, sizeof(nm))) ok++;
            }
            if (ok >= 8) {
                g_off.worldPersistentLevel = lvOff;
                g_off.levelActors = aOff;
                g_level = lev;
                g_actorCount = cnt;
                return true;
            }
        }
    }
    return false;
}

static bool findWorld(void) {
    int n = objCount();
    for (int i = 0; i < n; i++) {
        uintptr_t o = objAt(i);
        char nm[64];
        if (!o || !classNameOf(o, nm, sizeof(nm))) continue;
        if (strcmp(nm, "World") != 0) continue;
        if (validateWorld(o)) { g_world = o; return true; }
    }
    return false;
}

// ------------------------------------------------------------------ Camera
static bool plausibleView(uintptr_t addr, FMinimalViewInfo& v, float& fov) {
    FVector loc = rd<FVector>(addr);
    FRotator rot = rd<FRotator>(addr + 0xC);
    fov = rd<float>(addr + 0x18, 0.0f);
    if (!loc.Finite() || loc.NearZero()) return false;
    if (fabsf(loc.X) > 3.0e6f || fabsf(loc.Y) > 3.0e6f) return false;
    if (loc.Z < -30000.f || loc.Z > 100000.f) return false;
    if (fabsf(rot.Pitch) > 89.99f) return false;
    if (fabsf(rot.Yaw) > 720.f || fabsf(rot.Roll) > 45.f) return false;
    if (fov < 40.f || fov > 135.f) return false;
    v.Location = loc;
    v.Rotation = rot;
    v.FOV = fov;
    return true;
}

static bool discoverCameraCache(uintptr_t mgr) {
    FMinimalViewInfo v;
    float fov = 0;
    for (int off = 0x80; off <= 0x2000; off += 8) {
        if (plausibleView(mgr + off, v, fov)) {
            g_off.cameraMgrCache = off;
            g_off.cameraFovInCache = 0x18;
            return true;
        }
    }
    return false;
}

static bool discoverCameraMgr(uintptr_t pc) {
    for (int off = 0x80; off <= 0x900; off += 8) {
        uintptr_t m = rdPtr(pc + off);
        if (!looksLikePtr(m)) continue;
        char nm[64];
        if (!classNameOf(m, nm, sizeof(nm))) continue;
        if (!strstr(nm, "CameraManager")) continue;
        if (discoverCameraCache(m) || g_off.cameraMgrCache) {
            g_off.controllerCameraMgr = off;
            g_camMgr = m;
            return true;
        }
    }
    return false;
}

bool getCamera(FMinimalViewInfo& out) {
    std::lock_guard<std::recursive_mutex> runtimeLock(g_coreMutex);
    if (!g_off.cameraMgrCache) return false;
    if (!looksLikePtr(g_camMgr)) {
        g_camMgr = g_pc ? rdPtr(g_pc + g_off.controllerCameraMgr) : 0;
        if (!looksLikePtr(g_camMgr)) return false;
    }
    uintptr_t a = g_camMgr + g_off.cameraMgrCache;
    out.Location = rd<FVector>(a);
    out.Rotation = rd<FRotator>(a + 0xC);
    out.FOV      = rd<float>(a + 0x18, 90.0f);
    return out.Location.Finite() && !out.Location.NearZero() &&
        isfinite(out.Rotation.Pitch) && isfinite(out.Rotation.Yaw) &&
        isfinite(out.Rotation.Roll) && isfinite(out.FOV) && out.FOV > 5 && out.FOV < 170;
}

// ------------------------------------------------------------------ PC / Pawn / root
static bool discoverPawn(uintptr_t pc) {
    FMinimalViewInfo cam;
    bool haveCam = getCamera(cam);
    for (int off = 0x100; off <= 0x900; off += 8) {
        uintptr_t p = rdPtr(pc + off);
        if (!looksLikePtr(p)) continue;
        char nm[64];
        if (!classNameOf(p, nm, sizeof(nm))) continue;
        if (!strstr(nm, "Character") && !strstr(nm, "Pawn")) continue;
        if (haveCam) {
            uintptr_t root = rdPtr(p + g_off.actorRootComponent);
            if (looksLikePtr(root)) {
                FVector loc = rd<FVector>(root + g_off.sceneLocation);
                if (loc.Finite() && (loc - cam.Location).Size() < 1200.f) {
                    g_off.controllerPawn = off;
                    g_selfPawn = p;
                    return true;
                }
            }
        }
        g_off.controllerPawn = off;
        g_selfPawn = p;
        return true;
    }
    return false;
}

static bool discoverRootComponent(void) {
    if (!g_selfPawn) return false;
    for (int off = 0; off <= 0x600; off += 8) {
        uintptr_t root = rdPtr(g_selfPawn + off);
        if (!looksLikePtr(root)) continue;
        char nm[64];
        if (!classNameOf(root, nm, sizeof(nm))) continue;
        if (!strstr(nm, "Component")) continue;
        for (int lo = 0x80; lo <= 0x300; lo += 4) {
            FVector v = rd<FVector>(root + lo);
            if (!v.Finite() || v.NearZero()) continue;
            if (fabsf(v.X) > 3.0e6f || fabsf(v.Z) > 100000.f) continue;
            if (fabsf(v.Z) < 1.0f) continue;
            g_off.actorRootComponent = off;
            g_off.sceneLocation = lo;
            return true;
        }
    }
    return false;
}

static bool pickController(void) {
    int n = objCount();
    uintptr_t fallback = 0;
    for (int i = 0; i < n; i++) {
        uintptr_t o = objAt(i);
        char nm[64];
        if (!o || !classNameOf(o, nm, sizeof(nm))) continue;
        if (!strstr(nm, "PlayerController")) continue;
        if (discoverCameraMgr(o)) {
            g_pc = o;
            discoverPawn(o);
            return true;
        }
        if (!fallback) fallback = o;
    }
    if (fallback) { g_pc = fallback; return true; }
    return false;
}

uintptr_t getWorld()     {
    std::lock_guard<std::recursive_mutex> runtimeLock(g_coreMutex); return g_world; }
uintptr_t getSelfPawn()  {
    std::lock_guard<std::recursive_mutex> runtimeLock(g_coreMutex); return g_selfPawn; }
uintptr_t getLocalPC()   {
    std::lock_guard<std::recursive_mutex> runtimeLock(g_coreMutex); return g_pc; }
uintptr_t getCameraMgr() {
    std::lock_guard<std::recursive_mutex> runtimeLock(g_coreMutex); return g_camMgr; }
bool coreReady()         {
    std::lock_guard<std::recursive_mutex> runtimeLock(g_coreMutex); return g_ready; }

// ------------------------------------------------------------------ snapshot
bool snapshotActors(std::vector<ActorInfo>& out) {
    std::lock_guard<std::recursive_mutex> runtimeLock(g_coreMutex);
    out.clear();
    if (!g_level || g_actorCount <= 0 || !g_off.levelActors) return false;
    uintptr_t data = rdPtr(g_level + g_off.levelActors);
    if (!looksLikePtr(data)) return false;

    FMinimalViewInfo cam;
    bool haveCam = getCamera(cam);

    int32_t currentCount = rd<int32_t>(g_level + g_off.levelActors + 8, 0);
    int32_t currentMax = rd<int32_t>(g_level + g_off.levelActors + 12, 0);
    if (currentCount <= 0 || currentCount > 500000 || currentMax < currentCount) return false;
    int n = currentCount > 4000 ? 4000 : currentCount;
    out.reserve((size_t)n);

    for (int i = 0; i < n; i++) {
        uintptr_t a = rdPtr(data + (uintptr_t)i * 8);
        if (!looksLikePtr(a)) continue;

        ActorInfo ai;
        ai.addr = a;
        if (!classNameOf(a, ai.cls, sizeof(ai.cls))) continue;
        if (!strstr(ai.cls, "Character") && !strstr(ai.cls, "Pawn") &&
            !strstr(ai.cls, "Player")) continue;

        uintptr_t root = rdPtr(a + g_off.actorRootComponent);
        if (looksLikePtr(root)) ai.loc = rd<FVector>(root + g_off.sceneLocation);
        if (!ai.loc.Finite() || ai.loc.NearZero()) continue;

        if (haveCam) ai.dist = (ai.loc - cam.Location).Size() * 0.01f;
        ai.isSelf = (a == g_selfPawn);

        if (g_off.actorPlayerState) {
            uintptr_t ps = rdPtr(a + g_off.actorPlayerState);
            if (looksLikePtr(ps) && g_off.psTeam) ai.team = rd<int32_t>(ps + g_off.psTeam, -1);
        }
        ai.healthKnown = g_off.charHealth != 0;
        if (g_off.charHealth) {
            ai.hp = g_off.healthIsInt
                  ? (float)rd<int32_t>(a + g_off.charHealth, 0)
                  : rd<float>(a + g_off.charHealth, 0.0f);
        }
        ai.isChar = true;
        out.push_back(ai);
    }
    return !out.empty();
}

// ------------------------------------------------------------------ init
bool coreInit(bool force) {
    std::lock_guard<std::recursive_mutex> runtimeLock(g_coreMutex);
    if (g_ready && !force) return true;

    static bool haveConfigured = false;
    static Offsets configured;
    if (!haveConfigured) { configured=g_off; haveConfigured=true; }
    if (force) {
        g_off=configured;
        g_nameOff=-1; g_wide=false; g_nameChunks=0;
        g_objField=0; g_objChunks=0; g_numObjects=0; g_classOff=0;
        g_world=0; g_pc=0; g_camMgr=0; g_selfPawn=0; g_level=0; g_actorCount=0;
    }
    g_ready=false;
    collectImages();

    // GNames
    if (g_off.GNames) {
        uintptr_t v = rdPtr((uintptr_t)g_off.GNames);
        if (looksLikePtr(v) && tryGNames(v)) { g_nameChunks = v; }
        else if (tryGNames((uintptr_t)g_off.GNames)) { g_nameChunks = (uintptr_t)g_off.GNames; }
    }
    if (!g_nameChunks && !discoverGNames()) {
        zkLog("[core] GNames NOT FOUND");
        return false;
    }
    zkLog("[core] GNames chunks=%p nameOff=0x%x wide=%d", (void*)g_nameChunks, g_nameOff, g_wide ? 1 : 0);

    // GObjects
    if (g_off.GObjects) {
        uintptr_t field = (uintptr_t)g_off.GObjects;
        uintptr_t v = rdPtr(field);
        if (looksLikePtr(v) && tryObjects(field, v)) { /* ok */ }
    }
    if (!g_objChunks && !discoverGObjects()) {
        zkLog("[core] GObjects NOT FOUND");
        return false;
    }
    zkLog("[core] GObjects field=%p chunks=%p classOff=0x%x stride=0x%x num=%d",
          (void*)g_objField, (void*)g_objChunks, g_classOff, g_off.objItemStride, objCount());

    if (!g_world && !findWorld()) zkLog("[core] UWorld NOT FOUND (se van thu tiep)");
    if (g_world && !g_off.levelActors) validateWorld(g_world);

    if (!pickController()) zkLog("[core] PlayerController NOT FOUND");
    if (!g_off.actorRootComponent) discoverRootComponent();

    zkLog("[core] world=%p level=%p pc=%p camMgr=%p pawn=%p actors=%d",
          (void*)g_world, (void*)g_level, (void*)g_pc, (void*)g_camMgr,
          (void*)g_selfPawn, g_actorCount);
    zkLog("[core] offsets: lv=0x%x actors=0x%x camMgrOff=0x%x camCache=0x%x root=0x%x loc=0x%x pawnOff=0x%x",
          g_off.worldPersistentLevel, g_off.levelActors, g_off.controllerCameraMgr,
          g_off.cameraMgrCache, g_off.actorRootComponent, g_off.sceneLocation,
          g_off.controllerPawn);

    FMinimalViewInfo validatedCamera;
    g_ready = (g_nameChunks && g_objChunks && g_world && g_level && g_pc && getCamera(validatedCamera));
    return g_ready;
}
