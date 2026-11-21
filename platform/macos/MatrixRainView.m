// MatrixRainView.m - macOS shell for the Matrix Rain engine (MatrixRain.saver).
//
// Glyphs are rasterized once with Core Text (per-glyph font fallback, so any
// katakana the configured font lacks still renders); the engine composites
// each frame into a pixel buffer that drawRect: shows as one CGImage.
#import <ScreenSaver/ScreenSaver.h>
#import <CoreText/CoreText.h>
#include <math.h>

#include "../../engine/config.h"
#include "../../engine/paths.h"
#include "../../engine/rain.h"
#include "../../engine/render.h"

@interface MatrixRainView : ScreenSaverView
@end

static CTFontRef make_font(const char *family, CGFloat size)
{
    CFStringRef name = CFStringCreateWithCString(NULL, family, kCFStringEncodingUTF8);
    CFDictionaryRef attrs = CFDictionaryCreate(NULL, (const void **)&kCTFontFamilyNameAttribute, (const void **)&name, 1,
                                               &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CTFontDescriptorRef desc = CTFontDescriptorCreateWithAttributes(attrs);
    CTFontRef font = CTFontCreateWithFontDescriptor(desc, size, NULL);
    CTFontRef bold = CTFontCreateCopyWithSymbolicTraits(font, size, NULL, kCTFontBoldTrait, kCTFontBoldTrait);
    CFRelease(desc);
    CFRelease(attrs);
    CFRelease(name);
    if (bold) { CFRelease(font); return bold; }
    return font;
}

static rain_atlas *build_atlas(const rain_config *cfg, int cell_w, int cell)
{
    rain_atlas *a = atlas_create(cell_w, cell, cfg->glyph_count);
    CTFontRef font;
    CGColorSpaceRef gray;
    int g;
    if (!a) return NULL;
    font = make_font(cfg->font_name, cell);
    gray = CGColorSpaceCreateDeviceGray();
    for (g = 0; g < cfg->glyph_count; g++) {
        uint32_t cp = cfg->glyphs[g];
        UniChar chars[2];
        CGGlyph glyphs[2] = { 0, 0 };
        CFIndex n;
        CTFontRef use = font;
        CGSize adv;
        CGFloat asc, desc;
        CGPoint pos;
        CGContextRef ctx;
        if (cp >= 0x10000) {
            chars[0] = (UniChar)(0xD800 + ((cp - 0x10000) >> 10));
            chars[1] = (UniChar)(0xDC00 + ((cp - 0x10000) & 0x3FF));
            n = 2;
        } else {
            chars[0] = (UniChar)cp;
            n = 1;
        }
        if (!CTFontGetGlyphsForCharacters(font, chars, glyphs, n)) {
            CFStringRef s = CFStringCreateWithCharacters(NULL, chars, n);
            use = CTFontCreateForString(font, s, CFRangeMake(0, n));
            CFRelease(s);
            CTFontGetGlyphsForCharacters(use, chars, glyphs, n);
        }
        CTFontGetAdvancesForGlyphs(use, kCTFontOrientationHorizontal, glyphs, &adv, 1);
        asc = CTFontGetAscent(use);
        desc = CTFontGetDescent(use);
        // Draw straight into the atlas mask: row 0 of a bitmap context is the top.
        ctx = CGBitmapContextCreate(atlas_mask(a, g, 0), (size_t)cell_w, (size_t)cell, 8, (size_t)cell_w, gray, kCGImageAlphaNone);
        if (ctx) {
            CGContextSetGrayFillColor(ctx, 0, 1);
            CGContextFillRect(ctx, CGRectMake(0, 0, cell_w, cell));
            CGContextSetShouldAntialias(ctx, true);
            CGContextSetShouldSmoothFonts(ctx, false);
            CGContextSetGrayFillColor(ctx, 1, 1);
            pos = CGPointMake((cell_w - adv.width) / 2, (cell - (asc + desc)) / 2 + desc);
            CTFontDrawGlyphs(use, glyphs, &pos, 1, ctx);
            CGContextRelease(ctx);
        }
        if (use != font) CFRelease(use);
    }
    CGColorSpaceRelease(gray);
    CFRelease(font);
    atlas_finish(a);
    return a;
}

@implementation MatrixRainView {
    rain_config _cfg;
    rain_t *_rain;
    rain_atlas *_atlas;
    rain_renderer *_rr;
    uint32_t *_fb;
    int _w, _h, _cell, _cellW;
    BOOL _full;
    NSWindow *_sheet;
    NSSlider *_speed, *_size, *_density;
}

- (void)loadConfig
{
    NSString *res = [[NSBundle bundleForClass:[self class]] resourcePath];
    rain_load_config(&_cfg, res.fileSystemRepresentation, NULL, 0);
    [self setAnimationTimeInterval:_cfg.speed_ms / 1000.0];
}

- (instancetype)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview
{
    self = [super initWithFrame:frame isPreview:isPreview];
    if (self) [self loadConfig];
    return self;
}

- (void)freeRain
{
    renderer_destroy(_rr); _rr = NULL;
    atlas_destroy(_atlas); _atlas = NULL;
    rain_destroy(_rain); _rain = NULL;
    free(_fb); _fb = NULL;
    _w = _h = _cell = _cellW = 0;
}

- (void)dealloc
{
    [self freeRain];
}

- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    [self freeRain]; // rebuild at the new scale on the next frame
}

