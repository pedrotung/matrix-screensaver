// Matrix digital rain screensaver for macOS.
//
// The rain and the Apple II terminal are drawn by the shared core (../core/matrix_core.h)
// into a pixel buffer, which this view puts on screen each frame.
//
// macOS closes a screensaver on any key or mouse input and then shows its own lock screen,
// so a screensaver cannot ask for its own password. Instead, the "Wake up, Neo..." terminal
// plays as an interlude every few minutes, and the Mac's login password protects the screen.

#import <ScreenSaver/ScreenSaver.h>
#import <Cocoa/Cocoa.h>

#include "../core/matrix_core.h"

static NSString* const kModule = @"com.pedrotung.MatrixRain";

static double NowMs() { return [NSProcessInfo processInfo].systemUptime * 1000.0; }

namespace {
enum StepKind { ST_WAIT, ST_TYPE, ST_CLEAR, ST_DONE };
struct Step { StepKind kind; std::wstring text; int speed; int ms; };
}

@interface MatrixRainPTView : ScreenSaverView
@end

@implementation MatrixRainPTView {
    std::vector<uint32_t> _pixels;
    PixelBuf _fb;
    Rain _rain;
    Terminal _term;
    BOOL _ready;
    double _last;

    // "Wake up, Neo" interlude
    BOOL _interludeOn;
    BOOL _inTerminal;
    double _nextInterlude;
    std::vector<Step> _steps;
    size_t _stepIdx;
    double _stepStart, _nextChar;
    size_t _typed;
    std::wstring _prefix, _screen;

    // settings sheet
    NSWindow* _sheet;
    NSSlider* _speed;
    NSSlider* _density;
    NSSlider* _size;
    NSTextField* _speedVal;
    NSTextField* _densityVal;
    NSTextField* _sizeVal;
    NSButton* _interlude;
}

+ (ScreenSaverDefaults*)defaults {
    ScreenSaverDefaults* d = [ScreenSaverDefaults defaultsForModuleWithName:kModule];
    [d registerDefaults:@{@"Speed": @10, @"Density": @85, @"FontSize": @18, @"Interlude": @YES}];
    return d;
}

- (instancetype)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview {
    if ((self = [super initWithFrame:frame isPreview:isPreview])) {
        [self setAnimationTimeInterval:1.0 / 30.0];
        // On macOS 14+ the screensaver host can keep old instances alive; stop drawing when told to.
        [[NSDistributedNotificationCenter defaultCenter] addObserver:self
                                                            selector:@selector(willStop:)
                                                                name:@"com.apple.screensaver.willstop"
                                                              object:nil];
    }
    return self;
}

- (void)dealloc {
    [[NSDistributedNotificationCenter defaultCenter] removeObserver:self];
}

- (void)willStop:(NSNotification*)note {
    if (!self.isPreview) [self stopAnimation];
}

- (CGFloat)pixelScale {
    CGFloat s = self.window ? self.window.backingScaleFactor : [NSScreen mainScreen].backingScaleFactor;
    return s > 0 ? s : 1.0;
}

- (void)setUp {
    ScreenSaverDefaults* d = [MatrixRainPTView defaults];
    int speed10 = std::clamp((int)[d integerForKey:@"Speed"], 3, 25);
    int density = std::clamp((int)[d integerForKey:@"Density"], 30, 100);
    int size = std::clamp((int)[d integerForKey:@"FontSize"], 12, 32);
    _interludeOn = [d boolForKey:@"Interlude"] && !self.isPreview;

    CGFloat scale = [self pixelScale];
    int w = std::max(1, (int)lround(NSWidth(self.bounds) * scale));
    int h = std::max(1, (int)lround(NSHeight(self.bounds) * scale));
    _pixels.assign(size_t(w) * h, 0);
    _fb.px = _pixels.data();
    _fb.w = w;
    _fb.h = h;

    g_rng ^= (uint32_t)(NowMs() * 1000) * 2654435761u;
    int cell = self.isPreview ? std::max(5, int(size * h / 600.0)) : int(lround(size * scale));
    _rain.Init(w, h, cell, speed10 / 10.0, density / 100.0);
    if (_interludeOn) _term.Init(0, 0, w, h, scale);

    _inTerminal = NO;
    _last = NowMs();
    _nextInterlude = _last + 12000;  // first interlude shortly after the screensaver starts
    _ready = YES;
}

