/* engine_test.c - deterministic tests for the rain engine and config.
 * Run from the repository root (it reads ./config.yaml). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/config.h"
#include "../engine/paths.h"
#include "../engine/rain.h"
#include "../engine/render.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static rain_config base_cfg(void)
{
    rain_config c;
    config_defaults(&c);
    return c;
}

/* Glyphs never move: between ticks a visible cell's glyph only changes if a
 * string rewrote the cell (new born tick) or on a synchronized swap, which
 * must be a mutate tick, dissolving, with swap_from = the old glyph. */
static void test_glyphs_never_move(void)
{
    rain_config c = base_cfg();
    rain_t *r = rain_create(60, 40, &c, 1234);
    int cols = rain_cols(r), rows = rain_rows(r);
    rain_cell_view *prev = (rain_cell_view *)malloc(sizeof(rain_cell_view) * (size_t)(cols * rows));
    int *born = (int *)malloc(sizeof(int) * (size_t)(cols * rows));
    int t, x, y, swaps = 0, dissolve_off_tick = 0;
    for (x = 0; x < cols; x++)
        for (y = 0; y < rows; y++) { prev[x * rows + y] = rain_cell(r, x, y); born[x * rows + y] = rain_cell_born(r, x, y); }
    for (t = 0; t < 2000; t++) {
        int tick;
        rain_tick(r);
        tick = rain_tick_count(r);
        for (x = 0; x < cols; x++)
            for (y = 0; y < rows; y++) {
                rain_cell_view v = rain_cell(r, x, y), p = prev[x * rows + y];
                int b = rain_cell_born(r, x, y);
                if (v.dissolving) {
                    swaps++;
                    if (tick % c.mutate_every_ticks != 0) dissolve_off_tick++;
                }
                if (p.visible && v.visible && v.glyph != p.glyph && b == born[x * rows + y]) {
                    CHECK(tick % c.mutate_every_ticks == 0);
                    CHECK(v.dissolving);
                    CHECK(v.swap_from == p.glyph);
                }
                CHECK(v.shade <= 8);
                CHECK(v.flipped <= 2);
                CHECK(!v.visible || v.glyph < c.glyph_count);
                prev[x * rows + y] = v;
                born[x * rows + y] = b;
            }
    }
    CHECK(swaps > 0);
    CHECK(dissolve_off_tick == 0);
    free(prev);
    free(born);
    rain_destroy(r);
}

/* At most one cursor (shade 0) per column at any time - a string's bright
 * head is a single cell, also right after seeding. */
static void test_one_cursor_per_column(void)
{
    rain_config c = base_cfg();
    rain_t *r;
    int t, x, y;
    c.cursor_percent = 100;
    r = rain_create(80, 50, &c, 99);
    for (t = 0; t < 600; t++) {
        for (x = 0; x < rain_cols(r); x++) {
            int heads = 0;
            for (y = 0; y < rain_rows(r); y++) {
                rain_cell_view v = rain_cell(r, x, y);
                if (v.visible && v.shade == 0) heads++;
            }
            CHECK(heads <= 1);
        }
        rain_tick(r);
    }
    rain_destroy(r);
}

static uint64_t frame_hash(const rain_t *r)
{
    uint64_t h = 1469598103934665603ULL;
    int x, y;
    for (x = 0; x < rain_cols(r); x++)
        for (y = 0; y < rain_rows(r); y++) {
            rain_cell_view v = rain_cell(r, x, y);
            uint64_t k = v.visible ? (((uint64_t)v.glyph << 16) | ((uint64_t)v.shade << 8) | ((uint64_t)v.flipped << 1) | v.dissolving | (1u << 30)) : 0;
            h = (h ^ k) * 1099511628211ULL;
        }
    return h;
}

static void test_deterministic(void)
{
    rain_config c = base_cfg();
    rain_t *a = rain_create(50, 30, &c, 42), *b = rain_create(50, 30, &c, 42), *d = rain_create(50, 30, &c, 43);
    int t;
    for (t = 0; t < 500; t++) { rain_tick(a); rain_tick(b); rain_tick(d); }
    CHECK(frame_hash(a) == frame_hash(b));
    CHECK(frame_hash(a) != frame_hash(d));
    rain_destroy(a); rain_destroy(b); rain_destroy(d);
}

