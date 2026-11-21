/* matrixrain - X11 shell for the Matrix Rain engine (Debian / Ubuntu / any X).
 *
 *   matrixrain --fullscreen      standalone fullscreen; exits on input
 *   matrixrain --window          800x500 window, for testing
 *   matrixrain -root             xscreensaver hack (draws on XSCREENSAVER_WINDOW
 *                                or the root window)
 *   matrixrain -window-id 0x...  draw into an existing window
 *   --config FILE                use this config.yaml
 *
 * Glyphs are rasterized once with Xft (fontconfig fallback across CJK fonts);
 * the engine composites each frame into a pixel buffer we XPutImage.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "../../engine/config.h"
#include "../../engine/paths.h"
#include "../../engine/rain.h"
#include "../../engine/render.h"

enum { MODE_FULLSCREEN, MODE_WINDOW, MODE_HACK };

static Display *dpy;
static Window win;
static Visual *visual;
static int depth;
static GC gc;
static XImage *img;
static uint32_t *fb;
static int fb_w, fb_h, cell, cell_w; /* cell = glyph height, cell_w = column width */
static rain_config cfg;
static rain_t *rain;
static rain_atlas *atlas;
static rain_renderer *rr;
static int full_redraw = 1;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void usage(void)
{
    fprintf(stderr,
            "usage: matrixrain [--fullscreen | --window | -root | -window-id ID] [--config FILE]\n"
            "  --fullscreen   standalone fullscreen rain, exits on mouse/key (default)\n"
            "  --window       800x500 window, for testing\n"
            "  -root          xscreensaver hack mode\n"
            "  -window-id ID  draw into an existing X window\n"
            "  --config FILE  read settings from FILE instead of the usual lookup\n");
}

/* Vendored fonts (rain_font_dirs), for this process only via fontconfig -
 * nothing is installed. */
static void register_vendored_fonts(const char *exe)
{
    char dirs[RAIN_MAX_FONT_DIRS][1024];
    int i, n = rain_font_dirs(exe, dirs);
    for (i = 0; i < n; i++)
        if (FcConfigAppFontAddDir(NULL, (const FcChar8 *)dirs[i])) rain_log("font: added %s", dirs[i]);
}

static void exe_dir(char *out, size_t n)
{
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof buf - 1);
    char *slash;
    out[0] = 0;
    if (len <= 0) return;
    buf[len] = 0;
    slash = strrchr(buf, '/');
    if (slash) *slash = 0;
    if (strlen(buf) < n) strcpy(out, buf);
}

/* ---- glyph atlas via Xft ---------------------------------------------- */

static XftFont *open_font(const char *family, int px)
{
    return XftFontOpen(dpy, DefaultScreen(dpy),
                       XFT_FAMILY, XftTypeString, family,
                       XFT_PIXEL_SIZE, XftTypeDouble, (double)px,
                       XFT_WEIGHT, XftTypeInteger, XFT_WEIGHT_BOLD,
                       XFT_ANTIALIAS, XftTypeBool, True,
                       NULL);
}

static int mask_shift(unsigned long m)
{
    int s = 0;
    if (!m) return 0;
    while (!(m & 1)) { m >>= 1; s++; }
    return s;
}

static rain_atlas *build_atlas(void)
{
    static const char *fallbacks[] = {
        "Noto Sans CJK JP", "Noto Sans Mono CJK JP", "IPAGothic", "TakaoGothic",
        "VL Gothic", "Droid Sans Fallback", "monospace",
    };
    enum { NF = 1 + sizeof fallbacks / sizeof fallbacks[0] };
    XftFont *fonts[NF];
    int scr = DefaultScreen(dpy), nf = 0, g, i;
    Visual *dv = DefaultVisual(dpy, scr);
    Colormap cm = DefaultColormap(dpy, scr);
    Pixmap pm;
    XftDraw *draw;
    XftColor white, black;
    XRenderColor wc = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF }, bc = { 0, 0, 0, 0xFFFF };
    rain_atlas *a = atlas_create(cell_w, cell, cfg.glyph_count);
    int gshift = mask_shift(dv->green_mask);
    unsigned long gmax = dv->green_mask >> gshift;

    if (!a) return NULL;
    fonts[nf] = open_font(cfg.font_name, cell);
    if (fonts[nf]) nf++;
    for (i = 0; i < NF - 1; i++) {
        XftFont *f = open_font(fallbacks[i], cell);
        if (f) fonts[nf++] = f;
    }
    if (!nf) { atlas_finish(a); return a; }

    pm = XCreatePixmap(dpy, RootWindow(dpy, scr), (unsigned)cell_w, (unsigned)cell, (unsigned)DefaultDepth(dpy, scr));
    draw = XftDrawCreate(dpy, pm, dv, cm);
    XftColorAllocValue(dpy, dv, cm, &wc, &white);
    XftColorAllocValue(dpy, dv, cm, &bc, &black);

    for (g = 0; g < cfg.glyph_count; g++) {
        FcChar32 cp = cfg.glyphs[g];
        XftFont *f = fonts[0];
        XGlyphInfo ext;
        XImage *gi;
        uint8_t *mask = atlas_mask(a, g, 0);
        int x, y, px, py;
        for (i = 0; i < nf; i++)
            if (XftCharExists(dpy, fonts[i], cp)) { f = fonts[i]; break; }
        XftTextExtents32(dpy, f, &cp, 1, &ext);
        x = (cell_w - ext.xOff) / 2;
        y = (cell - (f->ascent + f->descent)) / 2 + f->ascent;
        XftDrawRect(draw, &black, 0, 0, (unsigned)cell_w, (unsigned)cell);
        XftDrawString32(draw, &white, f, x, y, &cp, 1);
        gi = XGetImage(dpy, pm, 0, 0, (unsigned)cell_w, (unsigned)cell, AllPlanes, ZPixmap);
        if (!gi) continue;
        for (py = 0; py < cell; py++)
            for (px = 0; px < cell_w; px++) {
                unsigned long v = (XGetPixel(gi, px, py) & dv->green_mask) >> gshift;
                mask[py * cell_w + px] = (uint8_t)(gmax ? v * 255 / gmax : 0);
            }
        XDestroyImage(gi);
    }

    XftColorFree(dpy, dv, cm, &white);
    XftColorFree(dpy, dv, cm, &black);
    XftDrawDestroy(draw);
    XFreePixmap(dpy, pm);
    for (i = 0; i < nf; i++) XftFontClose(dpy, fonts[i]);
    atlas_finish(a);
    return a;
}

