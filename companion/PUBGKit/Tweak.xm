// UI startup is independent of runtime data discovery.
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include "Core.hpp"
#include "Offsets.hpp"

extern void startMenu(void);
static void zkBoot(void) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        startMenu();
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)(NSEC_PER_SEC)),
                       dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
            @autoreleasepool {
                loadOffsets();
                zkLog("[startup-v2] bundle=%s",NSBundle.mainBundle.bundleIdentifier.UTF8String);
                bool ok=coreInit(false);
                zkLog("[boot] coreInit=%d",ok?1:0);
            }
        });
    });
}
%ctor {
    @autoreleasepool {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)(6*NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{ zkBoot(); });
    }
}
