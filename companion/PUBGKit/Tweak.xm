// Tweak.xm - điểm vào, chờ app lên rồi boot
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include "Core.hpp"
#include "Offsets.hpp"
#include <stdbool.h>

extern void startMenu(void);

static bool g_booted = false;

static void zkBoot(void) {
    if (g_booted) return;
    g_booted = true;

    loadOffsets();
    zkLog("==== PUBGM VN boot, bundle=%s ====", [[[NSBundle mainBundle] bundleIdentifier] UTF8String]);

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        BOOL ok = coreInit(false);
        zkLog("[boot] coreInit=%d", ok ? 1 : 0);

        dispatch_async(dispatch_get_main_queue(), ^{
            startMenu();
            // tự ghi 1 bản probe sau khi vào lobby, để user chỉ việc lấy file
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(15 * NSEC_PER_SEC)),
                           dispatch_get_main_queue(), ^{
                dispatch_async(dispatch_get_global_queue(0, 0), ^{
                    probeLogDump(false);
                });
            });
        });
    });
}

%ctor {
    @autoreleasepool {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(6 * NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{
            zkBoot();
        });
    }
}
