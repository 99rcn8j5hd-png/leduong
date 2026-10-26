// Core.hpp
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <mutex>
extern std::recursive_mutex g_coreMutex;
#include "UE.hpp"

struct ActorInfo {
    uintptr_t addr  = 0;
    char      cls[64] = {0};
    FVector   loc;
    float     dist  = 0;
    float     hp    = 0;
    int       team  = -1;
    bool      isSelf = false;
    bool      isChar = false;
    bool      healthKnown = false;
};

// Dò toàn bộ: GNames -> GObjects -> World -> PlayerController -> Camera -> offsets actor
bool coreInit(bool force = false);
bool coreReady();

uintptr_t getWorld();
uintptr_t getSelfPawn();
uintptr_t getLocalPC();
uintptr_t getCameraMgr();
int  objCount();
uintptr_t objAt(int i);
bool classNameOf(uintptr_t obj, char* out, size_t cap);
uintptr_t findObjectByClassContains(const char* needle, int nth = 0);

bool getCamera(FMinimalViewInfo& out);
bool snapshotActors(std::vector<ActorInfo>& out);

// log dùng chung (Probe.mm định nghĩa)
void zkLog(const char* fmt, ...);
void probeLogDump(bool verbose);
