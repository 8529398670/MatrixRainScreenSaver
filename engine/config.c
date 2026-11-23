/* config.c - config.yaml reader/writer. No external library, no locale
 * dependence (numbers are always parsed and printed with a '.' decimal). */
#include "config.h"
#include "paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The bundled open font (fonts/open/), the same on every OS. Where it
 * can't be loaded each OS falls back to its own Japanese font. */
#define RAIN_DEFAULT_FONT "BIZ UDGothic"

/* Built-in glyph set (used when config.yaml has no `glyphs:` line): all
 * 56 half-width katakana U+FF66-U+FF9D, digits, Z and symbols. */
static int default_glyphs(uint32_t *out)
{
    static const char extras[] = "0123456789Z*+-:=.<>\"/\\|";
    int n = 0;
    uint32_t cp;
    size_t i;
    for (cp = 0xFF66; cp <= 0xFF9D; cp++) out[n++] = cp;
    for (i = 0; extras[i]; i++) out[n++] = (uint32_t)(unsigned char)extras[i];
    return n;
}

/* Built-in themes, always available (a config.yaml `themes:` block adds to
 * them, or redefines one by using its name). */
static const rain_theme builtin_themes[] = {
    /* the film's green on black */
    { "default", 0xE8FFE8, 0x00FF41, 0x00280A, 0x000000, { "" }, 0, { 0 }, 0 },
    /* Wright State: Green #046A38 rain, Gold #CBA052 heads on Black #101820,
     * fading to Dark Green #00482B */
    { "wsu-green", 0xCBA052, 0x046A38, 0x00482B, 0x101820, { "" }, 0, { 0 }, 0 },
    /* Wright State: Gold #CBA052 rain, Yellow #F8E08E heads on Black #101820,
     * fading to Deep Gold #AE8643 */
    { "wsu-gold", 0xF8E08E, 0xCBA052, 0xAE8643, 0x101820, { "" }, 0, { 0 }, 0 },
    /* Wright State: green and gold strings mixed on Black */
    { "wsu", 0xCBA052, 0x046A38, 0x00482B, 0x101820, { "wsu-green", "wsu-gold" }, 2, { 0 }, 0 },
};

void config_defaults(rain_config *c)
{
    memset(c, 0, sizeof *c);
    c->fps = 24;      /* one tick = one film frame */
    c->speed_ms = 42; /* 1000 / 24, rounded */
    strcpy(c->font_name, RAIN_DEFAULT_FONT);
    c->font_size = 20;
    c->cell_width_percent = 75;
    c->density = 90;
    c->trail_min = 5;
    c->trail_max = 25;
    c->fall_min = 0.5;
    c->fall_max = 1.0;
    c->flip_percent = 100;
    c->mutate_every_ticks = 4;
    c->changing_percent = 40;
    c->cursor_percent = 20;
    c->trail_fade = 0;
    c->glow_percent = 60;
    c->head_color = 0xE8FFE8;
    c->rain_color = 0x00FF41;
    c->tail_color = 0x00280A;
    c->background_color = 0x000000;
    c->theme_count = (int)(sizeof builtin_themes / sizeof builtin_themes[0]);
    memcpy(c->themes, builtin_themes, sizeof builtin_themes);
    c->exit_mouse_pixels = 5;
    c->wake_mouse_ms = 0;
    c->wake_keys = 1;
    c->debug_log = 0;
    c->glyph_count = default_glyphs(c->glyphs);
    c->glyphs_overridden = 0;
}

/* ---- UTF-8 ------------------------------------------------------------ */

int utf8_decode(const char *s, uint32_t *out, int max)
{
    const unsigned char *p = (const unsigned char *)s;
    int n = 0;
    while (*p && n < max) {
        uint32_t cp;
        int extra, i;
        if (*p < 0x80) { cp = *p; extra = 0; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; extra = 1; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; extra = 2; }
        else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; extra = 3; }
        else { p++; continue; }
        p++;
        for (i = 0; i < extra; i++) {
            if ((*p & 0xC0) != 0x80) break;
            cp = (cp << 6) | (*p & 0x3F);
            p++;
        }
        if (i < extra) continue; /* truncated sequence: drop it */
        out[n++] = cp;
    }
    return n;
}

int utf8_encode(uint32_t cp, char *o)
{
    if (cp < 0x80) { o[0] = (char)cp; o[1] = 0; return 1; }
    if (cp < 0x800) {
        o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); o[2] = 0; return 2;
    }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (char)(0x80 | (cp & 0x3F)); o[3] = 0; return 3;
    }
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F)); o[4] = 0;
    return 4;
}

/* ---- small string helpers -------------------------------------------- */