static void test_density_and_resize(void)
{
    rain_config c = base_cfg();
    rain_t *r;
    int t, x, y, lit = 0;
    c.density = 100;
    r = rain_create(100, 60, &c, 7);
    for (t = 0; t < 300; t++) rain_tick(r);
    for (x = 0; x < rain_cols(r); x++)
        for (y = 0; y < rain_rows(r); y++) lit += rain_cell(r, x, y).visible;
    CHECK(lit > 100 * 60 / 10);
    CHECK(rain_resize(r, 10, 5) == 0);
    CHECK(rain_cols(r) == 10 && rain_rows(r) == 5);
    for (t = 0; t < 300; t++) rain_tick(r);
    CHECK(rain_cell(r, 10, 0).visible == 0); /* out of range is blank */
    rain_destroy(r);
}

static void test_render(void)
{
    rain_config c = base_cfg();
    rain_t *r = rain_create(8, 6, &c, 5);
    rain_atlas *a = atlas_create(4, 4, c.glyph_count);
    rain_renderer *rr;
    uint32_t px[32 * 20], shades[RAIN_SHADES];
    int g, i, painted;
    for (g = 0; g < c.glyph_count; g++) {
        uint8_t *m = atlas_mask(a, g, 0);
        m[0] = 255; /* top-left pixel lit */
    }
    atlas_finish(a);
    CHECK(atlas_mask(a, 0, 1)[3] == 255 && atlas_mask(a, 0, 1)[0] == 0);   /* mirrored */
    CHECK(atlas_mask(a, 0, 2)[12] == 255 && atlas_mask(a, 0, 2)[0] == 0);  /* upside down */
    rr = renderer_create(&c, a);
    painted = renderer_draw(rr, r, px, 32, 20, 32, 1);
    CHECK(painted > 0);
    for (i = 0; i < 32 * 20; i++) CHECK((px[i] & 0xFF000000u) == 0);
    rain_tick(r);
    CHECK(renderer_draw(rr, r, px, 32, 20, 32, 0) <= 8 * 6);
    CHECK(renderer_draw(rr, r, px, 32, 20, 32, 0) == 0); /* nothing changed */
    rain_shade_colors(&c, 0, shades);
    CHECK(shades[0] == 0xE8FFE8 && shades[1] == 0x00FF41 && shades[7] == 0x00280A);
    CHECK(shades[8] == 0x74FF94); /* half way head -> rain */
    renderer_destroy(rr);
    atlas_destroy(a);
    rain_destroy(r);
}

/* Glow only adds light: it never darkens a pixel, never touches a frame
 * with nothing lit, and glow_percent 0 leaves the plain render alone. */
static void test_glow(void)
{
    rain_config c = base_cfg();
    rain_t *r = rain_create(16, 8, &c, 11);
    rain_atlas *a = atlas_create(6, 8, c.glyph_count);
    rain_renderer *on, *off;
    static uint32_t p_on[96 * 64], p_off[96 * 64];
    int g, i, brighter = 0, darker = 0;
    for (g = 0; g < c.glyph_count; g++) memset(atlas_mask(a, g, 0) + 6 * 3, 255, 6);
    atlas_finish(a);
    on = renderer_create(&c, a);
    c.glow_percent = 0;
    off = renderer_create(&c, a);
    for (i = 0; i < 20; i++) rain_tick(r);
    renderer_draw(on, r, p_on, 96, 64, 96, 1);
    renderer_draw(off, r, p_off, 96, 64, 96, 1);
    for (i = 0; i < 96 * 64; i++) {
        int c1;
        for (c1 = 0; c1 <= 16; c1 += 8) {
            int x = (p_on[i] >> c1) & 255, y = (p_off[i] >> c1) & 255;
            if (x > y) brighter++;
            if (x < y) darker++;
        }
    }
    CHECK(brighter > 0);
    CHECK(darker == 0);
    renderer_destroy(on);
    renderer_destroy(off);
    atlas_destroy(a);
    rain_destroy(r);
}

/* The film's fixed orientations. */
static void test_glyph_variants(void)
{
    CHECK(rain_glyph_variant(0xFF8A, 1) == 1); /* katakana: mirrored */
    CHECK(rain_glyph_variant(0xFF8A, 0) == 0);
    CHECK(rain_glyph_variant('2', 0) == 1 && rain_glyph_variant('5', 1) == 1 && rain_glyph_variant('9', 1) == 1);
    CHECK(rain_glyph_variant('3', 1) == 2 && rain_glyph_variant('6', 1) == 2);
    CHECK(rain_glyph_variant('0', 1) == 0 && rain_glyph_variant('7', 1) == 0 && rain_glyph_variant('Z', 1) == 0);
    CHECK(rain_glyph_variant(0x65E5, 1) == 0); /* the kanji */
}