/* ---- surface ---------------------------------------------------------- */

static int setup_surface(int w, int h)
{
    int cols, rows;
    uint32_t *nfb;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (img) { img->data = NULL; XDestroyImage(img); img = NULL; }
    nfb = (uint32_t *)realloc(fb, (size_t)w * (size_t)h * sizeof(uint32_t));
    if (!nfb) return -1;
    fb = nfb;
    fb_w = w;
    fb_h = h;
    img = XCreateImage(dpy, visual, (unsigned)depth, ZPixmap, 0, (char *)fb, (unsigned)w, (unsigned)h, 32, w * 4);
    if (!img) return -1;
    img->byte_order = LSBFirst; /* our pixels are little-endian 0x00RRGGBB */
    rain_grid_for(w, h, cell_w, cell, &cols, &rows);
    if (!rain) rain = rain_create(cols, rows, &cfg, (uint32_t)time(NULL) ^ ((uint32_t)getpid() << 16));
    else rain_resize(rain, cols, rows);
    full_redraw = 1;
    rain_log("surface %dx%d cell=%dx%d cols=%d rows=%d", w, h, cell_w, cell, cols, rows);
    return rain ? 0 : -1;
}

static void frame(void)
{
    rain_tick(rain);
    renderer_draw(rr, rain, fb, fb_w, fb_h, fb_w, full_redraw);
    full_redraw = 0;
    if (depth == 32) { /* ARGB visual: make every pixel opaque */
        size_t i, n = (size_t)fb_w * (size_t)fb_h;
        for (i = 0; i < n; i++) fb[i] |= 0xFF000000u;
    }
    XPutImage(dpy, win, gc, img, 0, 0, 0, 0, (unsigned)fb_w, (unsigned)fb_h);
    XFlush(dpy);
}

static void hide_cursor(void)
{
    static char none[1] = { 0 };
    XColor black;
    Pixmap blank = XCreateBitmapFromData(dpy, win, none, 1, 1);
    Cursor c;
    memset(&black, 0, sizeof black);
    c = XCreatePixmapCursor(dpy, blank, blank, &black, &black, 0, 0);
    XDefineCursor(dpy, win, c);
    XFreeCursor(dpy, c);
    XFreePixmap(dpy, blank);
}

static int xft_dpi(void)
{
    const char *v = XGetDefault(dpy, "Xft", "dpi");
    int d = v ? atoi(v) : 96;
    return d >= 48 && d <= 480 ? d : 96;
}

/* ---- main ------------------------------------------------------------- */

