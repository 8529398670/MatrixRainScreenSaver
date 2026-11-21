/* paths.c - config/log locations and UTF-8 file access per OS. */
#include "paths.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define SEP '\\'
#define SEP_STR "\\"
#else
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define SEP '/'
#define SEP_STR "/"
#endif

#ifdef _WIN32
static wchar_t *to_wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w;
    if (n <= 0) return NULL;
    w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)n);
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static int env_utf8(const wchar_t *name, char *out, size_t n)
{
    DWORD len = GetEnvironmentVariableW(name, NULL, 0);
    wchar_t *w;
    int ok = 0;
    if (!len) return 0;
    w = (wchar_t *)malloc(sizeof(wchar_t) * len);
    if (!w) return 0;
    if (GetEnvironmentVariableW(name, w, len) && WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL) > 0)
        ok = out[0] != 0;
    free(w);
    return ok;
}
#else
static int env_utf8(const char *name, char *out, size_t n)
{
    const char *v = getenv(name);
    if (!v || !*v || strlen(v) + 1 > n) return 0;
    strcpy(out, v);
    return 1;
}
#endif

FILE *rain_fopen(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t *wp = to_wide(path), *wm = to_wide(mode);
    FILE *f = (wp && wm) ? _wfopen(wp, wm) : NULL;
    free(wp);
    free(wm);
    return f;
#else
    return fopen(path, mode);
#endif
}

int rain_file_exists(const char *path)
{
#ifdef _WIN32
    wchar_t *wp = to_wide(path);
    DWORD a = wp ? GetFileAttributesW(wp) : INVALID_FILE_ATTRIBUTES;
    free(wp);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static void make_dir(const char *dir)
{
#ifdef _WIN32
    wchar_t *w = to_wide(dir);
    if (w) { CreateDirectoryW(w, NULL); free(w); }
#else
    mkdir(dir, 0755);
#endif
}

int rain_mkdirs_for(const char *filepath)
{
    char buf[1024];
    size_t i, len = strlen(filepath);
    if (len >= sizeof buf) return -1;
    strcpy(buf, filepath);
    for (i = 1; i < len; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char save = buf[i];
#ifdef _WIN32
            if (i == 2 && buf[1] == ':') continue; /* skip "C:\" */
#endif
            buf[i] = 0;
            make_dir(buf);
            buf[i] = save;
        }
    }
    return 0;
}

static int join(char *out, size_t n, const char *a, const char *b)
{
    size_t la = strlen(a);
    int need_sep = la && a[la - 1] != '/' && a[la - 1] != '\\';
    if (la + (size_t)need_sep + strlen(b) + 1 > n) return 0;
    strcpy(out, a);
    if (need_sep) { out[la] = SEP; out[la + 1] = 0; }
    strcat(out, b);
    return 1;
}

static int user_dir(char *out, size_t n, int for_log)
{
    char base[1024];
#if defined(_WIN32)
    if (!env_utf8(for_log ? L"LOCALAPPDATA" : L"APPDATA", base, sizeof base)) return 0;
    return join(out, n, base, "MatrixRain");
#elif defined(__APPLE__)
    /* Under the macOS screensaver sandbox HOME is the legacyScreenSaver
     * container, so this lands in ~/Library/Containers/.../Data/Library. */
    if (!env_utf8("HOME", base, sizeof base)) return 0;
    return join(out, n, base, for_log ? "Library/Logs/MatrixRain" : "Library/Application Support/MatrixRain");
#else
    if (env_utf8(for_log ? "XDG_STATE_HOME" : "XDG_CONFIG_HOME", base, sizeof base))
        return join(out, n, base, "matrixrain");
    if (!env_utf8("HOME", base, sizeof base)) return 0;
    return join(out, n, base, for_log ? ".local/state/matrixrain" : ".config/matrixrain");
#endif
}

int rain_user_config_path(char *out, size_t n)
{
    char dir[1024];
    return user_dir(dir, sizeof dir, 0) && join(out, n, dir, "config.yaml");
}

