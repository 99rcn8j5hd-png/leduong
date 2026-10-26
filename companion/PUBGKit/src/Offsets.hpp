// Offsets.hpp - toàn bộ offset nằm ở đây, đọc từ plist để không phải build lại
#pragma once
#include <stdint.h>
#include <string>
#include <mutex>

struct Offsets {
    // ---- tĩnh (RVA trong image chứa UE4). 0 = auto-detect lúc runtime ----
    uint64_t GNames  = 0;
    uint64_t GObjects = 0;
    uint64_t GEngine = 0;

    // ---- UObject / FUObjectArray ----
    int32_t className     = 0;      // vị trí UClass* trong UObject (auto)
    int32_t objItemStride = 0x18;   // kích thước FUObjectItem
    int32_t objChunkSize  = 65536;  // ElementsPerChunk của ObjObjects
    int32_t nameChunkSize = 16384;  // ElementsPerChunk của GNames

    // ---- chain tới World / PlayerController ----
    int32_t engineGameViewport    = 0;
    int32_t gameViewportWorld     = 0;
    int32_t worldPersistentLevel  = 0;
    int32_t worldGameInstance     = 0;
    int32_t gameInstanceLocalPlayer = 0;
    int32_t localPlayerController = 0;
    int32_t controllerCameraMgr   = 0;
    int32_t cameraMgrCache        = 0;   // offset tới FMinimalViewInfo trong APlayerCameraManager
    int32_t cameraFovInCache      = 0x18;
    int32_t controllerIsLocal     = 0;
    int32_t controllerPawn        = 0;

    // ---- Actor / Character ----
    int32_t levelActors      = 0;
    int32_t actorRootComponent = 0;
    int32_t sceneLocation    = 0;
    int32_t actorPlayerState = 0;
    int32_t psTeam           = 0;
    int32_t psName           = 0;
    int32_t charHealth       = 0;   // 0 = chưa biết -> ESP sẽ bỏ qua HP
    int32_t healthIsInt      = 0;   // 0 float, 1 int32
    int32_t charMesh         = 0;
    int32_t meshBoneArray    = 0;
    int32_t boneStride       = 0x30;
    int32_t boneTransOff     = 0x10;

    // ---- Aim ----
    int32_t controllerControlRotation = 0;  // FRotator; 0 = chỉ tính, không ghi

    bool loaded = false;
};

struct Config {
    bool  esp        = false;
    bool  drawBox    = true;
    bool  drawName   = true;
    bool  drawDist   = true;
    bool  drawHealth = true;
    bool  drawSelf   = false;
    bool  aim        = false;
    bool  aimPredict = true;
    float aimFov     = 150.0f;  // bán kính FOV (px trên màn hình)
    float aimSmooth  = 8.0f;    // 1 = snap, 8-15 = mượt
    int   aimKeyAlways = 1;     // 1 = luôn bật khi toggle, 0 = chỉ khi bắn
    uint32_t pingMs  = 60;
    float  bulletSpeed = 900.0f;
};

extern Offsets g_off;
extern Config g_cfg;
extern std::mutex g_configMutex;
Config configSnapshot();

// đọc /var/jb/var/mobile/Documents/PUBGKit/offsets.plist (hoặc bản non-rootless)
bool loadOffsets();
const char* offsetsPath(void);
const char* workDir(void);