/* Without trail_fade, lit cells are one flat brightness, and trails still
 * end (deletion strings eat them) so the screen never fills up. */
static void test_flat_trails_end(void)
{
    rain_config c = base_cfg();
    rain_t *r;
    int t, x, y, lit = 0, total, other_shade = 0;
    c.density = 100;
    c.cursor_percent = 0;
    r = rain_create(60, 40, &c, 3);
    for (t = 0; t < 3000; t++) {
        rain_tick(r);
        for (x = 0; x < rain_cols(r); x++)
            for (y = 0; y < rain_rows(r); y++) {
                rain_cell_view v = rain_cell(r, x, y);
                if (v.visible && v.shade != 1) other_shade++;
            }
    }
    total = rain_cols(r) * rain_rows(r);
    for (x = 0; x < rain_cols(r); x++)
        for (y = 0; y < rain_rows(r); y++) lit += rain_cell(r, x, y).visible;
    CHECK(other_shade == 0);
    CHECK(lit > total / 10);
    CHECK(lit < total * 8 / 10);
    rain_destroy(r);
}

/* Glyph changes are grouped by string: with changing_percent 0 almost
 * nothing changes; with 100 every visible glyph changes on a swap tick. */
static void test_changing_strings(void)
{
    rain_config c = base_cfg();
    int pass;
    for (pass = 0; pass < 2; pass++) {
        rain_t *r;
        int t, x, y, vis = 0, dis = 0;
        c.changing_percent = pass ? 100 : 0;
        r = rain_create(40, 30, &c, 21);
        for (t = 0; t < 400; t++) rain_tick(r);
        while (rain_tick_count(r) % c.mutate_every_ticks) rain_tick(r);
        for (x = 0; x < rain_cols(r); x++)
            for (y = 0; y < rain_rows(r); y++) {
                rain_cell_view v = rain_cell(r, x, y);
                vis += v.visible;
                dis += v.visible && v.dissolving;
            }
        CHECK(vis > 0);
        if (pass) CHECK(dis > vis * 8 / 10); /* minus cells written this very tick */
        else CHECK(dis < vis / 20 + 1);
        rain_destroy(r);
    }
}

static void test_config_parse(void)
{
    rain_config c = base_cfg();
    config_parse(&c,
        "# comment line\r\n"
        "speed_ms: 30   # trailing comment\r\n"
        "font_name: \"Yu Gothic\"\n"
        "font_size: 99\n"              /* clamped to 32 */
        "density_percent: abc\n"       /* bad: ignored */
        "trail_min: 20\n"
        "trail_max: 10\n"              /* fixed up to trail_min */
        "fall_speed_min: 0.4\n"
        "fall_speed_max: 1.25\n"
        "head_color: \"#FF0080\"\n"
        "rain_color: #102030\n"
        "tail_color: lime\n"
        "background_color: \"#GGGGGG\"\n" /* bad: keeps default */
        "debug_log: true\n"
        "wake_mouse_ms: 1500\n"
        "wake_keys: 0\n"               /* clamped to 1 */
        "unknown_key: 5\n"
        "no colon here\n"
        "glyphs: 'AB:C'\n");
    CHECK(c.speed_ms == 30);
    CHECK(!strcmp(c.font_name, "Yu Gothic"));
    CHECK(c.font_size == 32);
    CHECK(c.density == 90);
    CHECK(c.trail_min == 20 && c.trail_max == 20);
    CHECK(c.fall_min > 0.399 && c.fall_min < 0.401);
    CHECK(c.fall_max > 1.249 && c.fall_max < 1.251);
    CHECK(c.head_color == 0xFF0080);
    CHECK(c.rain_color == 0x102030);
    CHECK(c.tail_color == 0x00FF00);
    CHECK(c.background_color == 0x000000);
    CHECK(c.debug_log == 1);
    CHECK(c.wake_mouse_ms == 1500);
    CHECK(c.wake_keys == 1);
    CHECK(c.glyph_count == 4 && c.glyphs[2] == ':' && c.glyphs_overridden);
}

static void test_default_font(void)
{
    rain_config c;
    config_defaults(&c);
    CHECK(!strcmp(c.font_name, "BIZ UDGothic")); /* bundled in fonts/open/ */
}