- (void)startAnimation {
    [super startAnimation];
    [self setUp];
}

- (void)stopAnimation {
    [super stopAnimation];
    _ready = NO;
    _pixels.clear();
    _pixels.shrink_to_fit();
    _fb = PixelBuf();
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    if (_ready) [self setUp];
}

// ---------------------------------------------------------------- interlude script

- (void)startInterlude {
    _inTerminal = YES;
    _screen.clear();
    _steps = {
        {ST_WAIT, L"", 0, 300},
        {ST_TYPE, L"Wake up, Neo...", 45, 0},
        {ST_WAIT, L"", 0, 1100},
        {ST_CLEAR, L"", 0, 0},
        {ST_WAIT, L"", 0, 250},
        {ST_TYPE, L"Follow the white rabbit.", 45, 0},
        {ST_WAIT, L"", 0, 1800},
        {ST_CLEAR, L"", 0, 0},
        {ST_WAIT, L"", 0, 400},
        {ST_DONE, L"", 0, 0},
    };
    _stepIdx = 0;
    _stepStart = _nextChar = NowMs();
    _typed = 0;
    _prefix.clear();
}

// Returns YES while a line is being typed (solid cursor).
- (BOOL)runSteps:(double)now {
    while (_stepIdx < _steps.size()) {
        Step& s = _steps[_stepIdx];
        auto advance = [&] { _stepIdx++; _stepStart = now; _typed = 0; _prefix = _screen; _nextChar = now; };
        switch (s.kind) {
        case ST_WAIT:
            if (now - _stepStart < s.ms) return NO;
            advance();
            break;
        case ST_CLEAR:
            _screen.clear();
            advance();
            break;
        case ST_DONE:
            _steps.clear();
            _inTerminal = NO;
            _fb.Clear(0);
            _nextInterlude = now + 180000;  // every 3 minutes
            return NO;
        case ST_TYPE:
            while (_typed < s.text.size() && now >= _nextChar) {
                wchar_t c = s.text[_typed++];
                _screen = _prefix + s.text.substr(0, _typed);
                _nextChar += s.speed * (0.5 + Rand01()) + ((c == L',' || c == L'.') ? s.speed : 0);
            }
            if (_typed < s.text.size()) return YES;
            advance();
            break;
        }
    }
    return NO;
}

// ---------------------------------------------------------------- frames

- (void)animateOneFrame {
    if (!_ready) return;
    double now = NowMs();
    double dt = std::min((now - _last) / 16.67, 4.0);
    _last = now;

    if (_interludeOn && !_inTerminal && now >= _nextInterlude) [self startInterlude];

    if (_inTerminal) {
        BOOL typing = [self runSteps:now];
        if (_inTerminal) {
            _term.SetText(_screen, typing);
            _term.SetCursorPhase(fmod(now, 1060.0) < 530.0);
            _term.Render(_fb, 0.97 + 0.03 * Rand01());
        }
    } else {
        _rain.Step(_fb, dt);
    }
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)rect {
    CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
    if (!_ready || _pixels.empty()) {
        CGContextSetRGBFillColor(ctx, 0, 0, 0, 1);
        CGContextFillRect(ctx, NSRectToCGRect(self.bounds));
        return;
    }
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef dp = CGDataProviderCreateWithData(nullptr, _pixels.data(), _pixels.size() * 4, nullptr);
    CGImageRef img = CGImageCreate(_fb.w, _fb.h, 8, 32, size_t(_fb.w) * 4, cs,
                                   kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst, dp, nullptr, false,
                                   kCGRenderingIntentDefault);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    CGContextDrawImage(ctx, NSRectToCGRect(self.bounds), img);
    CGImageRelease(img);
    CGDataProviderRelease(dp);
    CGColorSpaceRelease(cs);
}

// ---------------------------------------------------------------- settings sheet

- (BOOL)hasConfigureSheet {
    return YES;
}

- (NSTextField*)label:(NSString*)text frame:(NSRect)f {
    NSTextField* t = [NSTextField labelWithString:text];
    t.frame = f;
    return t;
}