static int is_space(char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }
static char lower(char ch) { return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch; }

static char *trim(char *s)
{
    char *e;
    while (is_space(*s)) s++;
    e = s + strlen(s);
    while (e > s && is_space(e[-1])) *--e = 0;
    return s;
}

static int ieq(const char *a, const char *b)
{
    while (*a && *b) { if (lower(*a) != lower(*b)) return 0; a++; b++; }
    return *a == *b;
}

static char *unquote(char *v)
{
    size_t n = strlen(v);
    if (n >= 2 && ((v[0] == '"' && v[n - 1] == '"') || (v[0] == '\'' && v[n - 1] == '\''))) {
        v[n - 1] = 0;
        return v + 1;
    }
    return v;
}

/* Strict integer: optional sign, digits, nothing else. */
static int parse_int(const char *s, int *out)
{
    long long v = 0;
    int neg = 0, digits = 0;
    if (*s == '+' || *s == '-') neg = (*s++ == '-');
    while (*s >= '0' && *s <= '9') {
        if (v < 1000000000LL) v = v * 10 + (*s - '0');
        s++; digits++;
    }
    if (!digits || *s) return 0;
    *out = (int)(neg ? -v : v);
    return 1;
}

/* Locale-independent decimal: [sign] digits [. digits] [e[sign]digits]. */
static int parse_double(const char *s, double *out)
{
    double v = 0, scale = 1;
    int neg = 0, digits = 0, ex = 0, exneg = 0, exdigits = 0;
    if (*s == '+' || *s == '-') neg = (*s++ == '-');
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); digits++; }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { scale /= 10; v += (*s++ - '0') * scale; digits++; }
    }
    if (!digits) return 0;
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '+' || *s == '-') exneg = (*s++ == '-');
        while (*s >= '0' && *s <= '9') { if (ex < 300) ex = ex * 10 + (*s - '0'); s++; exdigits++; }
        if (!exdigits) return 0;
        while (ex-- > 0) v = exneg ? v / 10 : v * 10;
    }
    if (*s) return 0;
    *out = neg ? -v : v;
    return 1;
}

static int clamp_int(const char *v, int lo, int hi, int def)
{
    int n;
    if (!parse_int(v, &n)) return def;
    return n < lo ? lo : n > hi ? hi : n;
}

static int parse_bool(const char *v, int def)
{
    if (ieq(v, "true") || ieq(v, "yes") || ieq(v, "on") || !strcmp(v, "1")) return 1;
    if (ieq(v, "false") || ieq(v, "no") || ieq(v, "off") || !strcmp(v, "0")) return 0;
    return def;
}

static double clamp_double(const char *v, double lo, double hi, double def)
{
    double n;
    if (!parse_double(v, &n)) return def;
    return n < lo ? lo : n > hi ? hi : n;
}