// (Re)build everything for the current size and backing scale.
- (BOOL)ensureSurface
{
    NSRect px = [self convertRectToBacking:self.bounds];
    int w = (int)px.size.width, h = (int)px.size.height, cols, rows, cell, cell_w;
    CGFloat scale = self.bounds.size.width > 0 ? px.size.width / self.bounds.size.width : 1;
    if (w < 1 || h < 1) return NO;
    cell = (int)(_cfg.font_size * scale + 0.5);
    if (self.isPreview) cell = cell / 2 < 8 ? 8 : cell / 2;
    rain_cell_size(&_cfg, cell, &cell_w, &cell);
    if (w == _w && h == _h && cell == _cell && cell_w == _cellW && _rain) return YES;

    if (cell != _cell || cell_w != _cellW || !_atlas) {
        renderer_destroy(_rr);
        atlas_destroy(_atlas);
        _atlas = build_atlas(&_cfg, cell_w, cell);
        _rr = _atlas ? renderer_create(&_cfg, _atlas) : NULL;
        _cell = cell;
        _cellW = cell_w;
    }
    uint32_t *fb = realloc(_fb, (size_t)w * (size_t)h * sizeof(uint32_t));
    if (!fb || !_rr) return NO;
    _fb = fb;
    _w = w;
    _h = h;
    rain_grid_for(w, h, cell_w, cell, &cols, &rows);
    if (!_rain) _rain = rain_create(cols, rows, &_cfg, arc4random());
    else rain_resize(_rain, cols, rows);
    _full = YES;
    if (_rain) renderer_draw(_rr, _rain, _fb, _w, _h, _w, YES);
    _full = NO;
    return _rain != NULL;
}

- (void)animateOneFrame
{
    if (![self ensureSurface]) return;
    rain_tick(_rain);
    renderer_draw(_rr, _rain, _fb, _w, _h, _w, _full);
    _full = NO;
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)rect
{
    CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
    if (![self ensureSurface]) {
        [[NSColor blackColor] setFill];
        NSRectFill(self.bounds);
        return;
    }
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGDataProviderRef dp = CGDataProviderCreateWithData(NULL, _fb, (size_t)_w * (size_t)_h * 4, NULL);
    CGImageRef im = CGImageCreate((size_t)_w, (size_t)_h, 8, 32, (size_t)_w * 4, cs,
                                  kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaNoneSkipFirst,
                                  dp, NULL, false, kCGRenderingIntentDefault);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    CGContextDrawImage(ctx, NSRectToCGRect(self.bounds), im);
    CGImageRelease(im);
    CGDataProviderRelease(dp);
    CGColorSpaceRelease(cs);
}

// ---- configure sheet: the same three sliders as the Windows dialog -------