- (NSSlider*)slider:(double)lo max:(double)hi value:(double)v frame:(NSRect)f {
    NSSlider* s = [NSSlider sliderWithValue:v minValue:lo maxValue:hi target:self action:@selector(sliderMoved:)];
    s.frame = f;
    return s;
}

- (void)updateLabels {
    int sp = (int)lround(_speed.doubleValue);
    _speedVal.stringValue = [NSString stringWithFormat:@"%d.%dx", sp / 10, sp % 10];
    _densityVal.stringValue = [NSString stringWithFormat:@"%d%%", (int)lround(_density.doubleValue)];
    _sizeVal.stringValue = [NSString stringWithFormat:@"%d px", (int)lround(_size.doubleValue)];
}

- (void)sliderMoved:(id)sender {
    [self updateLabels];
}

- (NSWindow*)configureSheet {
    ScreenSaverDefaults* d = [MatrixRainPTView defaults];
    _sheet = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 440, 250)
                                         styleMask:NSWindowStyleMaskTitled
                                           backing:NSBackingStoreBuffered
                                             defer:YES];
    NSView* v = _sheet.contentView;

    CGFloat y = 196;
    [v addSubview:[self label:@"速度 Speed" frame:NSMakeRect(20, y, 110, 20)]];
    _speed = [self slider:3 max:25 value:[d integerForKey:@"Speed"] frame:NSMakeRect(130, y, 210, 24)];
    [v addSubview:_speed];
    _speedVal = [self label:@"" frame:NSMakeRect(350, y, 70, 20)];
    [v addSubview:_speedVal];

    y -= 40;
    [v addSubview:[self label:@"密度 Density" frame:NSMakeRect(20, y, 110, 20)]];
    _density = [self slider:30 max:100 value:[d integerForKey:@"Density"] frame:NSMakeRect(130, y, 210, 24)];
    [v addSubview:_density];
    _densityVal = [self label:@"" frame:NSMakeRect(350, y, 70, 20)];
    [v addSubview:_densityVal];

    y -= 40;
    [v addSubview:[self label:@"字号 Font size" frame:NSMakeRect(20, y, 110, 20)]];
    _size = [self slider:12 max:32 value:[d integerForKey:@"FontSize"] frame:NSMakeRect(130, y, 210, 24)];
    [v addSubview:_size];
    _sizeVal = [self label:@"" frame:NSMakeRect(350, y, 70, 20)];
    [v addSubview:_sizeVal];

    y -= 44;
    _interlude = [NSButton checkboxWithTitle:@"每 3 分钟播放 Wake up, Neo 片段 / Play the \"Wake up, Neo\" interlude"
                                      target:nil
                                      action:nil];
    _interlude.frame = NSMakeRect(18, y, 410, 22);
    _interlude.state = [d boolForKey:@"Interlude"] ? NSControlStateValueOn : NSControlStateValueOff;
    [v addSubview:_interlude];

    NSButton* cancel = [NSButton buttonWithTitle:@"取消 Cancel" target:self action:@selector(cancelSheet:)];
    cancel.frame = NSMakeRect(220, 16, 100, 32);
    cancel.keyEquivalent = @"\033";
    [v addSubview:cancel];
    NSButton* ok = [NSButton buttonWithTitle:@"确定 OK" target:self action:@selector(saveSheet:)];
    ok.frame = NSMakeRect(326, 16, 100, 32);
    ok.keyEquivalent = @"\r";
    [v addSubview:ok];

    [self updateLabels];
    return _sheet;
}

- (void)closeSheet {
    if (_sheet.sheetParent) [_sheet.sheetParent endSheet:_sheet];
    else [NSApp endSheet:_sheet];
    _sheet = nil;
}

- (void)saveSheet:(id)sender {
    ScreenSaverDefaults* d = [MatrixRainPTView defaults];
    [d setInteger:lround(_speed.doubleValue) forKey:@"Speed"];
    [d setInteger:lround(_density.doubleValue) forKey:@"Density"];
    [d setInteger:lround(_size.doubleValue) forKey:@"FontSize"];
    [d setBool:_interlude.state == NSControlStateValueOn forKey:@"Interlude"];
    [d synchronize];
    [self closeSheet];
    if (_ready) [self setUp];
}

- (void)cancelSheet:(id)sender {
    [self closeSheet];
}

@end