static int hexval(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    ch = lower(ch);
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static uint32_t parse_color(const char *v, uint32_t def)
{
    static const struct { const char *name; uint32_t rgb; } named[] = {
        { "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 },
        { "green", 0x008000 }, { "lime", 0x00FF00 }, { "blue", 0x0000FF },
        { "yellow", 0xFFFF00 }, { "cyan", 0x00FFFF }, { "aqua", 0x00FFFF },
        { "magenta", 0xFF00FF }, { "fuchsia", 0xFF00FF }, { "gray", 0x808080 },
        { "grey", 0x808080 }, { "orange", 0xFFA500 }, { "darkgreen", 0x006400 },
        { "limegreen", 0x32CD32 }, { "springgreen", 0x00FF7F },
    };
    const char *s = v;
    size_t i;
    if (*s == '#') s++;
    if (strlen(s) == 6) {
        uint32_t rgb = 0;
        for (i = 0; i < 6; i++) {
            int h = hexval(s[i]);
            if (h < 0) return def;
            rgb = (rgb << 4) | (uint32_t)h;
        }
        return rgb;
    }
    for (i = 0; i < sizeof named / sizeof named[0]; i++)
        if (ieq(v, named[i].name)) return named[i].rgb;
    return def;
}

/* ---- themes ----------------------------------------------------------- */

int config_find_theme(const rain_config *c, const char *name)
{
    int i;
    for (i = 0; i < c->theme_count; i++)
        if (ieq(name, c->themes[i].name)) return i;
    return -1;
}

rain_palette config_palette(const rain_config *c, int i)
{
    rain_palette p;
    if (i <= 0 || i >= c->palette_count) {
        p.head = c->head_color; p.rain = c->rain_color; p.tail = c->tail_color;
        return p;
    }
    return c->palettes[i];
}

static int gcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }

/* A mix theme's weights for its usable names (w[i] < 0 = not given) ->
 * whole-number odds. Names without a weight share what is left of 1
 * equally (or, with nothing left, get the average of the given ones);
 * with no weights at all every one is equal. Reduced by their common
 * divisor, so 0.5/0.5 picks exactly like 1/1. A weight of 0 never comes
 * up, unless every one is 0. */
static void resolve_weights(const double *w, int *out, int n)
{
    int i, set = 0, g = 0, any = 0;
    double given = 0, share;
    for (i = 0; i < n; i++) if (w[i] >= 0) { given += w[i]; set++; }
    if (set == 0) share = 1;
    else if (set == n) share = 0; /* unused */
    else if (given < 1) share = (1 - given) / (n - set);
    else share = given / set;
    for (i = 0; i < n; i++) {
        double v = w[i] >= 0 ? w[i] : share;
        out[i] = (int)(v * 1000 + 0.5);
        if (out[i] > 0) any = 1;
    }
    if (!any) for (i = 0; i < n; i++) out[i] = 1;
    for (i = 0; i < n; i++) g = gcd(g, out[i]);
    if (g > 1) for (i = 0; i < n; i++) out[i] /= g;
}

int config_pick_palette(const rain_config *c, uint32_t roll)
{
    int i, total = 0;
    if (c->palette_count <= 1) return 0;
    for (i = 0; i < c->palette_count; i++) total += c->palette_weights[i];
    if (total <= 0) return 0;
    roll %= (uint32_t)total;
    for (i = 0; i < c->palette_count; i++) {
        int w = c->palette_weights[i];
        if (roll < (uint32_t)w) return i;
        roll -= (uint32_t)w;
    }
    return 0;
}

int config_apply_theme(rain_config *c, const char *name)
{
    int i = config_find_theme(c, name), k, n = 0;
    double w[RAIN_MAX_MIX];
    const rain_theme *t;
    if (i < 0) return -1;
    t = &c->themes[i];
    memcpy(c->theme, t->name, sizeof c->theme);
    /* A mix: every named plain theme becomes one string colour set (a
     * name that is missing, or itself a mix, is skipped). */
    for (k = 0; k < t->mix_count && n < RAIN_MAX_MIX; k++) {
        int j = config_find_theme(c, t->mix[k]);
        const rain_theme *m;
        if (j < 0 || c->themes[j].mix_count) continue;
        m = &c->themes[j];
        if (n == 0) c->background_color = m->background;
        c->palettes[n].head = m->head;
        c->palettes[n].rain = m->rain;
        c->palettes[n].tail = m->tail;
        w[n] = (t->mix_weighted >> k & 1) ? t->mix_weight[k] : -1;
        n++;
    }
    if (n > 0) resolve_weights(w, c->palette_weights, n);
    if (n == 0) { /* plain theme, or a mix with nothing usable */
        c->palettes[0].head = t->head;
        c->palettes[0].rain = t->rain;
        c->palettes[0].tail = t->tail;
        c->background_color = t->background;
        c->palette_weights[0] = 1;
        n = 1;
    }
    c->palette_count = n;
    c->head_color = c->palettes[0].head;
    c->rain_color = c->palettes[0].rain;
    c->tail_color = c->palettes[0].tail;
    return 0;
}

/* Split a `a, b` / `[a, "b"]` list into lower-case words. */
static int split_list(const char *val, char out[][RAIN_THEME_NAME_MAX], int max)
{
    char word[RAIN_THEME_NAME_MAX];
    int n = 0, count = 0;
    for (;; val++) {
        char ch = *val;
        if (ch && ch != ',' && ch != ' ' && ch != '[' && ch != ']' && ch != '"' && ch != '\'' && ch != '\t') {
            if (n < RAIN_THEME_NAME_MAX - 1) word[n++] = lower(ch);
            continue;
        }
        if (n && count < max) {
            word[n] = 0;
            memcpy(out[count++], word, n + 1);
        }
        n = 0;
        if (!ch) break;
    }
    return count;
}

/* `mix: a, b` (brackets and quotes allowed) -> the theme's mix list. */
static void parse_mix(rain_theme *t, const char *val)
{
    t->mix_count = split_list(val, t->mix, RAIN_MAX_MIX);
}

/* `    wsu-green: 0.75` inside a theme: add (or re-weigh) a mixed theme.
 * An empty value (`    wsu-gold:`) adds it with no share of its own, so
 * it gets an equal part of what the others leave. Returns 0 if val is
 * not a number, so it is not a mix entry. */
static int parse_mix_entry(rain_theme *t, const char *key, const char *val)
{
    char *end = (char *)val;
    double v = *val ? strtod(val, &end) : 0;
    int k;
    size_t n = strlen(key);
    if ((*val && end == val) || n == 0 || n >= RAIN_THEME_NAME_MAX) return 0;
    for (k = 0; k < t->mix_count; k++)
        if (ieq(key, t->mix[k])) break;
    if (k == t->mix_count) {
        if (k >= RAIN_MAX_MIX) return 1;
        memcpy(t->mix[k], key, n + 1);
        t->mix_count++;
    }
    t->mix_weight[k] = v > 0 ? (v > 1000 ? 1000 : v) : 0;
    if (*val) t->mix_weighted |= 1 << k;
    else t->mix_weighted &= ~(1 << k);
    return 1;
}

/* `  name:` inside `themes:` - find it, or add it starting from the film's
 * colours so a theme that sets only some of them still looks sane. */
static rain_theme *theme_slot(rain_config *c, const char *name)
{
    int i = config_find_theme(c, name);
    rain_theme *t;
    size_t n = strlen(name);
    if (i >= 0) return &c->themes[i];
    if (c->theme_count >= RAIN_MAX_THEMES || n == 0 || n >= RAIN_THEME_NAME_MAX) return NULL;
    t = &c->themes[c->theme_count++];
    *t = builtin_themes[0];
    for (i = 0; i <= (int)n; i++) t->name[i] = lower(name[i]);
    return t;
}

/* One `    key: value` line of a theme block. *fresh is set when the
 * block opens, so its first mix entry replaces an inherited mix list. */
static void apply_theme_field(rain_theme *t, const char *key, const char *val, int *fresh)
{
    if (!strcmp(key, "head") || !strcmp(key, "head_color")) t->head = parse_color(val, t->head);
    else if (!strcmp(key, "rain") || !strcmp(key, "rain_color")) t->rain = parse_color(val, t->rain);
    else if (!strcmp(key, "tail") || !strcmp(key, "tail_color")) t->tail = parse_color(val, t->tail);
    else if (!strcmp(key, "background") || !strcmp(key, "background_color")) t->background = parse_color(val, t->background);
    else if (!strcmp(key, "mix")) { parse_mix(t, val); t->mix_weighted = 0; *fresh = 0; } /* old list form */
    else {
        rain_theme keep = *t;
        if (*fresh) { t->mix_count = 0; t->mix_weighted = 0; }
        if (parse_mix_entry(t, key, val)) *fresh = 0;
        else *t = keep; /* not a number: an unknown key, ignored */
    }
}

/* ---- parse ------------------------------------------------------------ */

static void apply(rain_config *c, const char *key, const char *val)
{
    if (!strcmp(key, "fps")) {
        c->fps = clamp_int(val, 7, 50, c->fps);
        c->speed_ms = (1000 + c->fps / 2) / c->fps;
        c->fps_given = 1;
    }
    else if (!strcmp(key, "speed_ms")) {
        /* The old key (milliseconds per tick); an fps: line wins over it. */
        if (!c->fps_given) {
            c->speed_ms = clamp_int(val, 20, 150, c->speed_ms);
            c->fps = (1000 + c->speed_ms / 2) / c->speed_ms;
        }
    }
    else if (!strcmp(key, "font_name")) {
        if (*val) { strncpy(c->font_name, val, RAIN_FONT_NAME_MAX - 1); c->font_name[RAIN_FONT_NAME_MAX - 1] = 0; }
    }
    else if (!strcmp(key, "font_size")) c->font_size = clamp_int(val, 12, 32, c->font_size);
    else if (!strcmp(key, "density_percent")) c->density = clamp_int(val, 30, 100, c->density);
    else if (!strcmp(key, "trail_min")) c->trail_min = clamp_int(val, 1, 60, c->trail_min);
    else if (!strcmp(key, "trail_max")) c->trail_max = clamp_int(val, 1, 60, c->trail_max);
    else if (!strcmp(key, "fall_speed_min")) c->fall_min = clamp_double(val, 0.05, 3.0, c->fall_min);
    else if (!strcmp(key, "fall_speed_max")) c->fall_max = clamp_double(val, 0.05, 3.0, c->fall_max);
    else if (!strcmp(key, "flip_percent")) c->flip_percent = clamp_int(val, 0, 100, c->flip_percent);
    else if (!strcmp(key, "mutate_every_ticks")) c->mutate_every_ticks = clamp_int(val, 1, 60, c->mutate_every_ticks);
    else if (!strcmp(key, "cursor_percent")) c->cursor_percent = clamp_int(val, 0, 100, c->cursor_percent);
    else if (!strcmp(key, "theme")) {
        if (*val && strlen(val) < sizeof c->theme) strcpy(c->theme, val);
    }
    else if (!strcmp(key, "head_color")) c->head_color = parse_color(val, c->head_color);
    else if (!strcmp(key, "rain_color")) c->rain_color = parse_color(val, c->rain_color);
    else if (!strcmp(key, "tail_color")) c->tail_color = parse_color(val, c->tail_color);
    else if (!strcmp(key, "background_color")) c->background_color = parse_color(val, c->background_color);
    else if (!strcmp(key, "exit_mouse_pixels")) c->exit_mouse_pixels = clamp_int(val, 1, 100, c->exit_mouse_pixels);
    else if (!strcmp(key, "wake_mouse_ms")) c->wake_mouse_ms = clamp_int(val, 0, 10000, c->wake_mouse_ms);
    else if (!strcmp(key, "wake_keys")) c->wake_keys = clamp_int(val, 1, 50, c->wake_keys);
    else if (!strcmp(key, "cell_width_percent")) c->cell_width_percent = clamp_int(val, 40, 100, c->cell_width_percent);
    else if (!strcmp(key, "changing_percent")) c->changing_percent = clamp_int(val, 0, 100, c->changing_percent);
    else if (!strcmp(key, "glow_percent")) c->glow_percent = clamp_int(val, 0, 100, c->glow_percent);
    else if (!strcmp(key, "trail_fade")) c->trail_fade = parse_bool(val, c->trail_fade);
    else if (!strcmp(key, "debug_log")) c->debug_log = parse_bool(val, c->debug_log);
    else if (!strcmp(key, "glyphs")) {
        if (*val) {
            int n = utf8_decode(val, c->glyphs, RAIN_MAX_GLYPHS);
            if (n > 0) { c->glyph_count = n; c->glyphs_overridden = 1; }
        }
    }
}

void config_parse(rain_config *c, const char *text)
{
    const char *p = text;
    int in_themes = 0;           /* inside the top-level `themes:` block */
    rain_theme *cur = NULL;      /* the theme whose colours are being read */
    int fresh = 0;               /* cur's block has no mix entry yet */
    int cur_indent = 0;          /* indent of cur's `  name:` line */
    while (*p) {
        int indented, indent = 0;
        const char *q;
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char *buf = (char *)malloc(len + 1), *line, *colon, *key, *val, *k;
        if (!buf) return;
        memcpy(buf, p, len);
        buf[len] = 0;
        p += len + (eol ? 1 : 0);

        indented = buf[0] == ' ' || buf[0] == '\t';
        for (q = buf; *q == ' ' || *q == '\t'; q++) indent += *q == '\t' ? 4 : 1;
        line = trim(buf);
        if (!*line || *line == '#') { free(buf); continue; }
        colon = strchr(line, ':');
        if (!colon || colon == line) { free(buf); continue; }
        *colon = 0;
        key = trim(line);
        for (k = key; *k; k++) *k = lower(*k);
        val = trim(colon + 1);
        /* Trailing " # comment" - but a value that IS a colour starts with #. */
        if (*val != '#') {
            char *hash = strstr(val, " #");
            if (hash) { *hash = 0; val = trim(val); }
        }
        val = unquote(val);
        if (!indented) {
            in_themes = !strcmp(key, "themes") && !*val;
            cur = NULL;
            if (!in_themes) apply(c, key, val);
        } else if (in_themes) {
            /* "  wsu:" opens a theme; deeper lines are its fields, and a
             * deeper "    wsu-gold:" with no value is a mix entry. */
            if (!*val && (!cur || indent <= cur_indent)) {
                cur = theme_slot(c, key);
                fresh = 1;
                cur_indent = indent;
            }
            else if (cur) apply_theme_field(cur, key, val, &fresh); /* "    rain: ..." */
        }
        free(buf);
    }
    if (c->trail_max < c->trail_min) c->trail_max = c->trail_min;
    if (c->fall_max < c->fall_min) c->fall_max = c->fall_min;
    /* An unknown theme name is ignored: the colours stay as they were. */
    if (c->theme[0] && config_apply_theme(c, c->theme) != 0) c->theme[0] = 0;
}

static char *read_all(const char *path, size_t *out_len)
{
    FILE *f = rain_fopen(path, "rb");
    char *buf = NULL;
    size_t cap = 0, len = 0, got;
    if (!f) return NULL;
    for (;;) {
        if (len + 4096 + 1 > cap) {
            char *nb;
            cap = cap ? cap * 2 : 8192;
            nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); fclose(f); return NULL; }
            buf = nb;
        }
        got = fread(buf + len, 1, 4096, f);
        len += got;
        if (got < 4096) break;
    }
    fclose(f);
    buf[len] = 0;
    if (out_len) *out_len = len;
    return buf;
}

