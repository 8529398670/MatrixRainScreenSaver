/* rain.c - the rain simulation. Started as a port of the v4 C# algorithm;
 * now follows Carl Newton's frame-by-frame analysis of the 1999 opening
 * titles more closely (see rain.h). Fixed-size arrays, no per-tick lists. */
#include "rain.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint16_t glyph, swap_from;
    uint8_t empty, variant, is_cursor, dissolving, changing, palette;
    int32_t born_tick, life_ticks;
    int32_t cursor_left_tick; /* tick the bright head moved off this cell */
} cell_t;

/* A string travelling down one column. It never moves glyphs - it only
 * writes new ones below. A deletion string writes blanks instead; every
 * string is followed by one, which is what ends its trail. */
typedef struct {
    int active;
    int id;
    int col;
    double head_row;   /* row of the leading (bottom) glyph */
    double acc;        /* fractional-row accumulator */
    double rate;       /* rows per tick */
    int start_row;
    int prefix_len;    /* blank cells before the first visible glyph */
    int trail_rows;    /* visible trail length in rows */
    int is_deletion;
    int owner_slot, owner_id; /* deletion strings: the string they follow */
    int has_cursor;
    int changing;      /* its glyphs keep changing (others stay still) */
    int palette;       /* colour set, from a mix theme (0 otherwise) */
    int prev_head_row;
} rain_stream;

struct rain {
    rain_config cfg;
    int cols, rows;
    int tick;
    int stammer_in, stammer_now;
    int next_id;
    uint32_t rng;
    cell_t *grid;          /* [x * rows + y] */
    rain_stream *streams;
    int stream_cap;
    int *writer;           /* scratch: active writing string per column, -1 */
};

/* ---- PRNG (xorshift32) ------------------------------------------------ */

static uint32_t rnd_u32(rain_t *r)
{
    uint32_t x = r->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return r->rng = x;
}
static int rnd_below(rain_t *r, int n) { return n <= 0 ? 0 : (int)(rnd_u32(r) % (uint32_t)n); }
static int rnd_range(rain_t *r, int lo, int hi) { return lo + rnd_below(r, hi - lo); } /* [lo, hi) */
static double rnd_double(rain_t *r) { return (rnd_u32(r) >> 8) / 16777216.0; }

/* ---- helpers ---------------------------------------------------------- */

static cell_t *at(rain_t *r, int x, int y) { return &r->grid[(size_t)x * (size_t)r->rows + (size_t)y]; }

static double sample_rate(rain_t *r)
{
    double span = r->cfg.fall_max - r->cfg.fall_min;
    return r->cfg.fall_min + rnd_double(r) * (span > 0 ? span : 0);
}

/* How the film draws each glyph (Newton): every katakana mirrored
 * left-right; 2 5 9 mirrored; 3 6 upside down; the rest as drawn. */
int rain_glyph_variant(uint32_t cp, int flip_roll_ok)
{
    if ((cp >= 0xFF66 && cp <= 0xFF9D) || (cp >= 0x30A0 && cp <= 0x30FF)) return flip_roll_ok ? 1 : 0;
    if (cp == '2' || cp == '5' || cp == '9') return 1;
    if (cp == '3' || cp == '6') return 2;
    return 0;
}

static int pick_variant(rain_t *r, int glyph)
{
    uint32_t cp = r->cfg.glyphs[glyph];
    int roll = r->cfg.flip_percent >= 100 ? 1 : rnd_below(r, 100) < r->cfg.flip_percent;
    return rain_glyph_variant(cp, roll);
}

static rain_stream *add_stream(rain_t *r, int col, int deletion)
{
    int i;
    for (i = 0; i < r->stream_cap; i++) {
        rain_stream *s = &r->streams[i];
        if (s->active) continue;
        memset(s, 0, sizeof *s);
        s->active = 1;
        s->id = ++r->next_id;
        s->col = col;
        s->head_row = -rnd_range(r, 0, 4);
        s->rate = sample_rate(r);
        s->start_row = 0;
        s->prefix_len = (!deletion && rnd_below(r, 100) < 9) ? rnd_range(r, 1, 21) : 0;
        s->trail_rows = rnd_range(r, r->cfg.trail_min, r->cfg.trail_max + 1);
        s->is_deletion = deletion;
        s->owner_slot = -1;
        s->has_cursor = !deletion && rnd_below(r, 100) < r->cfg.cursor_percent;
        s->changing = !deletion && rnd_below(r, 100) < r->cfg.changing_percent;
        /* Only roll with a mix, so single themes keep the same rain per seed. */
        s->palette = (!deletion && r->cfg.palette_count > 1) ? config_pick_palette(&r->cfg, rnd_u32(r)) : 0;
        s->prev_head_row = -1;
        return s;
    }
    return NULL; /* full - skip this one */
}