static void test_repo_config(void)
{
    rain_config c = base_cfg();
    CHECK(config_load_file(&c, "config.yaml") == 0);
    CHECK(c.glyph_count == 53);   /* the film's exact 53 */
    CHECK(c.glyphs[0] == 0xFF8A); /* ﾊ */
    CHECK(c.glyphs[30] == 0x65E5); /* 日 */
    CHECK(c.speed_ms == 42);
    CHECK(c.flip_percent == 100 && !c.trail_fade);
    /* whichever theme it picks (you change it), it's one that exists */
    CHECK(c.theme[0] && config_find_theme(&c, c.theme) >= 0 && c.theme_count >= 4);
    CHECK(!strcmp(c.font_name, "BIZ UDGothic"));
}

static void test_themes(void)
{
    rain_config c = base_cfg(), d;
    char *text;
    int i;
    /* no theme line: the old top-level colour keys, as written */
    config_parse(&c, "rain_color: \"#123456\"\n");
    CHECK(!c.theme[0] && c.rain_color == 0x123456);
    /* a built-in theme wins over colour keys, wherever they are */
    c = base_cfg();
    config_parse(&c, "theme: WSU-Green\nrain_color: \"#123456\"\n");
    CHECK(!strcmp(c.theme, "wsu-green"));
    CHECK(c.rain_color == 0x046A38 && c.head_color == 0xCBA052);
    CHECK(c.tail_color == 0x00482B && c.background_color == 0x101820);
    c = base_cfg();
    config_parse(&c, "theme: wsu-gold\n");
    CHECK(c.rain_color == 0xCBA052 && c.head_color == 0xF8E08E && c.background_color == 0x101820);
    c = base_cfg();
    config_parse(&c, "rain_color: \"#123456\"\ntheme: default\n");
    CHECK(c.rain_color == 0x00FF41 && c.head_color == 0xE8FFE8 && c.background_color == 0);
    /* unknown theme name: ignored */
    c = base_cfg();
    config_parse(&c, "theme: nope\nrain_color: \"#123456\"\n");
    CHECK(!c.theme[0] && c.rain_color == 0x123456);
    /* a theme defined in the file, picked before or after its block; an
     * unset colour starts from the film's; a top-level key ends the block */
    c = base_cfg();
    config_parse(&c, "theme: wsu-1\nthemes:\n  # mine\n  wsu-1:\n    rain: \"#112233\"\n"
                     "    background: \"#010203\"\n  other:\n    rain: \"#445566\"\nspeed_ms: 30\n"
                     "rain: \"#FFFFFF\"\n");
    CHECK(!strcmp(c.theme, "wsu-1") && c.rain_color == 0x112233 && c.background_color == 0x010203);
    CHECK(c.head_color == 0xE8FFE8 && c.speed_ms == 30);
    CHECK(config_find_theme(&c, "other") >= 0 && c.themes[config_find_theme(&c, "other")].rain == 0x445566);
    /* redefining a built-in replaces its colours */
    c = base_cfg();
    config_parse(&c, "themes:\n  default:\n    rain: \"#ABCDEF\"\ntheme: default\n");
    CHECK(c.rain_color == 0xABCDEF && c.theme_count == 4);
    /* indented lines outside themes: are not top-level keys */
    c = base_cfg();
    config_parse(&c, "  speed_ms: 30\n");
    CHECK(c.speed_ms == 42);
    /* the default text round-trips every theme and the selection */
    c = base_cfg();
    config_parse(&c, "themes:\n  mine:\n    head: \"#010101\"\n    rain: \"#020202\"\ntheme: mine\n");
    text = config_default_text(&c);
    d = base_cfg();
    config_parse(&d, text);
    free(text);
    CHECK(!strcmp(d.theme, "mine") && d.theme_count == c.theme_count && d.rain_color == 0x020202);
    for (i = 0; i < c.theme_count; i++)
        CHECK(!strcmp(d.themes[i].name, c.themes[i].name) && d.themes[i].head == c.themes[i].head &&
              d.themes[i].tail == c.themes[i].tail && d.themes[i].background == c.themes[i].background &&
              d.themes[i].mix_count == c.themes[i].mix_count);
}