int config_load_file(rain_config *c, const char *path)
{
    char *text = read_all(path, NULL);
    const char *t;
    if (!text) return -1;
    t = text;
    if ((unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF) t += 3;
    config_parse(c, t);
    free(text);
    return 0;
}

/* ---- write ------------------------------------------------------------ */

typedef struct { char *s; size_t len, cap; } sbuf;

static void sb_add(sbuf *b, const char *s)
{
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        char *ns;
        while (nc < b->len + n + 1) nc *= 2;
        ns = (char *)realloc(b->s, nc);
        if (!ns) return;
        b->s = ns; b->cap = nc;
    }
    memcpy(b->s + b->len, s, n + 1);
    b->len += n;
}

static void sb_line(sbuf *b, const char *s) { sb_add(b, s); sb_add(b, "\n"); }

static void sb_kv_int(sbuf *b, const char *k, int v)
{
    char tmp[64];
    sprintf(tmp, "%s: %d", k, v);
    sb_line(b, tmp);
}

/* Locale-independent "0.3" style output, up to 3 decimals. */
static void fmt_double(double v, char *out)
{
    long long milli = (long long)(v * 1000.0 + (v < 0 ? -0.5 : 0.5));
    long long ip = milli / 1000, fp = milli % 1000;
    if (fp < 0) fp = -fp;
    if (fp == 0) sprintf(out, "%lld.0", ip);
    else if (fp % 100 == 0) sprintf(out, "%lld.%lld", ip, fp / 100);
    else if (fp % 10 == 0) sprintf(out, "%lld.%02lld", ip, fp / 10);
    else sprintf(out, "%lld.%03lld", ip, fp);
}