- (BOOL)hasConfigureSheet
{
    return YES;
}

- (NSString *)configPath
{
    char path[1024];
    if (!rain_resolve_config(path, sizeof path, [[NSBundle bundleForClass:[self class]] resourcePath].fileSystemRepresentation) &&
        !rain_user_config_path(path, sizeof path))
        return nil;
    return [NSString stringWithUTF8String:path];
}

static NSTextField *label(NSString *text, NSRect frame)
{
    NSTextField *t = [NSTextField labelWithString:text];
    t.frame = frame;
    return t;
}

static NSSlider *slider(NSRect frame, double lo, double hi, double val, NSInteger ticks)
{
    NSSlider *s = [NSSlider sliderWithValue:val minValue:lo maxValue:hi target:nil action:nil];
    s.frame = frame;
    s.numberOfTickMarks = ticks;
    s.allowsTickMarkValuesOnly = NO;
    return s;
}

- (NSWindow *)configureSheet
{
    NSView *v;
    NSButton *ok, *cancel, *open;
    NSString *path = [self configPath] ?: @"?";
    _sheet = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 380, 260)
                                         styleMask:NSWindowStyleMaskTitled
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    v = _sheet.contentView;
    [v addSubview:label(@"Fall speed (lower = faster)", NSMakeRect(20, 226, 340, 18))];
    _speed = slider(NSMakeRect(20, 198, 340, 24), 20, 150, _cfg.speed_ms, 14);
    [v addSubview:_speed];
    [v addSubview:label(@"Glyph size", NSMakeRect(20, 170, 340, 18))];
    _size = slider(NSMakeRect(20, 142, 340, 24), 12, 32, _cfg.font_size, 11);
    [v addSubview:_size];
    [v addSubview:label(@"How many columns", NSMakeRect(20, 114, 340, 18))];
    _density = slider(NSMakeRect(20, 86, 340, 24), 30, 100, _cfg.density, 8);
    [v addSubview:_density];
    NSTextField *where = label([NSString stringWithFormat:@"Saved to %@", path], NSMakeRect(20, 56, 340, 18));
    where.lineBreakMode = NSLineBreakByTruncatingMiddle;
    where.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    [v addSubview:where];

    open = [NSButton buttonWithTitle:@"Open config.yaml" target:self action:@selector(openConfig:)];
    open.frame = NSMakeRect(14, 12, 150, 32);
    cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancelSheet:)];
    cancel.frame = NSMakeRect(196, 12, 84, 32);
    cancel.keyEquivalent = @"\e";
    ok = [NSButton buttonWithTitle:@"OK" target:self action:@selector(okSheet:)];
    ok.frame = NSMakeRect(282, 12, 84, 32);
    ok.keyEquivalent = @"\r";
    [v addSubview:open];
    [v addSubview:cancel];
    [v addSubview:ok];
    return _sheet;
}

- (void)closeSheet
{
    NSWindow *parent = _sheet.sheetParent;
    if (parent) [parent endSheet:_sheet];
    else [NSApp endSheet:_sheet];
    [_sheet orderOut:nil];
}

- (void)cancelSheet:(id)sender
{
    [self closeSheet];
}

- (void)okSheet:(id)sender
{
    NSString *path = [self configPath];
    if (path) {
        char speed[16], size[16], density[16];
        config_kv kv[3] = { { "speed_ms", speed }, { "font_size", size }, { "density_percent", density } };
        snprintf(speed, sizeof speed, "%d", (int)lround(_speed.doubleValue));
        snprintf(size, sizeof size, "%d", (int)lround(_size.doubleValue));
        snprintf(density, sizeof density, "%d", (int)lround(_density.doubleValue));
        config_update_keys(path.fileSystemRepresentation, &_cfg, kv, 3);
        [self loadConfig];
        [self freeRain];
    }
    [self closeSheet];
}

- (void)openConfig:(id)sender
{
    NSString *path = [self configPath];
    if (!path) return;
    if (!rain_file_exists(path.fileSystemRepresentation)) config_write_default(path.fileSystemRepresentation, &_cfg);
    [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:path]];
}

@end
