/* MatrixRain.scr - Windows shell for the Matrix Rain engine.
 *
 * Usage (the standard screensaver contract):
 *   MatrixRain.scr /s          run fullscreen across every monitor
 *   MatrixRain.scr /p <hwnd>   draw inside the Screen Saver Settings preview
 *   MatrixRain.scr /c[:hwnd]   settings dialog (writes config.yaml)
 *   MatrixRain.scr /r out.bmp [ticks] [w] [h]   save a frame, no window (dev)
 *   (no arguments)             settings dialog
 *
 * The rain itself lives in engine/: this file only rasterizes the glyphs
 * with GDI once, and shows the engine's pixel buffer with SetDIBitsToDevice.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#include "../../engine/config.h"
#include "../../engine/paths.h"
#include "../../engine/rain.h"
#include "../../engine/render.h"

#define TIMER_ID 1

static rain_config g_cfg;
static char g_exe_dir[1024];
static char g_config_path[1024];

static rain_t *g_rain;
static rain_atlas *g_atlas;
static rain_renderer *g_rr;
static uint32_t *g_fb;
static int g_w, g_h, g_cw, g_ch; /* surface size, cell size */
static int g_preview;
static int g_full_redraw = 1;
static POINT g_start_mouse;   /* last point a real (non-jitter) move was counted at */
static int g_mouse_seen;
static DWORD g_move_streak_start, g_move_last; /* current run of continuous movement */
static int g_press_count;
static DWORD g_press_last;

#define WAKE_MOVE_GAP_MS 500   /* a pause this long ends a movement run */
#define WAKE_PRESS_GAP_MS 2000 /* presses further apart than this start over */

/* ---- setup helpers ---------------------------------------------------- */

static void set_dpi_awareness(void)
{
    typedef BOOL(WINAPI * SetCtx)(HANDLE);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    SetCtx f = u ? (SetCtx)(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    if (f) f((HANDLE)-4); /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
    else SetProcessDPIAware();
}

static int system_dpi(void)
{
    typedef UINT(WINAPI * GetDpi)(void);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    GetDpi f = u ? (GetDpi)(void *)GetProcAddress(u, "GetDpiForSystem") : NULL;
    int dpi;
    HDC dc;
    if (f) return (int)f();
    dc = GetDC(NULL);
    dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    return dpi > 0 ? dpi : 96;
}

static void find_exe_dir(void)
{
    wchar_t w[MAX_PATH];
    wchar_t *slash;
    DWORD n = GetModuleFileNameW(NULL, w, MAX_PATH);
    if (!n || n >= MAX_PATH) { g_exe_dir[0] = 0; return; }
    slash = wcsrchr(w, L'\\');
    if (slash) *slash = 0;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, g_exe_dir, (int)sizeof g_exe_dir, NULL, NULL);
}

/* Vendored fonts (rain_font_dirs), private to this process - nothing is
 * installed. */