/* A writing string plus the deletion string that follows trail_rows behind
 * it at about the same speed, eating its trail from the top. */
static rain_stream *add_writer(rain_t *r, int col)
{
    rain_stream *s = add_stream(r, col, 0), *d;
    if (!s) return NULL;
    d = add_stream(r, col, 1);
    if (d) {
        d->head_row = s->head_row - s->trail_rows - s->prefix_len;
        d->rate = s->rate * (0.85 + 0.3 * rnd_double(r));
        d->owner_slot = (int)(s - r->streams);
        d->owner_id = s->id;
    }
    return s;
}

static void clear_cell(rain_t *r, int x, int y)
{
    cell_t *c;
    if (x < 0 || x >= r->cols || y < 0 || y >= r->rows) return;
    c = at(r, x, y);
    c->empty = 1;
    c->is_cursor = 0;
    c->dissolving = 0;
    c->cursor_left_tick = -1;
}

static int random_glyph(rain_t *r) { return rnd_below(r, r->cfg.glyph_count); }

static void write_glyph(rain_t *r, rain_stream *s, int row, int born_tick, int cursor)
{
    cell_t *c;
    int life;
    if (row < 0 || row >= r->rows) return;
    c = at(r, s->col, row);
    c->glyph = (uint16_t)random_glyph(r);
    c->empty = 0;
    c->variant = (uint8_t)pick_variant(r, c->glyph);
    c->is_cursor = (uint8_t)cursor;
    c->changing = (uint8_t)s->changing;
    c->palette = (uint8_t)s->palette;
    c->born_tick = born_tick;
    life = (int)(s->trail_rows / s->rate);
    c->life_ticks = life < 1 ? 1 : life;
    c->dissolving = 0;
    c->cursor_left_tick = -1;
}

static void advance_stream(rain_t *r, rain_stream *s)
{
    int row;
    /* The cell the head is leaving keeps half the highlight for one frame
     * (Newton's "partially highlighted" frame), then turns rain colour. */
    if (s->prev_head_row >= 0 && s->prev_head_row < r->rows) {
        cell_t *c = at(r, s->col, s->prev_head_row);
        if (c->is_cursor) c->cursor_left_tick = r->tick;
        c->is_cursor = 0;
    }
    row = (int)floor(s->head_row);
    s->prev_head_row = row;
    if (row < 0 || row >= r->rows) return;
    if (s->is_deletion || row - s->start_row < s->prefix_len) {
        /* Deletion string, or the invisible prefix: traverse blank. */
        clear_cell(r, s->col, row);
        return;
    }
    write_glyph(r, s, row, r->tick, s->has_cursor);
}

static int column_has_visible(rain_t *r, int x)
{
    int y;
    for (y = 0; y < r->rows; y++)
        if (!at(r, x, y)->empty) return 1;
    return 0;
}

static rain_stream *owner_of(rain_t *r, const rain_stream *d)
{
    rain_stream *o;
    if (d->owner_slot < 0) return NULL;
    o = &r->streams[d->owner_slot];
    return (o->active && o->id == d->owner_id) ? o : NULL;
}

static void seed_streams(rain_t *r)
{
    int x, row;
    for (x = 0; x < r->cols; x++) {
        rain_stream *s;
        int head, top, i;
        if (rnd_below(r, 100) >= r->cfg.density) continue;
        s = add_writer(r, x);
        if (!s) break;
        s->head_row = rnd_range(r, 0, r->rows);
        head = (int)floor(s->head_row);
        top = head - s->trail_rows + 1;
        /* Its deletion string starts just above the visible trail. */
        for (i = 0; i < r->stream_cap; i++) {
            rain_stream *d = &r->streams[i];
            if (d->active && d->is_deletion && owner_of(r, d) == s) d->head_row = top - 1;
        }
        /* Pre-fill the trail so the screen isn't empty at startup; older
         * cells are born earlier. Only the head cell gets the cursor. */
        for (row = top < 0 ? 0 : top; row <= head; row++) {
            if (row - s->start_row < s->prefix_len) continue;
            write_glyph(r, s, row, 0 - (int)((head - row) / s->rate), s->has_cursor && row == head);
        }
        s->prev_head_row = head;
    }
}

