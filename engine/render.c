/* render.c - software compositor shared by every shell. */
#include "render.h"

#include <stdlib.h>
#include <string.h>

rain_atlas *atlas_create(int cell, int glyph_count)
{
    rain_atlas *a = (rain_atlas *)calloc(1, sizeof *a);
    if (!a) return NULL;
    if (cell < 1) cell = 1;
    if (glyph_count < 1) glyph_count = 1;
    a->cell = cell;
    a->glyph_count = glyph_count;
    a->masks = (uint8_t *)calloc((size_t)glyph_count * 2 * (size_t)cell * (size_t)cell, 1);
    if (!a->masks) { free(a); return NULL; }
    return a;
}

void atlas_destroy(rain_atlas *a)
{
    if (!a) return;
    free(a->masks);
    free(a);
}

uint8_t *atlas_mask(rain_atlas *a, int glyph, int flipped)
{
    size_t sz = (size_t)a->cell * (size_t)a->cell;
    if (glyph < 0 || glyph >= a->glyph_count) glyph = 0;
    return a->masks + ((size_t)glyph * 2 + (flipped ? 1 : 0)) * sz;
}

void atlas_finish(rain_atlas *a)
{
    int g, x, y, n = a->cell;
    for (g = 0; g < a->glyph_count; g++) {
        const uint8_t *src = atlas_mask(a, g, 0);
        uint8_t *dst = atlas_mask(a, g, 1);
        for (y = 0; y < n; y++)
            for (x = 0; x < n; x++)
                dst[y * n + x] = src[y * n + (n - 1 - x)];
    }
}

static uint32_t lerp_color(uint32_t a, uint32_t b, double t)
{
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    int r = (int)(ar + (br - ar) * t), g = (int)(ag + (bg - ag) * t), bl = (int)(ab + (bb - ab) * t);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

void rain_shade_colors(const rain_config *cfg, uint32_t out[8])
{
    int i;
    out[0] = cfg->head_color;
    for (i = 1; i < 8; i++) out[i] = lerp_color(cfg->rain_color, cfg->tail_color, (i - 1) / 6.0);
}

struct rain_renderer {
    rain_atlas *atlas;
    uint32_t shades[8];
    uint32_t bg;
    uint64_t *last;   /* last drawn state per cell */
    int cols, rows;
};

rain_renderer *renderer_create(const rain_config *cfg, rain_atlas *atlas)
{
    rain_renderer *rr = (rain_renderer *)calloc(1, sizeof *rr);
    if (!rr) return NULL;
    rr->atlas = atlas;
    rain_shade_colors(cfg, rr->shades);
    rr->bg = cfg->background_color & 0xFFFFFF;
    return rr;
}

void renderer_destroy(rain_renderer *rr)
{
    if (!rr) return;
    free(rr->last);
    free(rr);
}

static uint64_t cell_key(const rain_cell_view *v)
{
    if (!v->visible) return 0;
    return (1ULL << 63) | ((uint64_t)v->glyph << 32) |
           ((uint64_t)(v->dissolving ? v->swap_from : 0) << 16) |
           ((uint64_t)v->shade << 8) | ((uint64_t)v->flipped << 1) | (uint64_t)v->dissolving;
}

/* dst = dst + (color - dst) * coverage * alpha */
static void blend_mask(uint32_t *px, int stride, int x0, int y0, int cw, int ch,
                       const uint8_t *mask, int cell, uint32_t color, unsigned alpha)
{
    int cr = (color >> 16) & 255, cg = (color >> 8) & 255, cb = color & 255;
    int x, y;
    for (y = 0; y < ch; y++) {
        uint32_t *row = px + (size_t)(y0 + y) * (size_t)stride + x0;
        const uint8_t *m = mask + y * cell;
        for (x = 0; x < cw; x++) {
            unsigned a = m[x];
            uint32_t d;
            int dr, dg, db;
            if (!a) continue;
            a = (a * alpha + 127) / 255;
            d = row[x];
            dr = (d >> 16) & 255; dg = (d >> 8) & 255; db = d & 255;
            dr += ((cr - dr) * (int)a) / 255;
            dg += ((cg - dg) * (int)a) / 255;
            db += ((cb - db) * (int)a) / 255;
            row[x] = ((uint32_t)dr << 16) | ((uint32_t)dg << 8) | (uint32_t)db;
        }
    }
}

int renderer_draw(rain_renderer *rr, const rain_t *r, uint32_t *px, int w, int h, int stride_px, int full)
{
    int cols = rain_cols(r), rows = rain_rows(r), cell = rr->atlas->cell;
    int x, y, painted = 0;

    if (cols != rr->cols || rows != rr->rows || !rr->last) {
        free(rr->last);
        rr->last = (uint64_t *)calloc((size_t)cols * (size_t)rows, sizeof(uint64_t));
        rr->cols = cols;
        rr->rows = rows;
        full = 1;
        if (!rr->last) return 0;
    }
    if (full) {
        for (y = 0; y < h; y++) {
            uint32_t *row = px + (size_t)y * (size_t)stride_px;
            for (x = 0; x < w; x++) row[x] = rr->bg;
        }
    }

    for (x = 0; x < cols; x++) {
        int x0 = x * cell, cw = cell;
        if (x0 >= w) break;
        if (x0 + cw > w) cw = w - x0;
        for (y = 0; y < rows; y++) {
            int y0 = y * cell, ch = cell, yy;
            rain_cell_view v;
            uint64_t key, *last;
            if (y0 >= h) break;
            if (y0 + ch > h) ch = h - y0;
            v = rain_cell(r, x, y);
            key = cell_key(&v);
            last = &rr->last[(size_t)x * (size_t)rows + (size_t)y];
            if (!full && key == *last) continue;
            *last = key;
            painted++;
            if (!full) {
                for (yy = 0; yy < ch; yy++) {
                    uint32_t *row = px + (size_t)(y0 + yy) * (size_t)stride_px + x0;
                    int xx;
                    for (xx = 0; xx < cw; xx++) row[xx] = rr->bg;
                }
            }
            if (!v.visible) continue;
            if (v.dissolving) {
                blend_mask(px, stride_px, x0, y0, cw, ch, atlas_mask(rr->atlas, v.swap_from, v.flipped), cell, rr->shades[v.shade], 128);
                blend_mask(px, stride_px, x0, y0, cw, ch, atlas_mask(rr->atlas, v.glyph, v.flipped), cell, rr->shades[v.shade], 128);
            } else {
                blend_mask(px, stride_px, x0, y0, cw, ch, atlas_mask(rr->atlas, v.glyph, v.flipped), cell, rr->shades[v.shade], 255);
            }
        }
    }
    return painted;
}