static void sb_kv_color(sbuf *b, const char *k, uint32_t rgb)
{
    char tmp[64];
    sprintf(tmp, "%s: \"#%02X%02X%02X\"", k, (unsigned)(rgb >> 16) & 255, (unsigned)(rgb >> 8) & 255, (unsigned)rgb & 255);
    sb_line(b, tmp);
}

/* The `themes:` block (written last in the file). */
static void sb_theme_block(sbuf *b, const rain_config *c)
{
    char tmp[256];
    int i;
    sb_line(b, "# ---- themes ----------------------------------------------------------");
    sb_line(b, "# themes: each one sets four #RRGGBB colours -");
    sb_line(b, "#   head       the bright leading glyph");
    sb_line(b, "#   rain       the main colour of the trail");
    sb_line(b, "#   tail       the dark end a trail fades to (with trail_fade: true)");
    sb_line(b, "#   background behind the rain");
    sb_line(b, "# or, instead of colours, mixes other themes, each with its share of the");
    sb_line(b, "# falling strings (background from the first), e.g.");
    sb_line(b, "#     wsu-green: 0.75");
    sb_line(b, "#     wsu-gold: 0.25");
    sb_line(b, "# Leave a share empty (`wsu-gold:`) and it gets what the others leave.");
    sb_line(b, "# Add your own: copy a block, give it a new name, and set theme: (at the");
    sb_line(b, "# top of the file) to that name.");
    sb_line(b, "# Keep the indents (2 spaces for the name, 4 for its colours).");
    sb_line(b, "themes:");
    for (i = 0; i < c->theme_count; i++) {
        const rain_theme *t = &c->themes[i];
        if (!strcmp(t->name, "default")) sb_line(b, "  # the film's green on black");
        else if (!strcmp(t->name, "wsu-green")) sb_line(b, "  # Wright State: Green rain, Gold heads, on Black");
        else if (!strcmp(t->name, "wsu-gold")) sb_line(b, "  # Wright State: Gold rain, Yellow heads, on Black");
        else if (!strcmp(t->name, "wsu")) sb_line(b, "  # Wright State: green and gold strings mixed");
        sprintf(tmp, "  %s:", t->name);
        sb_line(b, tmp);
        if (t->mix_count) {
            int k;
            for (k = 0; k < t->mix_count; k++) {
                if (t->mix_weighted >> k & 1) sprintf(tmp, "    %s: %g", t->mix[k], t->mix_weight[k]);
                else if (t->mix_weighted) sprintf(tmp, "    %s:", t->mix[k]); /* gets the rest */
                else sprintf(tmp, "    %s: %g", t->mix[k], 1.0 / t->mix_count);
                sb_line(b, tmp);
            }
            continue;
        }
        sb_kv_color(b, "    head", t->head);
        sb_kv_color(b, "    rain", t->rain);
        sb_kv_color(b, "    tail", t->tail);
        sb_kv_color(b, "    background", t->background);
    }
}