/* A mix theme: each string takes one of the mixed themes' colours. */
static void test_mix_theme(void)
{
    rain_config c = base_cfg();
    rain_palette p0, p1;
    rain_t *r;
    int x, y, seen[2] = { 0, 0 }, bad = 0, t;
    config_parse(&c, "theme: wsu\n");
    CHECK(!strcmp(c.theme, "wsu") && c.palette_count == 2);
    p0 = config_palette(&c, 0);
    p1 = config_palette(&c, 1);
    CHECK(p0.rain == 0x046A38 && p0.head == 0xCBA052 && c.rain_color == 0x046A38);
    CHECK(p1.rain == 0xCBA052 && p1.head == 0xF8E08E && c.background_color == 0x101820);
    /* both colour sets show up, and nothing else */
    r = rain_create(40, 30, &c, 9);
    for (t = 0; t < 60; t++) rain_tick(r);
    for (x = 0; x < 40; x++)
        for (y = 0; y < 30; y++) {
            rain_cell_view v = rain_cell(r, x, y);
            if (!v.visible) continue;
            if (v.palette > 1) bad++;
            else seen[v.palette]++;
        }
    CHECK(seen[0] > 0 && seen[1] > 0 && !bad);
    rain_destroy(r);
    /* a user mix, quoted list, with a missing name skipped */
    c = base_cfg();
    config_parse(&c, "themes:\n  mine:\n    mix: [default, nope, \"wsu-gold\"]\ntheme: mine\n");
    CHECK(c.palette_count == 2 && c.rain_color == 0x00FF41 && config_palette(&c, 1).rain == 0xCBA052);
    CHECK(c.background_color == 0);
    /* switching back to a plain theme drops the mix */
    config_parse(&c, "theme: wsu-gold\n");
    CHECK(c.palette_count == 1 && config_palette(&c, 1).rain == 0xCBA052);
}

/* Mix weights: `wsu-green: 0.75` in a theme sets how often it comes up. */
static void test_mix_weights(void)
{
    rain_config c = base_cfg(), d, e;
    rain_t *r;
    int t, x, y, seen[2] = { 0, 0 };
    char *text;
    uint32_t roll;
    /* the built-in wsu: equal, 1/1 */
    config_parse(&c, "theme: wsu\n");
    CHECK(c.palette_count == 2 && c.palette_weights[0] == 1 && c.palette_weights[1] == 1);
    /* 0.75/0.25 reduces to 3/1; tabs work as indents too */
    config_parse(&c, "themes:\n  mine:\n\twsu-green: 0.75\n\twsu-gold: 0.25\ntheme: mine\n");
    CHECK(c.palette_count == 2 && c.palette_weights[0] == 3 && c.palette_weights[1] == 1);
    CHECK(c.rain_color == 0x046A38 && config_palette(&c, 1).rain == 0xCBA052);
    /* the pick follows the odds: 3 of every 4 rolls are palette 0 */
    for (roll = 0, t = 0; roll < 400; roll++) t += config_pick_palette(&c, roll) == 0;
    CHECK(t == 300);
    /* and so does the rain, roughly */
    r = rain_create(80, 40, &c, 5);
    for (t = 0; t < 80; t++) rain_tick(r);
    for (x = 0; x < 80; x++)
        for (y = 0; y < 40; y++) {
            rain_cell_view v = rain_cell(r, x, y);
            if (v.visible && v.palette < 2) seen[v.palette]++;
        }
    CHECK(seen[1] > 0 && seen[0] > 2 * seen[1]);
    rain_destroy(r);
    /* redefining the built-in wsu replaces its list, not adds to it */
    c = base_cfg();
    config_parse(&c, "themes:\n  wsu:\n    default: 1\n    wsu-gold: 3\ntheme: wsu\n");
    CHECK(c.palette_count == 2 && c.rain_color == 0x00FF41 && c.palette_weights[0] == 1 && c.palette_weights[1] == 3);
    /* a 0 never comes up; a missing theme is skipped; a non-number is ignored */
    config_parse(&c, "themes:\n  m4:\n    default: 0.6\n    nope: 0.2\n    wsu-gold: 0.4\n    wsu-green: 0\n    foo: bar\ntheme: m4\n");
    CHECK(c.palette_count == 3 && c.palette_weights[0] == 3 && c.palette_weights[1] == 2 && c.palette_weights[2] == 0);
    for (roll = 0, t = 0; roll < 400; roll++) t += config_pick_palette(&c, roll) == 2;
    CHECK(t == 0);
    /* the old `mix: a, b` list still works, equal */
    config_parse(&c, "themes:\n  old:\n    mix: default, wsu-gold\ntheme: old\n");
    CHECK(c.palette_count == 2 && c.palette_weights[0] == 1 && c.palette_weights[1] == 1);
    /* weights survive the default text round trip */
    config_apply_theme(&c, "m4");
    text = config_default_text(&c);
    CHECK(text && strstr(text, "  m4:\n    default: 0.6\n    nope: 0.2\n    wsu-gold: 0.4\n    wsu-green: 0\n"));
    CHECK(text && strstr(text, "  wsu:\n    default: 1\n    wsu-gold: 3\n"));
    d = base_cfg();
    if (text) config_parse(&d, text);
    free(text);
    CHECK(!strcmp(d.theme, "m4") && d.palette_count == 3 && d.palette_weights[0] == 3 && d.palette_weights[2] == 0);
    /* a name with no share gets what the others leave: 0.95 + 0.05 = 19/1 */
    e = base_cfg();
    config_parse(&e, "themes:\n  wsu:\n    wsu-green: 0.95\n    wsu-gold:\n  after:\n    rain: \"#010203\"\ntheme: wsu\n");
    CHECK(e.palette_count == 2 && e.palette_weights[0] == 19 && e.palette_weights[1] == 1);
    CHECK(config_find_theme(&e, "after") >= 0 && config_find_theme(&e, "wsu-gold") == 2);
    /* several without a share split the rest equally: 0.5, 0.25, 0.25 */
    config_parse(&e, "themes:\n  m3:\n    default: 0.5\n    wsu-green:\n    wsu-gold:\ntheme: m3\n");
    CHECK(e.palette_count == 3 && e.palette_weights[0] == 2 && e.palette_weights[1] == 1 && e.palette_weights[2] == 1);
    text = config_default_text(&e);
    CHECK(text && strstr(text, "  m3:\n    default: 0.5\n    wsu-green:\n    wsu-gold:\n"));
    d = base_cfg();
    if (text) config_parse(&d, text);
    free(text);
    CHECK(!strcmp(d.theme, "m3") && d.palette_weights[0] == 2 && d.palette_weights[2] == 1);
}

