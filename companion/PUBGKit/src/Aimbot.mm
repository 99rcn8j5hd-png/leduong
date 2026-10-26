// Aimbot.mm - chọn mục tiêu theo pixel gần tâm, smoothing + bù đạn rơi
#import <UIKit/UIKit.h>
#include "Core.hpp"
#include "Offsets.hpp"
#include "Mem.hpp"
#include <vector>
#include <math.h>
#include <unistd.h>
#include <atomic>

static std::atomic<bool> g_aimRun{false};
static uintptr_t g_lastTarget = 0;

static void aimLoop(void) {
    while (g_aimRun) {
        @autoreleasepool {
        const Config cfg=configSnapshot();
        if (!cfg.aim) { usleep(60000); continue; }
        __block CGSize scr;
        dispatch_sync(dispatch_get_main_queue(), ^{ scr=[UIScreen mainScreen].bounds.size; });
        std::unique_lock<std::recursive_mutex> runtimeLock(g_coreMutex);
        if (!cfg.aim || !coreReady()) { runtimeLock.unlock(); usleep(60000); continue; }

        FMinimalViewInfo cam;
        if (!getCamera(cam)) { runtimeLock.unlock(); usleep(60000); continue; }

        std::vector<ActorInfo> list;
        if (!snapshotActors(list)) { runtimeLock.unlock(); usleep(60000); continue; }

        int selfTeam = -1;
        uintptr_t selfAddr = 0;
        for (size_t i = 0; i < list.size(); i++) {
            if (list[i].isSelf) { selfTeam = list[i].team; selfAddr = list[i].addr; break; }
        }

        float bestD = cfg.aimFov > 5.f ? cfg.aimFov : 150.f;
        ActorInfo best;
        bool found = false;

        for (size_t i = 0; i < list.size(); i++) {
            ActorInfo& a = list[i];
            if (a.isSelf || a.addr == selfAddr) continue;
            if (selfTeam >= 0 && a.team == selfTeam) continue;
            if (a.dist > 600.f) continue;

            FVector2D sp;
            if (!worldToScreen(a.loc, cam, scr.width, scr.height, sp)) continue;
            float dx = sp.X - scr.width * 0.5f;
            float dy = sp.Y - scr.height * 0.5f;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < bestD) { bestD = d; best = a; found = true; }
        }

        if (!found) { runtimeLock.unlock(); usleep(25000); continue; }

        FVector aimPt = best.loc;
        aimPt.Z += 55.0f;                                  // ngực
        if (cfg.aimPredict)
            aimPt = zkPredict(cam.Location, aimPt, cfg.bulletSpeed, cfg.pingMs);

        FRotator want  = zkCalcAngle(cam.Location, aimPt);
        FRotator delta = zkAngleDelta(cam.Rotation, want);
        float sm = cfg.aimSmooth < 1.0f ? 1.0f : cfg.aimSmooth;

        if (g_off.controllerControlRotation) {
            uintptr_t pc = getLocalPC();
            if (pc) {
                FRotator now;
                if (memRead(pc + g_off.controllerControlRotation, &now, sizeof(now))) {
                    FRotator step;
                    step.Pitch = (want.Pitch - now.Pitch) / sm;
                    step.Yaw   = zkNormYaw(want.Yaw - now.Yaw) / sm;
                    step.Roll  = 0.0f;
                    FRotator out;
                    out.Pitch = now.Pitch + step.Pitch;
                    out.Yaw   = zkNormYaw(now.Yaw + step.Yaw);
                    out.Roll  = 0.0f;
                    if (out.Pitch > 89.0f)  out.Pitch = 89.0f;
                    if (out.Pitch < -89.0f) out.Pitch = -89.0f;
                    wr<FRotator>(pc + g_off.controllerControlRotation, out);
                }
            }
        } else {
            // chưa có offset ControlRotation -> chỉ log mục tiêu để bạn verify
            if (g_lastTarget != best.addr) {
                g_lastTarget = best.addr;
                zkLog("[aim] target=%p %s d=%.0fm deltaP=%.2f deltaY=%.2f (chua co ControlRotation)",
                      (void*)best.addr, best.cls, best.dist, delta.Pitch, delta.Yaw);
            }
        }
        runtimeLock.unlock(); usleep(16000);   // ~60Hz
        }
    }
}

void aimSetup(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (g_aimRun) return;
        g_aimRun = true;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
            aimLoop();
        });
        zkLog("[aim] thread up");
    });
}