char *config_default_text(const rain_config *c)
{
    sbuf b = { 0 };
    char tmp[256], d[32];
    sb_line(&b, "# Matrix Rain screensaver - settings");
    sb_line(&b, "# Edit this file, save, and the next time the rain runs it picks it up.");
    sb_line(&b, "# No restart or reinstall needed. A bad line is ignored, the rest still applies.");
    sb_line(&b, "");
    sb_line(&b, "# ---- colours ---------------------------------------------------------");
    sb_line(&b, "# theme: change just this one word to swap every colour. Built in:");
    sb_line(&b, "# default (the film's green), wsu-green, wsu-gold, wsu (both mixed). The themes are listed,");
    sb_line(&b, "# and you can add your own, in the `themes:` block at the end of the file.");
    sprintf(tmp, "theme: %s", c->theme[0] ? c->theme : "default");
    sb_line(&b, tmp);
    sb_line(&b, "");
    sb_line(&b, "# fps: how fast the rain moves, in frames per second (higher = faster).");
    sb_line(&b, "# 24 = one tick per film frame, as in the film. 7-50.");
    sb_kv_int(&b, "fps", c->fps);
    sb_line(&b, "# font_name: a font family with half-width katakana. Bundled with the");
    sb_line(&b, "# saver (fonts/open/): \"BIZ UDGothic\", \"Noto Sans JP\", \"IPAGothic\".");
    sb_line(&b, "# Or one the computer has: \"MS Gothic\" (Windows), \"Hiragino Sans\" (Mac).");
    sb_line(&b, "# Glyphs are always drawn bold; don't add \"bold\" to the name.");
    sprintf(tmp, "font_name: \"%s\"", c->font_name);
    sb_line(&b, tmp);
    sb_line(&b, "# font_size: glyph size in pixels (scaled for high-DPI screens). 12-32.");
    sb_kv_int(&b, "font_size", c->font_size);
    sb_line(&b, "# cell_width_percent: column width as a % of font_size. The film's glyphs");
    sb_line(&b, "# are taller than wide; 100 = square cells. 40-100.");
    sb_kv_int(&b, "cell_width_percent", c->cell_width_percent);
    sb_line(&b, "# density_percent: how many columns are filled. 30-100.");
    sb_kv_int(&b, "density_percent", c->density);
    sb_line(&b, "# trail_min / trail_max: how long each falling column is, in glyphs.");
    sb_kv_int(&b, "trail_min", c->trail_min);
    sb_kv_int(&b, "trail_max", c->trail_max);
    sb_line(&b, "# trail_fade: false = trails stay one brightness until the invisible");
    sb_line(&b, "# deletion string behind them erases them (the 1999 film); true = they");
    sb_line(&b, "# dim from the theme's rain to tail colour and time out (the sequels' look).");
    sb_line(&b, c->trail_fade ? "trail_fade: true" : "trail_fade: false");
    sb_line(&b, "# fall_speed_min / fall_speed_max: rows each string grows per tick.");
    sb_line(&b, "# Newton's frame counts suggest about one row per film frame; at");
    sb_line(&b, "# fps 24 that is 1.0. Not verified against the footage.");
    fmt_double(c->fall_min, d); sprintf(tmp, "fall_speed_min: %s", d); sb_line(&b, tmp);
    fmt_double(c->fall_max, d); sprintf(tmp, "fall_speed_max: %s", d); sb_line(&b, tmp);
    sb_line(&b, "# flip_percent: chance each katakana is mirrored. The film mirrors all");
    sb_line(&b, "# of them (100). Digits always follow the film: 2 5 9 mirrored, 3 6");
    sb_line(&b, "# upside down, the rest as drawn. 0-100.");
    sb_kv_int(&b, "flip_percent", c->flip_percent);
    sb_line(&b, "# mutate_every_ticks: every N ticks ALL changing glyphs swap at once");
    sb_line(&b, "# (the film's synchronized shimmer), with a one-frame cross-dissolve.");
    sb_line(&b, "# 4 = hold 3 frames + 1 overlap frame, as in the film.");
    sb_kv_int(&b, "mutate_every_ticks", c->mutate_every_ticks);
    sb_line(&b, "# changing_percent: chance a string's glyphs keep changing; the others");
    sb_line(&b, "# stay still. The film's share is not measured. 0-100.");
    sb_kv_int(&b, "changing_percent", c->changing_percent);
    sb_line(&b, "# glow_percent: strength of the soft glow around lit glyphs. 0 = none");
    sb_line(&b, "# (also the fastest). 0-100.");
    sb_kv_int(&b, "glow_percent", c->glow_percent);
    sb_line(&b, "# cursor_percent: chance a string gets a bright leading glyph (the");
    sb_line(&b, "# film has about 1 in 5). 0-100.");
    sb_kv_int(&b, "cursor_percent", c->cursor_percent);
    sb_line(&b, "# exit_mouse_pixels: how far the mouse must move to wake the screen.");
    sb_kv_int(&b, "exit_mouse_pixels", c->exit_mouse_pixels);
    sb_line(&b, "# wake_mouse_ms: the mouse must keep moving this long (ms) to wake the");
    sb_line(&b, "# screen; a pause of half a second starts the count over. 0 = at once. 0-10000.");
    sb_kv_int(&b, "wake_mouse_ms", c->wake_mouse_ms);
    sb_line(&b, "# wake_keys: how many key presses or mouse clicks wake the screen. They");
    sb_line(&b, "# must come within 2 seconds of each other; holding a key counts once. 1-50.");
    sb_kv_int(&b, "wake_keys", c->wake_keys);
    sb_line(&b, "# debug_log: true writes a log to the user log folder (see README).");
    sb_line(&b, c->debug_log ? "debug_log: true" : "debug_log: false");
    sb_line(&b, "# glyphs: optional - override the built-in glyph set with your own string.");
    sb_line(&b, "# (half-width katakana + 0-9 + Z + symbols is the built-in default.)");
    sb_line(&b, "# glyphs: \"...\"");
    sb_line(&b, "");
    sb_theme_block(&b, c);
    return b.s;
}

