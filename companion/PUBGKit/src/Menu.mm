// Menu.mm - overlay menu (cửa sổ trong suốt nổi trên game)
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include "Core.hpp"
#include "Offsets.hpp"

extern void espSetup(void);
extern void aimSetup(void);

// ------------------------------------------------------------------ views
@interface ZKPassthrough : UIView
@end
@implementation ZKPassthrough
- (UIView*)hitTest:(CGPoint)p withEvent:(UIEvent*)e {
    UIView* v = [super hitTest:p withEvent:e];
    return (v == self) ? nil : v;      // chỉ nhận touch ở control con
}
@end

@interface ZKMenuWindow : UIWindow
@property(nonatomic,weak) UIView *panel;
@property(nonatomic,weak) UIView *fab;
@end
@implementation ZKMenuWindow
- (UIView*)hitTest:(CGPoint)p withEvent:(UIEvent*)e {
    UIView* v = [super hitTest:p withEvent:e];
    if (self.panel && !self.panel.hidden && CGRectContainsPoint([self.panel convertRect:self.panel.bounds toView:self],p)) return v;
    if (self.fab && CGRectContainsPoint([self.fab convertRect:self.fab.bounds toView:self],p)) return v;
    return nil;
}
@end

@interface ZKMenu : NSObject
@property (nonatomic, strong) UIWindow*           win;
@property (nonatomic, strong) UIButton*           fab;
@property (nonatomic, strong) UIScrollView*       panel;
@property (nonatomic, strong) UILabel*            status;
@end

static ZKMenu* g_menu = nil;

@implementation ZKMenu {
    UILabel*  _fovVal;
    UILabel*  _smoothVal;
}

- (UISwitch*)addSwitch:(NSString*)title y:(CGFloat)y key:(int)key {
    UILabel* l = [[UILabel alloc] initWithFrame:CGRectMake(14, y, 210, 30)];
    l.text = title;
    l.textColor = [UIColor colorWithWhite:0.95 alpha:1];
    l.font = [UIFont systemFontOfSize:13 weight:UIFontWeightMedium];
    [_panel addSubview:l];

    UISwitch* s = [[UISwitch alloc] initWithFrame:CGRectMake(232, y - 1, 51, 31)];
    s.tag = key;
    s.transform = CGAffineTransformMakeScale(0.8, 0.8);
    s.onTintColor = [UIColor colorWithRed:0.16 green:0.72 blue:0.48 alpha:1];
    [s addTarget:self action:@selector(onSwitch:) forControlEvents:UIControlEventValueChanged];
    [_panel addSubview:s];
    const Config cfg=configSnapshot();
    const bool enabled[]={false,cfg.esp,cfg.drawBox,cfg.drawName,cfg.drawDist,cfg.drawHealth,cfg.drawSelf,cfg.aim,cfg.aimPredict};
    if (key>=1 && key<=8) s.on=enabled[key];
    return s;
}

- (void)onSwitch:(UISwitch*)s {
    std::lock_guard<std::mutex> configLock(g_configMutex);
    BOOL v = s.isOn;
    switch (s.tag) {
        case 1: g_cfg.esp = v; break;
        case 2: g_cfg.drawBox = v; break;
        case 3: g_cfg.drawName = v; break;
        case 4: g_cfg.drawDist = v; break;
        case 5: g_cfg.drawHealth = v; break;
        case 6: g_cfg.drawSelf = v; break;
        case 7: g_cfg.aim = v; break;
        case 8: g_cfg.aimPredict = v; break;
        default: break;
    }
}

- (void)onSlider:(UISlider*)s {
    std::lock_guard<std::mutex> configLock(g_configMutex);
    if (s.tag == 20) { g_cfg.aimFov = s.value; _fovVal.text = [NSString stringWithFormat:@"%.0f pt", s.value]; }
    if (s.tag == 21) { g_cfg.aimSmooth = s.value; _smoothVal.text = [NSString stringWithFormat:@"%.1f", s.value]; }
}

- (NSString *)coreStatus {
    std::unique_lock<std::recursive_mutex> lock(g_coreMutex,std::try_to_lock);
    if (!lock.owns_lock()) return @"Đang xử lý dữ liệu…";
    return [NSString stringWithFormat:@"Dữ liệu: %@ · camera: %@",coreReady()?@"sẵn sàng":@"chưa đủ",g_off.cameraMgrCache?@"có":@"chưa có"];
}
- (void)onProbe:(id)s {
    self.status.text = @"đang probe...";
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        probeLogDump(false);
        dispatch_async(dispatch_get_main_queue(), ^{
            self.status.text = [NSString stringWithFormat:@"log: %s/probe.txt", workDir()];
        });
    });
}