const char *rain_resolve_config(char *out, size_t n, const char *exe_dir)
{
    char tmp[1024];
#ifdef _WIN32
    if (env_utf8(L"MATRIXRAIN_CONFIG", tmp, sizeof tmp) && rain_file_exists(tmp) && strlen(tmp) < n)
#else
    if (env_utf8("MATRIXRAIN_CONFIG", tmp, sizeof tmp) && rain_file_exists(tmp) && strlen(tmp) < n)
#endif
        return strcpy(out, tmp);
    if (exe_dir && *exe_dir && join(tmp, sizeof tmp, exe_dir, "config.yaml") && rain_file_exists(tmp) && strlen(tmp) < n)
        return strcpy(out, tmp);
    if (rain_user_config_path(tmp, sizeof tmp) && rain_file_exists(tmp) && strlen(tmp) < n)
        return strcpy(out, tmp);
    return NULL;
}

static int dir_exists(const char *path)
{
#ifdef _WIN32
    wchar_t *wp = to_wide(path);
    DWORD a = wp ? GetFileAttributesW(wp) : INVALID_FILE_ATTRIBUTES;
    free(wp);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

/* path -> its folder, in place (no separator = ""). */
static void strip_file(char *path)
{
    char *a = strrchr(path, '/'), *b = strrchr(path, '\\');
    char *s = a > b ? a : b;
    if (s) *s = 0; else path[0] = 0;
}

int rain_font_dirs(const char *exe_dir, char out[RAIN_MAX_FONT_DIRS][1024])
{
    char cfg[1024], dir[1024];
    int n = 0;
    if (rain_user_config_path(cfg, sizeof cfg)) {
        strip_file(cfg);
        if (join(dir, sizeof dir, cfg, "fonts") && dir_exists(dir)) strcpy(out[n++], dir);
    }
    if (rain_resolve_config(cfg, sizeof cfg, exe_dir)) {
#ifndef _WIN32
        char real[PATH_MAX];
        if (realpath(cfg, real) && strlen(real) < sizeof cfg) strcpy(cfg, real);
#endif
        strip_file(cfg);
        if (*cfg && join(dir, sizeof dir, cfg, "fonts" SEP_STR "vendor") &&
            dir_exists(dir) && (n == 0 || strcmp(dir, out[0])))
            strcpy(out[n++], dir);
    }
    return n;
}

static FILE *g_log;

static void open_log(void)
{
    char dir[1024], path[1100];
    if (g_log || !user_dir(dir, sizeof dir, 1)) return;
#ifdef _WIN32
    if (!join(path, sizeof path, dir, "MatrixRain.log")) return;
#else
    if (!join(path, sizeof path, dir, "matrixrain.log")) return;
#endif
    rain_mkdirs_for(path);
    g_log = rain_fopen(path, "ab");
}

void rain_log(const char *fmt, ...)
{
    va_list ap;
    time_t now;
    struct tm *tm;
    if (!g_log) return;
    now = time(NULL);
    tm = localtime(&now);
    if (tm) fprintf(g_log, "%04d-%02d-%02d %02d:%02d:%02d ", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

void rain_load_config(rain_config *cfg, const char *exe_dir, char *loaded_from, size_t n)
{
    char path[1024];
    config_defaults(cfg);
    if (rain_resolve_config(path, sizeof path, exe_dir)) {
        config_load_file(cfg, path);
    } else if (rain_user_config_path(path, sizeof path)) {
        config_write_default(path, cfg);
    } else {
        strcpy(path, "(defaults)");
    }
    if (loaded_from && n) {
        strncpy(loaded_from, path, n - 1);
        loaded_from[n - 1] = 0;
    }
    if (cfg->debug_log) {
        open_log();
        rain_log("config loaded from %s speed=%d font=%d glyphs=%d", path, cfg->speed_ms, cfg->font_size, cfg->glyph_count);
    }
}