int config_write_default(const char *path, const rain_config *c)
{
    char *text = config_default_text(c);
    FILE *f;
    int ok;
    if (!text) return -1;
    rain_mkdirs_for(path);
    f = rain_fopen(path, "wb");
    if (!f) { free(text); return -1; }
    ok = fwrite(text, 1, strlen(text), f) == strlen(text);
    fclose(f);
    free(text);
    return ok ? 0 : -1;
}

/* Key of a "key: value" line, lower-cased into out; 0 if not such a line. */
static int line_key(const char *line, size_t len, char *out, size_t outsz)
{
    size_t i = 0, j = 0, start, end;
    /* Indented lines belong to a block (themes:), never a top-level key. */
    if (len && (line[0] == ' ' || line[0] == '\t')) return 0;
    if (i >= len || line[i] == '#') return 0;
    start = i;
    while (i < len && line[i] != ':') i++;
    if (i >= len || i == start) return 0;
    end = i;
    while (end > start && is_space(line[end - 1])) end--;
    for (i = start; i < end && j + 1 < outsz; i++) out[j++] = lower(line[i]);
    out[j] = 0;
    return 1;
}

int config_update_keys(const char *path, const rain_config *c, const config_kv *kv, int n)
{
    char *text, *p;
    const char *nl;
    sbuf out = { 0 };
    int *done, i, crlf, rc = -1;
    FILE *f;

    if (!rain_file_exists(path) && config_write_default(path, c) != 0) return -1;
    text = read_all(path, NULL);
    if (!text) return -1;
    crlf = strstr(text, "\r\n") != NULL;
    nl = crlf ? "\r\n" : "\n";
    done = (int *)calloc((size_t)(n > 0 ? n : 1), sizeof(int));
    if (!done) { free(text); return -1; }

    p = text;
    while (*p) {
        char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        size_t content = len;
        char key[64];
        int replaced = 0;
        if (content && p[content - 1] == '\r') content--;
        if (line_key(p, content, key, sizeof key)) {
            for (i = 0; i < n; i++) {
                /* saving fps: also replaces an old speed_ms: line */
                if (!strcmp(key, kv[i].key) || (!strcmp(kv[i].key, "fps") && !strcmp(key, "speed_ms"))) {
                    sb_add(&out, kv[i].key); sb_add(&out, ": "); sb_add(&out, kv[i].value);
                    done[i] = 1; replaced = 1;
                    break;
                }
            }
        }
        if (!replaced) {
            char save = p[content];
            p[content] = 0;
            sb_add(&out, p);
            p[content] = save;
        }
        if (eol) sb_add(&out, nl);
        p += len + (eol ? 1 : 0);
    }
    if (out.len && out.s[out.len - 1] != '\n') sb_add(&out, nl);
    for (i = 0; i < n; i++) {
        if (done[i]) continue;
        sb_add(&out, kv[i].key); sb_add(&out, ": "); sb_add(&out, kv[i].value); sb_add(&out, nl);
    }

    f = rain_fopen(path, "wb");
    if (f && out.s) {
        if (fwrite(out.s, 1, out.len, f) == out.len) rc = 0;
    }
    if (f) fclose(f);
    free(out.s);
    free(done);
    free(text);
    return rc;
}