/* ---- public ----------------------------------------------------------- */

void rain_grid_for(int w, int h, int cell_w, int cell_h, int *cols, int *rows)
{
    if (cell_w < 1) cell_w = 1;
    if (cell_h < 1) cell_h = 1;
    *cols = w / cell_w;
    if (*cols < 1) *cols = 1;
    *rows = h / cell_h + 2;
    if (*rows < 1) *rows = 1;
}

static int init_grid(rain_t *r, int cols, int rows)
{
    size_t n;
    int x, y;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    free(r->grid); free(r->streams); free(r->writer);
    r->cols = cols;
    r->rows = rows;
    n = (size_t)cols * (size_t)rows;
    r->grid = (cell_t *)calloc(n, sizeof(cell_t));
    r->stream_cap = cols * 4;
    r->streams = (rain_stream *)calloc((size_t)r->stream_cap, sizeof(rain_stream));
    r->writer = (int *)calloc((size_t)cols, sizeof(int));
    if (!r->grid || !r->streams || !r->writer) return -1;
    for (x = 0; x < cols; x++)
        for (y = 0; y < rows; y++) {
            at(r, x, y)->empty = 1;
            at(r, x, y)->life_ticks = 1;
            at(r, x, y)->cursor_left_tick = -1;
        }
    r->tick = 0;
    r->stammer_in = rnd_range(r, 80, 200);
    r->stammer_now = 0;
    seed_streams(r);
    return 0;
}

rain_t *rain_create(int cols, int rows, const rain_config *cfg, uint32_t seed)
{
    rain_t *r = (rain_t *)calloc(1, sizeof(rain_t));
    if (!r) return NULL;
    r->cfg = *cfg;
    if (r->cfg.glyph_count <= 0) {
        rain_config d;
        config_defaults(&d);
        memcpy(r->cfg.glyphs, d.glyphs, sizeof d.glyphs);
        r->cfg.glyph_count = d.glyph_count;
    }
    if (r->cfg.mutate_every_ticks < 1) r->cfg.mutate_every_ticks = 1;
    if (r->cfg.trail_max < r->cfg.trail_min) r->cfg.trail_max = r->cfg.trail_min;
    if (r->cfg.fall_min <= 0) r->cfg.fall_min = 0.05;
    if (r->cfg.fall_max < r->cfg.fall_min) r->cfg.fall_max = r->cfg.fall_min;
    r->rng = seed ? seed : 0x9E3779B9u;
    if (init_grid(r, cols, rows) != 0) { rain_destroy(r); return NULL; }
    return r;
}

void rain_destroy(rain_t *r)
{
    if (!r) return;
    free(r->grid); free(r->streams); free(r->writer);
    free(r);
}

int rain_resize(rain_t *r, int cols, int rows)
{
    if (cols == r->cols && rows == r->rows) return 0;
    return init_grid(r, cols, rows);
}

static void step_stream(rain_t *r, rain_stream *s)
{
    rain_stream *o = s->is_deletion ? owner_of(r, s) : NULL;
    if (r->stammer_now && s->has_cursor) return;
    s->acc += s->rate;
    while (s->acc >= 1.0) {
        /* A deletion string never overtakes the head of its own string. */
        if (o && s->head_row + 1.0 >= o->head_row) { s->acc = 0; break; }
        s->acc -= 1.0;
        s->head_row += 1.0;
        advance_stream(r, s);
    }
}