static void test_default_text_roundtrip(void)
{
    rain_config a = base_cfg(), b;
    char *text;
    a.speed_ms = 77; a.fall_min = 0.35; a.fall_max = 0.6; a.themes[0].rain = 0x123456;
    a.wake_mouse_ms = 2500; a.wake_keys = 4;
    text = config_default_text(&a);
    config_defaults(&b);
    config_parse(&b, text);
    CHECK(b.speed_ms == 77);
    CHECK(b.wake_mouse_ms == 2500 && b.wake_keys == 4);
    CHECK(b.fall_min > 0.349 && b.fall_min < 0.351);
    CHECK(b.fall_max > 0.599 && b.fall_max < 0.601);
    CHECK(b.rain_color == 0x123456);
    CHECK(!b.glyphs_overridden);
    free(text);
}

static void test_update_keys(void)
{
    const char *path = "engine_test_tmp.yaml";
    const char *orig = "# top comment\r\nspeed_ms: 50\r\n# middle comment\r\nfont_size: 20 # note\r\n";
    config_kv kv[] = { { "font_size", "24" }, { "density_percent", "70" } };
    rain_config c = base_cfg();
    FILE *f = fopen(path, "wb");
    char buf[1024];
    size_t n;
    fwrite(orig, 1, strlen(orig), f);
    fclose(f);
    CHECK(config_update_keys(path, &c, kv, 2) == 0);
    f = fopen(path, "rb");
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    CHECK(!strcmp(buf, "# top comment\r\nspeed_ms: 50\r\n# middle comment\r\nfont_size: 24\r\ndensity_percent: 70\r\n"));
    /* `theme:` is rewritten; an indented line in themes: is left alone */
    f = fopen(path, "wb");
    fputs("theme: default\nthemes:\n  theme:\n    rain: \"#010101\"\n", f);
    fclose(f);
    kv[0].key = "theme"; kv[0].value = "wsu-gold";
    CHECK(config_update_keys(path, &c, kv, 1) == 0);
    f = fopen(path, "rb");
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    CHECK(!strcmp(buf, "theme: wsu-gold\nthemes:\n  theme:\n    rain: \"#010101\"\n"));
    remove(path);
}

int main(void)
{
    test_glyphs_never_move();
    test_one_cursor_per_column();
    test_deterministic();
    test_density_and_resize();
    test_render();
    test_glow();
    test_glyph_variants();
    test_flat_trails_end();
    test_changing_strings();
    test_config_parse();
    test_repo_config();
    test_default_font();
    test_themes();
    test_mix_theme();
    test_mix_weights();
    test_default_text_roundtrip();
    test_update_keys();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
