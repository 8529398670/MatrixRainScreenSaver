/* config.h - settings for the Matrix Rain engine, read from config.yaml.
 *
 * The YAML is a tiny subset: one "key: value" per line, # comments (at the
 * start of a line, or " #" after a value), quoted strings, ints, decimals
 * and #RRGGBB colours. Unknown keys are ignored and a bad line is skipped,
 * so one typo never kills the rain.
 */
#ifndef MATRIXRAIN_CONFIG_H
#define MATRIXRAIN_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#define RAIN_MAX_GLYPHS 512
#define RAIN_FONT_NAME_MAX 128

#define RAIN_MAX_THEMES 16
#define RAIN_THEME_NAME_MAX 32
#define RAIN_MAX_MIX 4          /* themes one `mix:` theme can combine */

/* A named colour set from the `themes:` block of config.yaml (or a
 * built-in one). `theme: <name>` picks one and sets all four colours.
 * A mix theme instead lists other themes, each with its share:
 *     wsu:
 *       wsu-green: 0.75
 *       wsu-gold: 0.25
 * Each falling string then takes one of their colour sets, that often,
 * and the background is the first one's. */
typedef struct rain_theme {
    char name[RAIN_THEME_NAME_MAX];  /* lower-case, e.g. "default", "wsu-green" */
    uint32_t head, rain, tail, background; /* 0xRRGGBB */
    char mix[RAIN_MAX_MIX][RAIN_THEME_NAME_MAX]; /* mixed theme names */
    int mix_count;                   /* 0 = a plain theme */
    double mix_weight[RAIN_MAX_MIX]; /* each mixed theme's share */
    int mix_weighted;                /* bit k set = mix_weight[k] was given */
} rain_theme;

/* One string colour set: head, rain and tail. */
typedef struct rain_palette {
    uint32_t head, rain, tail;
} rain_palette;

typedef struct rain_config {
    int speed_ms;           /* tick interval, 20-150 */
    char font_name[RAIN_FONT_NAME_MAX]; /* UTF-8 font family */
    int font_size;          /* glyph cell size in (logical) pixels, 12-32 */
    int density;            /* % of columns filled, 30-100 */
    int trail_min, trail_max;      /* trail length in glyphs */
    double fall_min, fall_max;     /* rows per tick */
    int cell_width_percent; /* cell width as % of font_size (cells are font_size tall) */
    int flip_percent;       /* chance a katakana is mirrored (digits follow the film) */
    int mutate_every_ticks; /* synchronized swap every N ticks */
    int changing_percent;   /* chance a string's glyphs keep changing */
    int cursor_percent;     /* chance a string has a bright head */
    int trail_fade;         /* 1 = trails dim with age and time out (sequels' look) */
    int glow_percent;       /* bloom strength, 0 = none */
    char theme[RAIN_THEME_NAME_MAX]; /* selected theme name; "" = none (old configs) */
    rain_theme themes[RAIN_MAX_THEMES]; /* built-ins, then any from config.yaml */
    int theme_count;
    /* The colours in use: the selected theme's, or (with no theme picked)
     * the old top-level head/rain/tail/background_color keys. */
    uint32_t head_color, rain_color, tail_color, background_color; /* 0xRRGGBB */
    /* Extra string colour sets from a mix theme. Palette 0 is always
     * head/rain/tail_color above; palette_count 0 or 1 = no mix. */
    rain_palette palettes[RAIN_MAX_MIX];
    int palette_weights[RAIN_MAX_MIX]; /* relative odds of each palette, >= 1 */
    int palette_count;
    int exit_mouse_pixels;  /* moves smaller than this are ignored as jitter */
    int wake_mouse_ms;      /* mouse must keep moving this long to wake, 0 = at once */
    int wake_keys;          /* key presses / clicks needed to wake, 1 = the first */
    int debug_log;          /* 1 = write a log file in the user log dir */
    uint32_t glyphs[RAIN_MAX_GLYPHS]; /* Unicode code points */
    int glyph_count;
    int glyphs_overridden;  /* 1 if config.yaml set `glyphs:` */
} rain_config;

typedef struct config_kv {
    const char *key;
    const char *value;
} config_kv;

/* Index of the theme called name (case-insensitive) in cfg->themes, or -1. */
int config_find_theme(const rain_config *cfg, const char *name);

/* Select theme `name` and set the four colours from it. Returns 0, or -1
 * (nothing changed) if there is no such theme. */
int config_apply_theme(rain_config *cfg, const char *name);

/* String colour set i (0 <= i < max(palette_count, 1)). */
rain_palette config_palette(const rain_config *cfg, int i);

/* Palette for a new string from a random number, by palette_weights. */
int config_pick_palette(const rain_config *cfg, uint32_t roll);

/* Fill with built-in defaults (same values the yaml is seeded with). */
void config_defaults(rain_config *cfg);

/* Apply every recognised "key: value" line in text on top of cfg. */
void config_parse(rain_config *cfg, const char *text);

/* Read and parse a UTF-8 file (BOM tolerated). 0 on success, -1 if unreadable. */
int config_load_file(rain_config *cfg, const char *path);

/* Commented default config.yaml for cfg's values. Caller frees. */
char *config_default_text(const rain_config *cfg);

/* Write config_default_text to path (creating parent dirs). 0 on success. */
int config_write_default(const char *path, const rain_config *cfg);

/* Rewrite just these keys in the file at path, keeping every other line
 * and comment. Missing keys are appended. If the file is missing it is
 * first created from cfg's defaults. 0 on success. */
int config_update_keys(const char *path, const rain_config *cfg, const config_kv *kv, int n);

/* Decode UTF-8 into code points (invalid bytes skipped). Returns count. */
int utf8_decode(const char *s, uint32_t *out, int max);

/* Encode one code point as UTF-8 into out (>= 5 bytes). Returns length. */
int utf8_encode(uint32_t cp, char *out);

#endif
