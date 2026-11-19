/* rain.h - the Matrix digital rain simulation, platform-independent.
 *
 * The film's pattern (frame-by-frame analysis: matrix.watch + Carl Newton):
 * a fixed grid where glyphs never move; strings grow by writing a new glyph
 * one row below per tick; all due glyphs swap on the same tick with a
 * one-frame cross-dissolve; deletion strings erase from above; ~1 in 5
 * strings has a bright cursor head; every so often all cursors stammer.
 *
 * No OS calls, no allocation per tick, deterministic for a given seed.
 */
#ifndef MATRIXRAIN_RAIN_H
#define MATRIXRAIN_RAIN_H

#include <stdint.h>
#include "config.h"

typedef struct rain rain_t;

/* What a shell needs to draw one cell. glyph / swap_from index cfg->glyphs. */
typedef struct rain_cell_view {
    uint16_t glyph;
    uint16_t swap_from;  /* previous glyph, only meaningful while dissolving */
    uint8_t shade;       /* 0 = cursor (head colour), 1..7 = rain -> tail */
    uint8_t flipped;     /* draw mirrored horizontally */
    uint8_t dissolving;  /* draw swap_from and glyph both at 50% this frame */
    uint8_t visible;     /* 0 = draw background only */
} rain_cell_view;

/* Grid for a w x h pixel surface with square cells of `cell` pixels. */
void rain_grid_for(int w, int h, int cell, int *cols, int *rows);

rain_t *rain_create(int cols, int rows, const rain_config *cfg, uint32_t seed);
void rain_destroy(rain_t *r);
int rain_resize(rain_t *r, int cols, int rows); /* re-seeds; 0 on success */
void rain_tick(rain_t *r);

int rain_cols(const rain_t *r);
int rain_rows(const rain_t *r);
int rain_tick_count(const rain_t *r);
const rain_config *rain_cfg(const rain_t *r);
rain_cell_view rain_cell(const rain_t *r, int x, int y);

/* Tick the glyph in a cell was written (for tests). */
int rain_cell_born(const rain_t *r, int x, int y);

#endif
