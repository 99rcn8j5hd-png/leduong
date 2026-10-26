// ESP.mm - overlay vẽ box/tên/khoảng cách/máu (không hook render của game ->
//          không cần offset hàm Draw, ít rủi ro hơn)
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include "Core.hpp"
#include "Offsets.hpp"
#include <vector>
#include <mutex>
#include <unistd.h>
#include <atomic>

struct Snap {
    std::vector<ActorInfo> list;
    FMinimalViewInfo cam;
    bool haveCam = false;
};

static Snap            g_snap;
static std::mutex      g_snapMutex;
static CADisplayLink*  g_link = nil;
static std::atomic<bool> g_readerRun{false};

// ------------------------------------------------------- reader (background)
static void espReaderLoop(void) {
    while (g_readerRun) {
        const Config cfg=configSnapshot();
        FMinimalViewInfo cam;
        bool ok = getCamera(cam);
        std::vector<ActorInfo> list;
        if (cfg.esp || cfg.aim) snapshotActors(list);
        {
            std::lock_guard<std::mutex> g(g_snapMutex);
            g_snap.list.swap(list);
            g_snap.cam = cam;
            g_snap.haveCam = ok;
        }
        usleep(33000);   // ~30Hz đọc memory, đủ mượt, không nghẽn
    }
}

@interface ZKESPView : UIView
@end

@implementation ZKESPView

- (instancetype)initWithFrame:(CGRect)f {
    if ((self = [super initWithFrame:f])) {
        self.backgroundColor = [UIColor clearColor];
        self.userInteractionEnabled = NO;
        self.opaque = NO;
        self.clearsContextBeforeDrawing = YES;
    }
    return self;
}

- (void)tick:(CADisplayLink*)l { [self setNeedsDisplay]; }

- (void)start {
    if (!g_link) {
        g_link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
        g_link.preferredFramesPerSecond = 60;
        [g_link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    }
    if (!g_readerRun) {
        g_readerRun = true;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            espReaderLoop();
        });
    }
}