void rain_tick(rain_t *r)
{
    int x, y, i;
    double spawn_p, change_p_still;

    r->tick++;

    /* Cursor stammer: every so often ALL cursors hesitate one tick. */
    if (r->stammer_in-- <= 0) {
        r->stammer_now = 1;
        r->stammer_in = rnd_range(r, 80, 200);
    } else {
        r->stammer_now = 0;
    }

    /* Dissolves from the previous tick are done after one frame. */
    for (i = 0; i < r->cols * r->rows; i++) r->grid[i].dissolving = 0;

    /* Synchronized glyph swap: every changing glyph swaps on THIS tick,
     * then all hold still until the next swap tick. Only the glyphs of
     * "changing" strings swap; the rest stay put but for a rare flicker. */
    if (r->tick % r->cfg.mutate_every_ticks == 0) {
        change_p_still = 0.01;
        for (i = 0; i < r->cols * r->rows; i++) {
            cell_t *c = &r->grid[i];
            if (c->empty) continue;
            if (c->changing || rnd_double(r) < change_p_still) {
                c->swap_from = c->glyph;
                c->glyph = (uint16_t)random_glyph(r);
                c->variant = (uint8_t)pick_variant(r, c->glyph);
                c->dissolving = 1;
            }
        }
    }

    /* Trails normally end when their deletion string eats them. With
     * trail_fade they also time out; otherwise a long timeout is only a
     * safety net for a deletion string that could not be spawned. */
    for (x = 0; x < r->cols; x++)
        for (y = 0; y < r->rows; y++) {
            cell_t *c = at(r, x, y);
            int limit = r->cfg.trail_fade ? c->life_ticks : c->life_ticks * 4 + 40;
            if (!c->empty && r->tick - c->born_tick >= limit) clear_cell(r, x, y);
        }

    /* Advance writing strings first, then deletion strings (which must not
     * pass their writer). Retire strings that have left the screen. */
    for (x = 0; x < r->cols; x++) r->writer[x] = -1;
    for (i = 0; i < r->stream_cap; i++) {
        rain_stream *s = &r->streams[i];
        if (!s->active || s->is_deletion) continue;
        step_stream(r, s);
        if (s->head_row >= r->rows + 1) {
            if (s->prev_head_row >= 0 && s->prev_head_row < r->rows)
                at(r, s->col, s->prev_head_row)->is_cursor = 0;
            s->active = 0;
        } else {
            r->writer[s->col] = i;
        }
    }
    for (i = 0; i < r->stream_cap; i++) {
        rain_stream *s = &r->streams[i];
        int w;
        if (!s->active || !s->is_deletion) continue;
        /* A new string that has overtaken an orphaned deletion string will
         * overwrite what is below it, and has its own follower: retire it. */
        w = r->writer[s->col];
        if (!owner_of(r, s) && w >= 0 && r->streams[w].head_row >= s->head_row) { s->active = 0; continue; }
        step_stream(r, s);
        if (s->head_row >= r->rows + 1) s->active = 0;
    }

    /* Spawn new strings in columns with no writing string. */
    spawn_p = r->cfg.density / 100.0 * 0.05;
    for (x = 0; x < r->cols; x++) {
        double roll;
        if (r->writer[x] >= 0) continue;
        roll = rnd_double(r);
        if (roll < spawn_p) add_writer(r, x);
        else if (roll < spawn_p + 0.001 && column_has_visible(r, x))
            add_stream(r, x, 1); /* rare lone deletion string: erases a column's remains */
    }
}

int rain_cols(const rain_t *r) { return r->cols; }
int rain_rows(const rain_t *r) { return r->rows; }
int rain_tick_count(const rain_t *r) { return r->tick; }
const rain_config *rain_cfg(const rain_t *r) { return &r->cfg; }

rain_cell_view rain_cell(const rain_t *r, int x, int y)
{
    rain_cell_view v;
    const cell_t *c;
    memset(&v, 0, sizeof v);
    if (x < 0 || x >= r->cols || y < 0 || y >= r->rows) return v;
    c = &r->grid[(size_t)x * (size_t)r->rows + (size_t)y];
    if (c->empty) return v;
    v.visible = 1;
    v.glyph = c->glyph;
    v.swap_from = c->swap_from;
    v.flipped = c->variant;
    v.dissolving = c->dissolving;
    v.palette = c->palette;
    if (c->is_cursor) v.shade = 0;
    else if (c->cursor_left_tick == r->tick) v.shade = 8; /* half highlight */
    else if (r->cfg.trail_fade) {
        double t = (r->tick - c->born_tick) / (double)(c->life_ticks > 1 ? c->life_ticks : 1);
        int s = 1 + (int)(t * 6.0);
        v.shade = (uint8_t)(s > 7 ? 7 : s < 1 ? 1 : s);
    } else {
        v.shade = 1; /* the first film's trails are one flat brightness */
    }
    return v;
}

int rain_cell_born(const rain_t *r, int x, int y)
{
    if (x < 0 || x >= r->cols || y < 0 || y >= r->rows) return 0;
    return r->grid[(size_t)x * (size_t)r->rows + (size_t)y].born_tick;
}