- (void)onRescan:(id)s {
    self.status.text = @"đang dò lại...";
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        coreInit(true);
        dispatch_async(dispatch_get_main_queue(), ^{
            self.status.text = [self coreStatus];
        });
    });
}

- (void)onFab:(id)s {
    self.panel.hidden = !self.panel.hidden;
    if (!self.panel.hidden) {
        self.status.text = [self coreStatus];
    }
}

- (void)build {
    const Config cfg=configSnapshot();
    CGRect scr = [UIScreen mainScreen].bounds;

    self.win = [[ZKMenuWindow alloc] initWithFrame:scr];
    self.win.windowLevel = UIWindowLevelAlert + 2;
    self.win.backgroundColor = [UIColor clearColor];
    self.win.rootViewController = [[UIViewController alloc] init];
    self.win.rootViewController.view.backgroundColor = [UIColor clearColor];
    self.win.hidden=NO;

    UIView* root = self.win.rootViewController.view;

    // FAB
    self.fab = [UIButton buttonWithType:UIButtonTypeCustom];
    self.fab.frame = CGRectMake(12, scr.size.height * 0.35, 44, 44);
    self.fab.backgroundColor = [UIColor colorWithRed:0.06 green:0.08 blue:0.10 alpha:0.82];
    self.fab.layer.cornerRadius = 22;
    self.fab.layer.borderWidth = 1;
    self.fab.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.18].CGColor;
    [self.fab setTitle:@"ZK" forState:UIControlStateNormal];
    [self.fab setTitleColor:[UIColor colorWithRed:0.2 green:0.85 blue:0.6 alpha:1]
                   forState:UIControlStateNormal];
    self.fab.titleLabel.font = [UIFont boldSystemFontOfSize:14];
    [self.fab addTarget:self action:@selector(onFab:) forControlEvents:UIControlEventTouchUpInside];
    [root addSubview:self.fab];

    UIPanGestureRecognizer* pan = [[UIPanGestureRecognizer alloc] initWithTarget:self
                                                                         action:@selector(onPan:)];
    [self.fab addGestureRecognizer:pan];

    // Panel
    CGFloat w = MIN(300,scr.size.width-20), h = MIN(560,scr.size.height-60);
    self.panel = [[UIScrollView alloc] initWithFrame:CGRectMake(scr.size.width - w - 10,
                                                          MAX(20,(scr.size.height-h)/2), w, h)];
    self.panel.backgroundColor = [UIColor colorWithRed:0.05 green:0.07 blue:0.09 alpha:0.94];
    self.panel.layer.cornerRadius = 14;
    self.panel.layer.borderWidth = 1;
    self.panel.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.12].CGColor;
    self.panel.hidden = YES;
    [root addSubview:self.panel];

    UILabel* title = [[UILabel alloc] initWithFrame:CGRectMake(14, 10, 200, 22)];
    title.text = @"PUBGKit · Thử nghiệm";
    title.textColor = [UIColor colorWithRed:0.25 green:0.85 blue:0.6 alpha:1];
    title.font = [UIFont boldSystemFontOfSize:13];
    [self.panel addSubview:title];

    CGFloat y = 38;
    [self addSwitch:@"ESP" y:y key:1];            y += 36;
    [self addSwitch:@"Khung" y:y key:2];            y += 36;
    [self addSwitch:@"Tên class" y:y key:3];      y += 36;
    [self addSwitch:@"Khoảng cách" y:y key:4];    y += 36;
    [self addSwitch:@"Máu" y:y key:5];            y += 36;
    [self addSwitch:@"Hiện bản thân" y:y key:6];  y += 36;
    [self addSwitch:@"Tự ngắm" y:y key:7];         y += 36;
    [self addSwitch:@"Bù đạn rơi" y:y key:8];     y += 38;

    // FOV slider
    UILabel* fl = [[UILabel alloc] initWithFrame:CGRectMake(14, y, 90, 20)];
    fl.text = @"FOV aim"; fl.textColor = [UIColor lightGrayColor]; fl.font = [UIFont systemFontOfSize:12];
    [self.panel addSubview:fl];
    _fovVal = [[UILabel alloc] initWithFrame:CGRectMake(232, y, 60, 20)];
    _fovVal.text = @"150"; _fovVal.textColor = [UIColor whiteColor];
    _fovVal.font = [UIFont systemFontOfSize:12];
    [self.panel addSubview:_fovVal];
    UISlider* fs = [[UISlider alloc] initWithFrame:CGRectMake(14, y + 20, w - 28, 20)];
    fs.tag = 20; fs.minimumValue = 20; fs.maximumValue = 500; fs.value = cfg.aimFov;
    [fs addTarget:self action:@selector(onSlider:) forControlEvents:UIControlEventValueChanged];
    [self.panel addSubview:fs];
    y += 46;

    UILabel* sl = [[UILabel alloc] initWithFrame:CGRectMake(14, y, 90, 20)];
    sl.text = @"Độ mượt"; sl.textColor = [UIColor lightGrayColor]; sl.font = [UIFont systemFontOfSize:12];
    [self.panel addSubview:sl];
    _smoothVal = [[UILabel alloc] initWithFrame:CGRectMake(232, y, 60, 20)];
    _smoothVal.text = @"8.0"; _smoothVal.textColor = [UIColor whiteColor];
    _smoothVal.font = [UIFont systemFontOfSize:12];
    [self.panel addSubview:_smoothVal];
    UISlider* ss = [[UISlider alloc] initWithFrame:CGRectMake(14, y + 20, w - 28, 20)];
    ss.tag = 21; ss.minimumValue = 1; ss.maximumValue = 25; ss.value = cfg.aimSmooth;
    [ss addTarget:self action:@selector(onSlider:) forControlEvents:UIControlEventValueChanged];
    [self.panel addSubview:ss];
    y += 50;

    UIButton* pb = [UIButton buttonWithType:UIButtonTypeSystem];
    pb.frame = CGRectMake(14, y, 130, 34);
    [pb setTitle:@"PROBE → log" forState:UIControlStateNormal];
    pb.backgroundColor = [UIColor colorWithWhite:1 alpha:0.10];
    pb.layer.cornerRadius = 8;
    [pb addTarget:self action:@selector(onProbe:) forControlEvents:UIControlEventTouchUpInside];
    [self.panel addSubview:pb];

    UIButton* rb = [UIButton buttonWithType:UIButtonTypeSystem];
    rb.frame = CGRectMake(152, y, 130, 34);
    [rb setTitle:@"Dò lại offset" forState:UIControlStateNormal];
    rb.backgroundColor = [UIColor colorWithWhite:1 alpha:0.10];
    rb.layer.cornerRadius = 8;
    [rb addTarget:self action:@selector(onRescan:) forControlEvents:UIControlEventTouchUpInside];
    [self.panel addSubview:rb];

    y += 44;
    self.status = [[UILabel alloc] initWithFrame:CGRectMake(14, y, w - 28, 34)];
    self.status.text = @"sẵn sàng";
    self.status.numberOfLines = 2;
    self.status.textColor = [UIColor colorWithWhite:0.7 alpha:1];
    self.status.font = [UIFont systemFontOfSize:10];
    [self.panel addSubview:self.status];
    self.panel.contentSize=CGSizeMake(w,y+48);
    ((ZKMenuWindow *)self.win).panel=self.panel;
    ((ZKMenuWindow *)self.win).fab=self.fab;
}

- (void)onPan:(UIPanGestureRecognizer*)g {
    CGPoint t = [g translationInView:self.win];
    CGPoint c = self.fab.center;
    self.fab.center = CGPointMake(c.x + t.x, c.y + t.y);
    CGRect bounds=UIEdgeInsetsInsetRect(self.win.bounds,self.win.safeAreaInsets);
    CGRect frame=self.fab.frame;
    frame.origin.x=MAX(CGRectGetMinX(bounds),MIN(frame.origin.x,CGRectGetMaxX(bounds)-frame.size.width));
    frame.origin.y=MAX(CGRectGetMinY(bounds),MIN(frame.origin.y,CGRectGetMaxY(bounds)-frame.size.height));
    self.fab.frame=frame;
    [g setTranslation:CGPointZero inView:self.win];
}

@end

void startMenu(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (g_menu) return;
        g_menu = [[ZKMenu alloc] init];
        [g_menu build];
        espSetup();
        aimSetup();
        zkLog("[menu] up");
    });
}