- (void)drawRect:(CGRect)rect {
    const Config cfg=configSnapshot();
    if (!cfg.esp && !cfg.aim) return;
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    if (!ctx) return;

    if (cfg.aim) {
        CGContextSetStrokeColorWithColor(ctx,UIColor.systemCyanColor.CGColor);
        CGContextSetLineWidth(ctx,1);
        CGFloat radius=cfg.aimFov;
        CGContextStrokeEllipseInRect(ctx,CGRectMake(CGRectGetMidX(self.bounds)-radius,CGRectGetMidY(self.bounds)-radius,radius*2,radius*2));
    }
    if (!cfg.esp) return;
    std::vector<ActorInfo> list;
    FMinimalViewInfo cam;
    bool haveCam = false;
    {
        std::lock_guard<std::mutex> g(g_snapMutex);
        list = g_snap.list;
        cam = g_snap.cam;
        haveCam = g_snap.haveCam;
    }
    if (!haveCam || list.empty()) return;

    const float sw = self.bounds.size.width, sh = self.bounds.size.height;
    int selfTeam = -1;
    for (size_t i = 0; i < list.size(); i++)
        if (list[i].isSelf) { selfTeam = list[i].team; break; }

    for (size_t i = 0; i < list.size(); i++) {
        ActorInfo& a = list[i];
        if (a.isSelf && !cfg.drawSelf) continue;
        if (!a.isSelf && selfTeam >= 0 && a.team == selfTeam) continue;
        if (a.dist > 800.0f) continue;

        FVector2D feet, head;
        if (!worldToScreen(a.loc, cam, sw, sh, feet)) continue;
        FVector top = a.loc;
        top.Z += 92.0f;                                  // chiều cao capsule ~1.8m
        if (!worldToScreen(top, cam, sw, sh, head)) continue;

        float h = feet.Y - head.Y;
        if (h < 6.0f || h > sh * 2.0f) continue;
        float w = h * 0.52f;

        BOOL visible = fabsf(feet.X - sw * 0.5f) < sw * 0.55f;

        CGColorRef col = a.isSelf
            ? [UIColor colorWithRed:0.35 green:0.75 blue:1.0 alpha:0.95].CGColor
            : (a.dist < 120.f
               ? [UIColor colorWithRed:1.0 green:0.32 blue:0.32 alpha:0.95].CGColor
               : [UIColor colorWithRed:1.0 green:0.78 blue:0.2 alpha:0.9].CGColor);
        CGContextSetStrokeColorWithColor(ctx, col);
        CGContextSetLineWidth(ctx, 1.4f);

        if (cfg.drawBox) {
            CGRect r = CGRectMake(head.X - w * 0.5f, head.Y, w, h);
            CGContextStrokeRect(ctx, r);
            // 4 góc cho dễ nhìn
            float cl = w * 0.28f;
            CGContextSetLineWidth(ctx, 2.0f);
            CGPoint pts[8][2] = {
                {{r.origin.x, r.origin.y}, {r.origin.x + cl, r.origin.y}},
                {{r.origin.x, r.origin.y}, {r.origin.x, r.origin.y + cl * 0.8f}},
                {{CGRectGetMaxX(r), r.origin.y}, {CGRectGetMaxX(r) - cl, r.origin.y}},
                {{CGRectGetMaxX(r), r.origin.y}, {CGRectGetMaxX(r), r.origin.y + cl * 0.8f}},
                {{r.origin.x, CGRectGetMaxY(r)}, {r.origin.x + cl, CGRectGetMaxY(r)}},
                {{r.origin.x, CGRectGetMaxY(r)}, {r.origin.x, CGRectGetMaxY(r) - cl * 0.8f}},
                {{CGRectGetMaxX(r), CGRectGetMaxY(r)}, {CGRectGetMaxX(r) - cl, CGRectGetMaxY(r)}},
                {{CGRectGetMaxX(r), CGRectGetMaxY(r)}, {CGRectGetMaxX(r), CGRectGetMaxY(r) - cl * 0.8f}},
            };
            for (int k = 0; k < 8; k++) {
                CGContextMoveToPoint(ctx, pts[k][0].x, pts[k][0].y);
                CGContextAddLineToPoint(ctx, pts[k][1].x, pts[k][1].y);
            }
            CGContextStrokePath(ctx);
        }

        if (cfg.drawHealth && a.healthKnown) {
            float hp = a.hp;
            float maxHp = 100.0f;
            if (hp > 200.f) maxHp = 1000.f;
            if (hp > 2000.f) maxHp = 10000.f;
            float frac = hp / maxHp;
            if (frac < 0) frac = 0;
            if (frac > 1) frac = 1;
            CGRect br = CGRectMake(head.X - w * 0.5f - 5, head.Y, 3, h);
            CGContextSetFillColorWithColor(ctx, [UIColor colorWithWhite:0 alpha:0.45].CGColor);
            CGContextFillRect(ctx, br);
            CGRect fr = CGRectMake(br.origin.x, br.origin.y + h * (1 - frac), 3, h * frac);
            CGContextSetFillColorWithColor(ctx,
                [UIColor colorWithRed:0.25 green:0.95 blue:0.45 alpha:0.95].CGColor);
            CGContextFillRect(ctx, fr);
        }

        if (cfg.drawName || cfg.drawDist) {
            NSString* txt = @"";
            if (cfg.drawName && cfg.drawDist)
                txt = [NSString stringWithFormat:@"%s  %.0fm", a.cls, a.dist];
            else if (cfg.drawDist)
                txt = [NSString stringWithFormat:@"%.0fm", a.dist];
            else
                txt = [NSString stringWithUTF8String:a.cls];

            NSDictionary* attrs = @{
                NSFontAttributeName: [UIFont monospacedSystemFontOfSize:9 weight:UIFontWeightBold],
                NSForegroundColorAttributeName: visible ? [UIColor whiteColor]
                                                        : [UIColor colorWithWhite:1 alpha:0.5],
            };
            CGSize ts = [txt sizeWithAttributes:attrs];
            [txt drawAtPoint:CGPointMake(head.X - ts.width * 0.5f, head.Y - ts.height - 2)
              withAttributes:attrs];
        }
    }
}

@end

// ------------------------------------------------------------------ setup
static UIWindow* g_espWin = nil;

void espSetup(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (g_espWin) return;
        CGRect scr = [UIScreen mainScreen].bounds;
        g_espWin = [[UIWindow alloc] initWithFrame:scr];
        g_espWin.windowLevel = UIWindowLevelAlert + 1;
        g_espWin.backgroundColor = [UIColor clearColor];
        g_espWin.userInteractionEnabled = NO;          // touch xuyên xuống game
        UIViewController* vc = [[UIViewController alloc] init];
        vc.view.backgroundColor = [UIColor clearColor];
        g_espWin.rootViewController = vc;

        ZKESPView* v = [[ZKESPView alloc] initWithFrame:scr];
        v.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
        [vc.view addSubview:v];
        g_espWin.hidden=NO;
        [v start];
        zkLog("[esp] overlay up");
    });
}
