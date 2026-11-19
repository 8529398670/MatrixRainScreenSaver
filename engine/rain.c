/* rain.c - the rain simulation. A direct port of the v4 C# algorithm
 * (SeedStreams / NewStream / WriteGlyph / AdvanceStream / TickRain), with
 * fixed-size arrays instead of per-tick lists. */
#include "rain.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint16_t glyph, swap_from;
    uint8_t empty, flipped, is_cursor, dissolving;
    int32_t born_tick, life_ticks;
} cell_t;

/* A string travelling down one column. It never moves glyphs - it only
 * writes new ones below. A deletion string writes blanks instead. */
typedef struct {
    int active;
    int col;
    double head_row;   /* fractional row of the leading (bottom) glyph */
    double acc;        /* fractional-row accumulator */
    double rate;       /* rows per tick */
    int start_row;
    int prefix_len;    /* blank cells before the first visible glyph */
    int trail_rows;    /* visible trail length in rows */
    int is_deletion;
    int has_cursor;
    int prev_head_row;
} rain_stream;

struct rain {
    rain_config cfg;
    int cols, rows;
    int tick;
    int stammer_in, stammer_now;
    uint32_t rng;
    cell_t *grid;          /* [x * rows + y] */
    rain_stream *streams;
    int stream_cap;
    int *ended;            /* scratch: columns whose string just finished */
    uint8_t *busy;         /* scratch: columns that have a string */
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

static rain_stream *add_stream(rain_t *r, int col, int deletion)
{
    int i;
    for (i = 0; i < r->stream_cap; i++) {
        rain_stream *s = &r->streams[i];
        if (s->active) continue;
        memset(s, 0, sizeof *s);
        s->active = 1;
        s->col = col;
        s->head_row = -rnd_range(r, 0, 4);
        s->rate = sample_rate(r);
        s->start_row = 0;
        s->prefix_len = (!deletion && rnd_below(r, 100) < 9) ? rnd_range(r, 1, 21) : 0;
        s->trail_rows = rnd_range(r, r->cfg.trail_min, r->cfg.trail_max + 1);
        s->is_deletion = deletion;
        s->has_cursor = !deletion && rnd_below(r, 100) < r->cfg.cursor_percent;
        s->prev_head_row = -1;
        return s;
    }
    return NULL; /* full - skip this one */
}

static void clear_cell(rain_t *r, int x, int y)
{
    cell_t *c;
    if (x < 0 || x >= r->cols || y < 0 || y >= r->rows) return;
    c = at(r, x, y);
    c->empty = 1;
    c->is_cursor = 0;
    c->dissolving = 0;
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
    c->flipped = rnd_below(r, 100) < r->cfg.flip_percent;
    c->is_cursor = (uint8_t)cursor;
    c->born_tick = born_tick;
    life = (int)(s->trail_rows / s->rate);
    c->life_ticks = life < 1 ? 1 : life;
    c->dissolving = 0;
}

static void advance_stream(rain_t *r, rain_stream *s)
{
    int row;
    /* Clear the cursor flag on the cell the head is leaving. */
    if (s->prev_head_row >= 0 && s->prev_head_row < r->rows)
        at(r, s->col, s->prev_head_row)->is_cursor = 0;
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

static void seed_streams(rain_t *r)
{
    int x, row;
    for (x = 0; x < r->cols; x++) {
        rain_stream *s;
        int head;
        if (rnd_below(r, 100) >= r->cfg.density) continue;
        s = add_stream(r, x, 0);
        if (!s) break;
        s->head_row = rnd_range(r, 0, r->rows);
        /* Pre-fill the cells above the head so the screen isn't empty at
         * startup; older cells are born earlier. Only the head cell gets
         * the cursor (the C# version lit the whole seeded column). */
        head = (int)floor(s->head_row);
        for (row = 0; row <= head; row++) {
            if (row - s->start_row < s->prefix_len) continue;
            write_glyph(r, s, row, 0 - (int)((head - row) / s->rate), s->has_cursor && row == head);
        }
        s->prev_head_row = head;
    }
}

/* ---- public ----------------------------------------------------------- */

void rain_grid_for(int w, int h, int cell, int *cols, int *rows)
{
    if (cell < 1) cell = 1;
    *cols = w / cell;
    if (*cols < 1) *cols = 1;
    *rows = h / cell + 2;
    if (*rows < 1) *rows = 1;
}

static int init_grid(rain_t *r, int cols, int rows)
{
    size_t n;
    int x, y;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    free(r->grid); free(r->streams); free(r->ended); free(r->busy);
    r->cols = cols;
    r->rows = rows;
    n = (size_t)cols * (size_t)rows;
    r->grid = (cell_t *)calloc(n, sizeof(cell_t));
    r->stream_cap = cols * 2;
    r->streams = (rain_stream *)calloc((size_t)r->stream_cap, sizeof(rain_stream));
    r->ended = (int *)calloc((size_t)cols, sizeof(int));
    r->busy = (uint8_t *)calloc((size_t)cols, 1);
    if (!r->grid || !r->streams || !r->ended || !r->busy) return -1;
    for (x = 0; x < cols; x++)
        for (y = 0; y < rows; y++) {
            at(r, x, y)->empty = 1;
            at(r, x, y)->life_ticks = 1;
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
    free(r->grid); free(r->streams); free(r->ended); free(r->busy);
    free(r);
}

int rain_resize(rain_t *r, int cols, int rows)
{
    if (cols == r->cols && rows == r->rows) return 0;
    return init_grid(r, cols, rows);
}

void rain_tick(rain_t *r)
{
    int x, y, i, n_ended = 0;
    double spawn_p;

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

    /* Synchronized glyph swap: every cell due for a change swaps on THIS
     * tick, then all hold still until the next swap tick. (Random per-cell
     * timing would fizz like static; lockstep is the film's shimmer.) */
    if (r->tick % r->cfg.mutate_every_ticks == 0) {
        for (i = 0; i < r->cols * r->rows; i++) {
            cell_t *c = &r->grid[i];
            if (c->empty) continue;
            if (rnd_double(r) < 0.08) {
                c->swap_from = c->glyph;
                c->glyph = (uint16_t)random_glyph(r);
                c->flipped = rnd_below(r, 100) < r->cfg.flip_percent;
                c->dissolving = 1;
            }
        }
    }

    /* Age cells out: a glyph stays visible for its trail length. */
    for (x = 0; x < r->cols; x++)
        for (y = 0; y < r->rows; y++) {
            cell_t *c = at(r, x, y);
            if (!c->empty && r->tick - c->born_tick >= c->life_ticks) clear_cell(r, x, y);
        }

    /* Advance strings; retire finished ones. */
    for (i = 0; i < r->stream_cap; i++) {
        rain_stream *s = &r->streams[i];
        if (!s->active) continue;
        if (!(r->stammer_now && s->has_cursor)) {
            s->acc += s->rate;
            while (s->acc >= 1.0) {
                s->acc -= 1.0;
                s->head_row += 1.0;
                advance_stream(r, s);
            }
        }
        if (s->head_row - s->trail_rows > r->rows + 2) {
            if (s->prev_head_row >= 0 && s->prev_head_row < r->rows)
                at(r, s->col, s->prev_head_row)->is_cursor = 0;
            s->active = 0;
            r->ended[n_ended++] = s->col;
        }
    }

    /* Sometimes a deletion string follows a finished string and eats what
     * is left of it from the top down. */
    for (i = 0; i < n_ended; i++)
        if (rnd_below(r, 100) < 25) add_stream(r, r->ended[i], 1);

    /* Spawn new strings in columns that have none. */
    memset(r->busy, 0, (size_t)r->cols);
    for (i = 0; i < r->stream_cap; i++)
        if (r->streams[i].active) r->busy[r->streams[i].col] = 1;
    spawn_p = r->cfg.density / 100.0 * 0.05;
    for (x = 0; x < r->cols; x++) {
        double roll;
        if (r->busy[x]) continue;
        roll = rnd_double(r);
        if (roll < spawn_p) add_stream(r, x, 0);
        else if (roll < spawn_p + 0.001 && column_has_visible(r, x))
            add_stream(r, x, 1); /* rare deletion string: erases a column's remains */
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
    double t;
    memset(&v, 0, sizeof v);
    if (x < 0 || x >= r->cols || y < 0 || y >= r->rows) return v;
    c = &r->grid[(size_t)x * (size_t)r->rows + (size_t)y];
    if (c->empty) return v;
    t = (r->tick - c->born_tick) / (double)(c->life_ticks > 1 ? c->life_ticks : 1);
    if (t >= 1.0) return v;
    v.visible = 1;
    v.glyph = c->glyph;
    v.swap_from = c->swap_from;
    v.flipped = c->flipped;
    v.dissolving = c->dissolving;
    if (c->is_cursor) v.shade = 0;
    else {
        int s = 1 + (int)(t * 6.0);
        v.shade = (uint8_t)(s > 7 ? 7 : s);
    }
    return v;
}

int rain_cell_born(const rain_t *r, int x, int y)
{
    if (x < 0 || x >= r->cols || y < 0 || y >= r->rows) return 0;
    return r->grid[(size_t)x * (size_t)r->rows + (size_t)y].born_tick;
}