static void register_vendored_fonts(void)
{
    char dirs[RAIN_MAX_FONT_DIRS][1024];
    int i, n = rain_font_dirs(g_exe_dir, dirs);
    for (i = 0; i < n; i++) {
        wchar_t wdir[MAX_PATH], pat[MAX_PATH + 4], file[MAX_PATH * 2];
        WIN32_FIND_DATAW fd;
        HANDLE h;
        if (!MultiByteToWideChar(CP_UTF8, 0, dirs[i], -1, wdir, MAX_PATH)) continue;
        _snwprintf(pat, MAX_PATH + 4, L"%ls\\*", wdir);
        pat[MAX_PATH + 3] = 0;
        h = FindFirstFileW(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            const wchar_t *ext = wcsrchr(fd.cFileName, L'.');
            if (!ext || (_wcsicmp(ext, L".ttf") && _wcsicmp(ext, L".ttc") && _wcsicmp(ext, L".otf"))) continue;
            _snwprintf(file, MAX_PATH * 2, L"%ls\\%ls", wdir, fd.cFileName);
            file[MAX_PATH * 2 - 1] = 0;
            if (AddFontResourceExW(file, FR_PRIVATE, 0) > 0) rain_log("font: registered %ls", file);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

static HFONT make_font(const wchar_t *face, int cell)
{
    /* Bold: MS Gothic regular is 1px strokes at these sizes, thinner than the
     * film's; the glow then softens the edges. */
    return CreateFontW(-cell, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, face);
}

/* Does the font selected into dc have a real glyph for this BMP char? */
static int has_glyph(HDC dc, wchar_t ch)
{
    WORD idx = 0xFFFF;
    if (GetGlyphIndicesW(dc, &ch, 1, &idx, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR) return 0;
    return idx != 0xFFFF;
}

/* Rasterize every glyph once (white on black, grayscale anti-aliasing) and
 * keep the green channel as coverage. Falls back to MS Gothic for any glyph
 * the configured font lacks. */
static rain_atlas *build_atlas(const rain_config *cfg, int cell_w, int cell)
{
    rain_atlas *a = atlas_create(cell_w, cell, cfg->glyph_count);
    BITMAPINFO bi;
    void *bits = NULL;
    HDC dc;
    HBITMAP bmp, oldbmp;
    HFONT primary, fallback, oldfont;
    wchar_t face[RAIN_FONT_NAME_MAX];
    TEXTMETRICW tm;
    int g;

    if (!a) return NULL;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = cell_w;
    bi.bmiHeader.biHeight = -cell; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dc = CreateCompatibleDC(NULL);
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dc || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        atlas_finish(a);
        return a;
    }
    oldbmp = (HBITMAP)SelectObject(dc, bmp);
    if (!MultiByteToWideChar(CP_UTF8, 0, cfg->font_name, -1, face, RAIN_FONT_NAME_MAX)) wcscpy(face, L"MS Gothic");
    primary = make_font(face, cell);
    fallback = make_font(L"MS Gothic", cell);
    /* A face this PC doesn't have (a config shared with a Mac says
     * "Hiragino Sans") makes GDI substitute something like Courier New:
     * draw everything in MS Gothic instead, not a mix of the two. */
    {
        wchar_t got[LF_FACESIZE];
        HFONT prev = (HFONT)SelectObject(dc, primary);
        if (!GetTextFaceW(dc, LF_FACESIZE, got) || _wcsicmp(got, face)) {
            rain_log("font: %s not installed, using MS Gothic", cfg->font_name);
            DeleteObject(primary);
            primary = make_font(L"MS Gothic", cell);
        }
        SelectObject(dc, prev);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));

    for (g = 0; g < cfg->glyph_count; g++) {
        uint32_t cp = cfg->glyphs[g];
        wchar_t text[2];
        int len, x, y, i;
        SIZE sz;
        uint8_t *mask = atlas_mask(a, g, 0);
        const uint32_t *px = (const uint32_t *)bits;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            text[0] = (wchar_t)(0xD800 + (cp >> 10));
            text[1] = (wchar_t)(0xDC00 + (cp & 0x3FF));
            len = 2;
        } else {
            text[0] = (wchar_t)cp;
            len = 1;
        }
        oldfont = (HFONT)SelectObject(dc, primary);
        if (len == 1 && !has_glyph(dc, text[0])) SelectObject(dc, fallback);
        GetTextMetricsW(dc, &tm);
        GetTextExtentPoint32W(dc, text, len, &sz);
        x = (cell_w - sz.cx) / 2;
        y = (cell - tm.tmHeight) / 2;
        memset(bits, 0, (size_t)cell_w * (size_t)cell * 4);
        TextOutW(dc, x, y, text, len);
        GdiFlush();
        for (i = 0; i < cell_w * cell; i++) mask[i] = (uint8_t)((px[i] >> 8) & 0xFF);
        SelectObject(dc, oldfont);
    }

    SelectObject(dc, oldbmp);
    DeleteObject(primary);
    DeleteObject(fallback);
    DeleteObject(bmp);
    DeleteDC(dc);
    atlas_finish(a);
    return a;
}

static int setup_surface(int w, int h)
{
    int cols, rows;
    uint32_t *fb;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    fb = (uint32_t *)realloc(g_fb, (size_t)w * (size_t)h * sizeof(uint32_t));
    if (!fb) return -1;
    g_fb = fb;
    g_w = w;
    g_h = h;
    rain_grid_for(w, h, g_cw, g_ch, &cols, &rows);
    if (!g_rain) {
        g_rain = rain_create(cols, rows, &g_cfg, GetTickCount() ^ (GetCurrentProcessId() << 16));
        if (!g_rain) return -1;
    } else {
        rain_resize(g_rain, cols, rows);
    }
    g_full_redraw = 1;
    rain_log("surface %dx%d cell=%dx%d cols=%d rows=%d", w, h, g_cw, g_ch, cols, rows);
    return 0;
}

/* ---- the rain window -------------------------------------------------- */

static void close_saver(HWND hwnd)
{
    if (!g_preview) PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

static LRESULT CALLBACK rain_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (LOWORD(lp) && HIWORD(lp) && (LOWORD(lp) != g_w || HIWORD(lp) != g_h))
            setup_surface(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_TIMER:
        if (g_rain && g_fb) {
            rain_tick(g_rain);
            renderer_draw(g_rr, g_rain, g_fb, g_w, g_h, g_w, g_full_redraw);
            g_full_redraw = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (g_fb) {
            BITMAPINFO bi;
            memset(&bi, 0, sizeof bi);
            bi.bmiHeader.biSize = sizeof bi.bmiHeader;
            bi.bmiHeader.biWidth = g_w;
            bi.bmiHeader.biHeight = -g_h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            SetDIBitsToDevice(dc, 0, 0, (DWORD)g_w, (DWORD)g_h, 0, 0, 0, (UINT)g_h, g_fb, &bi, DIB_RGB_COLORS);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETCURSOR:
        if (!g_preview) { SetCursor(NULL); return TRUE; }
        break;
    case WM_MOUSEMOVE:
        if (!g_preview) {
            POINT p;
            GetCursorPos(&p);
            if (!g_mouse_seen) { g_start_mouse = p; g_mouse_seen = 1; }
            else if (abs(p.x - g_start_mouse.x) > g_cfg.exit_mouse_pixels ||
                     abs(p.y - g_start_mouse.y) > g_cfg.exit_mouse_pixels) {
                /* A real move. Keep a run going while moves keep coming; wake
                 * once the run has lasted wake_mouse_ms. */
                DWORD now = GetTickCount();
                g_start_mouse = p;
                if (!g_move_streak_start || now - g_move_last > WAKE_MOVE_GAP_MS) g_move_streak_start = now;
                g_move_last = now;
                if (now - g_move_streak_start >= (DWORD)g_cfg.wake_mouse_ms) close_saver(hwnd);
            }
        }
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (lp & (1L << 30)) return 0; /* auto-repeat from a held key: counts once */
        /* fall through */
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        DWORD now = GetTickCount();
        if (!g_press_count || now - g_press_last > WAKE_PRESS_GAP_MS) g_press_count = 0;
        g_press_count++;
        g_press_last = now;
        if (g_press_count >= g_cfg.wake_keys) close_saver(hwnd);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int run_rain(HINSTANCE inst, HWND preview_parent)
{
    WNDCLASSW wc;
    HWND hwnd;
    MSG m;
    int x, y, w, h, dpi = system_dpi();

    g_preview = preview_parent != NULL;
    {
        int font_px = MulDiv(g_cfg.font_size, dpi, 96);
        if (g_preview) font_px = font_px / 2 < 8 ? 8 : font_px / 2; /* the preview box is tiny */
        rain_cell_size(&g_cfg, font_px, &g_cw, &g_ch);
    }
    g_atlas = build_atlas(&g_cfg, g_cw, g_ch);
    g_rr = g_atlas ? renderer_create(&g_cfg, g_atlas) : NULL;
    if (!g_rr) return 1;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = rain_proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"MatrixRainSaver";
    wc.hCursor = g_preview ? LoadCursor(NULL, IDC_ARROW) : NULL;
    RegisterClassW(&wc);

    if (g_preview) {
        RECT rc;
        GetClientRect(preview_parent, &rc);
        x = 0; y = 0;
        w = rc.right - rc.left; h = rc.bottom - rc.top;
        if (setup_surface(w, h) != 0) return 1;
        hwnd = CreateWindowExW(0, wc.lpszClassName, L"Matrix Rain", WS_CHILD | WS_VISIBLE,
                               x, y, w, h, preview_parent, NULL, inst, NULL);
    } else {
        /* One window over the whole virtual desktop = every monitor. */
        x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (setup_surface(w, h) != 0) return 1;
        hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Matrix Rain",
                               WS_POPUP | WS_VISIBLE, x, y, w, h, NULL, NULL, inst, NULL);
        while (ShowCursor(FALSE) >= 0) {}
        if (hwnd) SetForegroundWindow(hwnd);
    }
    if (!hwnd) { rain_log("CreateWindow failed %lu", GetLastError()); return 1; }

    SetTimer(hwnd, TIMER_ID, g_preview ? 60 : (UINT)g_cfg.speed_ms, NULL);
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (!g_preview) while (ShowCursor(TRUE) < 0) {}
    renderer_destroy(g_rr);
    atlas_destroy(g_atlas);
    rain_destroy(g_rain);
    free(g_fb);
    return 0;
}

/* ---- settings dialog -------------------------------------------------- */

enum { ID_SPEED = 101, ID_SIZE, ID_DENSITY };
static HWND g_trk_speed, g_trk_size, g_trk_density;

static void save_settings(void)
{
    char speed[16], size[16], density[16], path[1024];
    config_kv kv[3];
    wsprintfA(speed, "%d", (int)SendMessageW(g_trk_speed, TBM_GETPOS, 0, 0));
    wsprintfA(size, "%d", (int)SendMessageW(g_trk_size, TBM_GETPOS, 0, 0));
    wsprintfA(density, "%d", (int)SendMessageW(g_trk_density, TBM_GETPOS, 0, 0));
    kv[0].key = "fps"; kv[0].value = speed;
    kv[1].key = "font_size"; kv[1].value = size;
    kv[2].key = "density_percent"; kv[2].value = density;
    if (!rain_resolve_config(path, sizeof path, g_exe_dir) && !rain_user_config_path(path, sizeof path)) return;
    if (config_update_keys(path, &g_cfg, kv, 3) != 0) {
        MessageBoxW(NULL, L"Could not save config.yaml.", L"Matrix Rain", MB_ICONERROR);
        rain_log("settings save failed: %s", path);
    }
}

static LRESULT CALLBACK settings_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) { save_settings(); DestroyWindow(hwnd); return 0; }
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
        break;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND add_ctl(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id, HFONT font, int dpi)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             MulDiv(x, dpi, 96), MulDiv(y, dpi, 96), MulDiv(w, dpi, 96), MulDiv(h, dpi, 96),
                             parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (c && font) SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}

static HWND add_slider(HWND parent, int y, int w, int id, int lo, int hi, int freq, int val, HFONT font, int dpi)
{
    HWND t = add_ctl(parent, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS, 12, y, w, 32, id, font, dpi);
    SendMessageW(t, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    SendMessageW(t, TBM_SETTICFREQ, (WPARAM)freq, 0);
    SendMessageW(t, TBM_SETPOS, TRUE, val < lo ? lo : val > hi ? hi : val);
    return t;
}

static int run_settings(HINSTANCE inst, HWND owner)
{
    INITCOMMONCONTROLSEX icc;
    WNDCLASSW wc;
    NONCLIENTMETRICSW ncm;
    HFONT font = NULL;
    HWND hwnd;
    RECT rc;
    MSG m;
    int dpi = system_dpi();
    char path[1024];
    wchar_t wpath[1024], where[1100];

    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    memset(&ncm, 0, sizeof ncm);
    ncm.cbSize = sizeof ncm;
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0)) font = CreateFontIndirectW(&ncm.lfMessageFont);

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = settings_proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"MatrixRainSettings";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    RegisterClassW(&wc);

    rc.left = 0; rc.top = 0; rc.right = MulDiv(360, dpi, 96); rc.bottom = MulDiv(250, dpi, 96);
    AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"Matrix Rain \x2014 Settings",
                           WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                           rc.right - rc.left, rc.bottom - rc.top, owner, NULL, inst, NULL);
    if (!hwnd) return 1;

    add_ctl(hwnd, L"STATIC", L"Speed (frames per second, 24 = the film)", 0, 16, 12, 320, 18, 0, font, dpi);
    g_trk_speed = add_slider(hwnd, 30, 330, ID_SPEED, 7, 50, 1, g_cfg.fps, font, dpi);
    add_ctl(hwnd, L"STATIC", L"Glyph size", 0, 16, 70, 320, 18, 0, font, dpi);
    g_trk_size = add_slider(hwnd, 88, 330, ID_SIZE, 12, 32, 2, g_cfg.font_size, font, dpi);
    add_ctl(hwnd, L"STATIC", L"How many columns", 0, 16, 128, 320, 18, 0, font, dpi);
    g_trk_density = add_slider(hwnd, 146, 330, ID_DENSITY, 30, 100, 10, g_cfg.density, font, dpi);

    if (!rain_resolve_config(path, sizeof path, g_exe_dir) && !rain_user_config_path(path, sizeof path)) strcpy(path, "?");
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024);
    wsprintfW(where, L"Saved to %s \x2014 every other setting is in that file.", wpath);
    add_ctl(hwnd, L"STATIC", where, SS_PATHELLIPSIS, 16, 184, 330, 18, 0, font, dpi);
    add_ctl(hwnd, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 168, 212, 84, 26, IDOK, font, dpi);
    add_ctl(hwnd, L"BUTTON", L"Cancel", WS_TABSTOP, 260, 212, 84, 26, IDCANCEL, font, dpi);

    /* Centre on the owner, or the screen. */
    {
        RECT wr, pr;
        GetWindowRect(hwnd, &wr);
        if (!owner || !GetWindowRect(owner, &pr)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &pr, 0);
        SetWindowPos(hwnd, NULL, pr.left + ((pr.right - pr.left) - (wr.right - wr.left)) / 2,
                     pr.top + ((pr.bottom - pr.top) - (wr.bottom - wr.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(g_trk_speed);
    if (owner) EnableWindow(owner, FALSE);
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (IsDialogMessageW(hwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (owner) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
    if (font) DeleteObject(font);
    return 0;
}

/* ---- off-screen render (development) ---------------------------------- */

/* MatrixRain.scr /r out.bmp [ticks] [width] [height]: run the rain without a
 * window and save the last frame, for checking the look without taking
 * over the screen. */
static int run_render(const wchar_t *out, int ticks, int w, int h)
{
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    HANDLE f;
    DWORD n;
    int t;
    rain_cell_size(&g_cfg, g_cfg.font_size, &g_cw, &g_ch);
    g_atlas = build_atlas(&g_cfg, g_cw, g_ch);
    g_rr = g_atlas ? renderer_create(&g_cfg, g_atlas) : NULL;
    if (!g_rr || setup_surface(w, h) != 0) return 1;
    for (t = 0; t < ticks; t++) {
        rain_tick(g_rain);
        renderer_draw(g_rr, g_rain, g_fb, g_w, g_h, g_w, t == 0);
    }
    memset(&fh, 0, sizeof fh);
    memset(&ih, 0, sizeof ih);
    ih.biSize = sizeof ih;
    ih.biWidth = w;
    ih.biHeight = -h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + (DWORD)w * (DWORD)h * 4;
    f = CreateFileW(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 1;
    WriteFile(f, &fh, sizeof fh, &n, NULL);
    WriteFile(f, &ih, sizeof ih, &n, NULL);
    WriteFile(f, g_fb, (DWORD)w * (DWORD)h * 4, &n, NULL);
    CloseHandle(f);
    return 0;
}

/* ---- entry ------------------------------------------------------------ */

static HWND parse_hwnd(const wchar_t *s)
{
    return s && *s ? (HWND)(INT_PTR)_wcstoi64(s, NULL, 10) : NULL;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    int argc = 0, rc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t mode = 0;
    HWND target = NULL;
    (void)prev; (void)cmdline; (void)show;

    set_dpi_awareness();
    find_exe_dir();
    rain_load_config(&g_cfg, g_exe_dir, g_config_path, sizeof g_config_path);
    register_vendored_fonts();

    if (argv && argc > 1) {
        const wchar_t *a = argv[1];
        while (*a == L'/' || *a == L'-') a++;
        mode = (wchar_t)towlower(*a);
        if (a[0] && a[1] == L':') target = parse_hwnd(a + 2);   /* "/c:1234" */
        else if (argc > 2) target = parse_hwnd(argv[2]);         /* "/p 1234" */
    }
    rain_log("start mode=%c", mode ? (char)mode : '-');

    if (mode == L's') {
        /* Single instance: never stack rains on top of each other. */
        HANDLE mutex = CreateMutexW(NULL, TRUE, L"MatrixRainSaverSingleInstance");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            rain_log("already running, exiting");
        } else {
            rc = run_rain(inst, NULL);
        }
        if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    } else if (mode == L'r' && argc > 2) {
        int ticks = argc > 3 ? _wtoi(argv[3]) : 300;
        int w = argc > 4 ? _wtoi(argv[4]) : 1920, h = argc > 5 ? _wtoi(argv[5]) : 1080;
        rc = run_render(argv[2], ticks > 0 ? ticks : 300, w > 0 ? w : 1920, h > 0 ? h : 1080);
    } else if (mode == L'p' && target && IsWindow(target)) {
        rc = run_rain(inst, target);
    } else {
        rc = run_settings(inst, mode == L'c' ? target : NULL);
    }
    rain_log("exit rc=%d", rc);
    if (argv) LocalFree(argv);
    return rc;
}