int main(int argc, char **argv)
{
    int mode = MODE_FULLSCREEN, i, scr, w, h;
    Window target = 0;
    Atom wm_delete = None;
    XWindowAttributes wa;
    char dir[PATH_MAX];
    int mouse_seen = 0, start_x = 0, start_y = 0, running = 1;
    double next;

    setlocale(LC_CTYPE, "");
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--fullscreen") || !strcmp(a, "-fullscreen")) mode = MODE_FULLSCREEN;
        else if (!strcmp(a, "--window") || !strcmp(a, "-window")) mode = MODE_WINDOW;
        else if (!strcmp(a, "-root") || !strcmp(a, "--root")) mode = MODE_HACK;
        else if ((!strcmp(a, "-window-id") || !strcmp(a, "--window-id")) && i + 1 < argc) {
            mode = MODE_HACK;
            target = (Window)strtoul(argv[++i], NULL, 0);
        }
        else if ((!strcmp(a, "--config") || !strcmp(a, "-config")) && i + 1 < argc) setenv("MATRIXRAIN_CONFIG", argv[++i], 1);
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        /* other xscreensaver flags (-fps, -install, -visual ...) are ignored */
    }

    exe_dir(dir, sizeof dir);
    rain_load_config(&cfg, dir, NULL, 0);
    register_vendored_fonts(dir);

    dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "matrixrain: cannot open display\n"); return 1; }
    scr = DefaultScreen(dpy);

    if (mode == MODE_HACK) {
        const char *env = getenv("XSCREENSAVER_WINDOW");
        if (!target && env && *env) target = (Window)strtoul(env, NULL, 0);
        if (!target) target = RootWindow(dpy, scr);
        win = target;
        if (!XGetWindowAttributes(dpy, win, &wa)) { fprintf(stderr, "matrixrain: bad window\n"); return 1; }
        visual = wa.visual;
        depth = wa.depth;
        w = wa.width;
        h = wa.height;
        XSelectInput(dpy, win, StructureNotifyMask | ExposureMask);
    } else {
        XSetWindowAttributes sa;
        visual = DefaultVisual(dpy, scr);
        depth = DefaultDepth(dpy, scr);
        w = mode == MODE_WINDOW ? 800 : DisplayWidth(dpy, scr);
        h = mode == MODE_WINDOW ? 500 : DisplayHeight(dpy, scr);
        memset(&sa, 0, sizeof sa);
        sa.background_pixel = BlackPixel(dpy, scr);
        sa.event_mask = KeyPressMask | ButtonPressMask | PointerMotionMask | StructureNotifyMask | ExposureMask;
        win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, (unsigned)w, (unsigned)h, 0, depth, InputOutput,
                            visual, CWBackPixel | CWEventMask, &sa);
        XStoreName(dpy, win, "Matrix Rain");
        wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(dpy, win, &wm_delete, 1);
        if (mode == MODE_FULLSCREEN) {
            Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
            Atom fs = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
            XChangeProperty(dpy, win, state, XA_ATOM, 32, PropModeReplace, (unsigned char *)&fs, 1);
            hide_cursor();
        }
        XMapRaised(dpy, win);
    }

    if (visual->class != TrueColor || (depth != 24 && depth != 32) ||
        visual->red_mask != 0xFF0000 || visual->green_mask != 0xFF00 || visual->blue_mask != 0xFF) {
        fprintf(stderr, "matrixrain: needs a 24/32-bit TrueColor visual\n");
        return 1;
    }

    cell = cfg.font_size * xft_dpi() / 96;
    if (mode == MODE_HACK && w < 400) cell = cell / 2 < 8 ? 8 : cell / 2; /* xscreensaver preview */
    rain_cell_size(&cfg, cell, &cell_w, &cell);
    gc = XCreateGC(dpy, win, 0, NULL);
    atlas = build_atlas();
    rr = atlas ? renderer_create(&cfg, atlas) : NULL;
    if (!rr || setup_surface(w, h) != 0) { fprintf(stderr, "matrixrain: out of memory\n"); return 1; }

    next = now_ms();
    while (running) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            switch (ev.type) {
            case ConfigureNotify:
                if (ev.xconfigure.window == win && (ev.xconfigure.width != fb_w || ev.xconfigure.height != fb_h))
                    setup_surface(ev.xconfigure.width, ev.xconfigure.height);
                break;
            case MapNotify:
                /* Only a viewable window may take focus (else BadMatch). */
                if (mode == MODE_FULLSCREEN && ev.xmap.window == win)
                    XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
                break;
            case Expose:
                full_redraw = 1;
                break;
            case MotionNotify:
                if (mode == MODE_FULLSCREEN) {
                    if (!mouse_seen) { start_x = ev.xmotion.x_root; start_y = ev.xmotion.y_root; mouse_seen = 1; }
                    else if (abs(ev.xmotion.x_root - start_x) > cfg.exit_mouse_pixels ||
                             abs(ev.xmotion.y_root - start_y) > cfg.exit_mouse_pixels)
                        running = 0;
                }
                break;
            case KeyPress:
            case ButtonPress:
                if (mode == MODE_FULLSCREEN) running = 0;
                break;
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == wm_delete) running = 0;
                break;
            }
        }
        if (!running) break;
        if (now_ms() >= next) {
            frame();
            next += cfg.speed_ms;
            if (next < now_ms()) next = now_ms() + cfg.speed_ms; /* fell behind: don't spiral */
        } else {
            fd_set fds;
            struct timeval tv;
            double wait = next - now_ms();
            int fd = ConnectionNumber(dpy);
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            tv.tv_sec = (long)(wait / 1000);
            tv.tv_usec = (long)((wait - tv.tv_sec * 1000.0) * 1000);
            if (select(fd + 1, &fds, NULL, NULL, &tv) < 0 && errno != EINTR) break;
        }
    }

    if (img) { img->data = NULL; XDestroyImage(img); }
    free(fb);
    renderer_destroy(rr);
    atlas_destroy(atlas);
    rain_destroy(rain);
    XFreeGC(dpy, gc);
    if (mode != MODE_HACK) XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
