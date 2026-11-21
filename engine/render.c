/* render.c - software compositor shared by every shell. */
#include "render.h"

#include <stdlib.h>
#include <string.h>

#if !defined(RAIN_NO_SIMD) && (defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64))
#include <emmintrin.h>
#define RAIN_SSE2 1
#else
#define RAIN_SSE2 0
#endif

rain_atlas *atlas_create(int cell_w, int cell_h, int glyph_count)
{
    rain_atlas *a = (rain_atlas *)calloc(1, sizeof *a);
    if (!a) return NULL;
    if (cell_w < 1) cell_w = 1;
    if (cell_h < 1) cell_h = 1;
    if (glyph_count < 1) glyph_count = 1;
    a->cell_w = cell_w;
    a->cell_h = cell_h;
    a->glyph_count = glyph_count;
    a->masks = (uint8_t *)calloc((size_t)glyph_count * RAIN_VARIANTS * (size_t)cell_w * (size_t)cell_h, 1);
    if (!a->masks) { free(a); return NULL; }
    return a;
}

void atlas_destroy(rain_atlas *a)
{
    if (!a) return;
    free(a->masks);
    free(a);
}

uint8_t *atlas_mask(rain_atlas *a, int glyph, int variant)
{
    size_t sz = (size_t)a->cell_w * (size_t)a->cell_h;
    if (glyph < 0 || glyph >= a->glyph_count) glyph = 0;
    if (variant < 0 || variant >= RAIN_VARIANTS) variant = 0;
    return a->masks + ((size_t)glyph * RAIN_VARIANTS + (size_t)variant) * sz;
}

void atlas_finish(rain_atlas *a)
{
    int g, x, y, w = a->cell_w, h = a->cell_h;
    for (g = 0; g < a->glyph_count; g++) {
        const uint8_t *src = atlas_mask(a, g, 0);
        uint8_t *mir = atlas_mask(a, g, 1), *up = atlas_mask(a, g, 2);
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                mir[y * w + x] = src[y * w + (w - 1 - x)];
                up[y * w + x] = src[(h - 1 - y) * w + x];
            }
    }
}

void rain_cell_size(const rain_config *cfg, int font_px, int *cell_w, int *cell_h)
{
    int pct = cfg->cell_width_percent > 0 ? cfg->cell_width_percent : 100;
    *cell_h = font_px < 1 ? 1 : font_px;
    *cell_w = (*cell_h * pct + 50) / 100;
    if (*cell_w < 1) *cell_w = 1;
}

static uint32_t lerp_color(uint32_t a, uint32_t b, double t)
{
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    int r = (int)(ar + (br - ar) * t), g = (int)(ag + (bg - ag) * t), bl = (int)(ab + (bb - ab) * t);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

void rain_shade_colors(const rain_config *cfg, int palette, uint32_t out[RAIN_SHADES])
{
    rain_palette p = config_palette(cfg, palette);
    int i;
    out[0] = p.head;
    for (i = 1; i < 8; i++) out[i] = lerp_color(p.rain, p.tail, (i - 1) / 6.0);
    out[8] = lerp_color(p.head, p.rain, 0.5);
}

/* Glow: the frame is shrunk GLOW_DOWN times, box-blurred, and the blur is
 * added back on top with bilinear filtering. */
#define GLOW_DOWN 4

struct rain_renderer {
    rain_atlas *atlas;
    uint32_t shades[RAIN_MAX_MIX][RAIN_SHADES];
    uint32_t bg;
    uint64_t *last;   /* last drawn state per cell */
    int cols, rows;
    int glow;         /* 0..100 */
    uint32_t *base;   /* the frame without glow (only when glow is on) */
    int bw, bh;
    uint32_t *small, *tmp;
    int sw, sh;
    int *xtab;        /* per output column: x0 << 8 | weight */
    int *colsum;      /* sw * 3 running sums for the vertical blur */
};

rain_renderer *renderer_create(const rain_config *cfg, rain_atlas *atlas)
{
    rain_renderer *rr = (rain_renderer *)calloc(1, sizeof *rr);
    int i;
    if (!rr) return NULL;
    rr->atlas = atlas;
    for (i = 0; i < RAIN_MAX_MIX; i++) rain_shade_colors(cfg, i, rr->shades[i]);
    rr->bg = cfg->background_color & 0xFFFFFF;
    rr->glow = cfg->glow_percent < 0 ? 0 : cfg->glow_percent > 100 ? 100 : cfg->glow_percent;
    return rr;
}

static void free_glow(rain_renderer *rr)
{
    free(rr->base); free(rr->small); free(rr->tmp); free(rr->xtab); free(rr->colsum);
    rr->base = rr->small = rr->tmp = NULL;
    rr->xtab = rr->colsum = NULL;
    rr->bw = rr->bh = rr->sw = rr->sh = 0;
}

void renderer_destroy(rain_renderer *rr)
{
    if (!rr) return;
    free(rr->last);
    free_glow(rr);
    free(rr);
}

static uint64_t cell_key(const rain_cell_view *v)
{
    if (!v->visible) return 0;
    return (1ULL << 63) | ((uint64_t)v->glyph << 32) |
           ((uint64_t)(v->dissolving ? v->swap_from : 0) << 16) |
           ((uint64_t)v->shade << 8) | ((uint64_t)v->palette << 4) | ((uint64_t)v->flipped << 1) | (uint64_t)v->dissolving;
}

/* dst = dst + (color - dst) * coverage. With m2, coverage is the two
 * glyphs at 50% each, summed and capped: where their strokes overlap the
 * cell is fully lit, like two film frames double-exposed. */
static void blend_mask(uint32_t *px, int stride, int x0, int y0, int cw, int ch,
                       const uint8_t *m1, const uint8_t *m2, int mask_w, uint32_t color)
{
    int cr = (color >> 16) & 255, cg = (color >> 8) & 255, cb = color & 255;
    int x, y;
    for (y = 0; y < ch; y++) {
        uint32_t *row = px + (size_t)(y0 + y) * (size_t)stride + x0;
        const uint8_t *a1 = m1 + y * mask_w, *a2 = m2 ? m2 + y * mask_w : NULL;
        for (x = 0; x < cw; x++) {
            unsigned a = a1[x];
            uint32_t d;
            int dr, dg, db;
            if (a2) { a = (a + a2[x] + 1) / 2; }
            if (!a) continue;
            d = row[x];
            dr = (d >> 16) & 255; dg = (d >> 8) & 255; db = d & 255;
            dr += ((cr - dr) * (int)a) / 255;
            dg += ((cg - dg) * (int)a) / 255;
            db += ((cb - db) * (int)a) / 255;
            row[x] = ((uint32_t)dr << 16) | ((uint32_t)dg << 8) | (uint32_t)db;
        }
    }
}

/* ---- glow ------------------------------------------------------------- */

static int glow_alloc(rain_renderer *rr, int w, int h)
{
    int x;
    if (rr->base && rr->bw == w && rr->bh == h) return 0;
    free_glow(rr);
    rr->bw = w; rr->bh = h;
    rr->sw = (w + GLOW_DOWN - 1) / GLOW_DOWN;
    rr->sh = (h + GLOW_DOWN - 1) / GLOW_DOWN;
    rr->base = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    rr->small = (uint32_t *)malloc((size_t)rr->sw * (size_t)rr->sh * sizeof(uint32_t));
    rr->tmp = (uint32_t *)malloc((size_t)rr->sw * (size_t)rr->sh * sizeof(uint32_t));
    rr->xtab = (int *)malloc((size_t)w * sizeof(int));
    rr->colsum = (int *)malloc((size_t)rr->sw * 3 * sizeof(int));
    if (!rr->base || !rr->small || !rr->tmp || !rr->xtab || !rr->colsum) { free_glow(rr); return -1; }
    for (x = 0; x < w; x++) {
        /* source position (x + 0.5) / D - 0.5 in 1/256ths */
        int s = ((2 * x + 1) * 256) / (2 * GLOW_DOWN) - 128;
        int x0, f;
        if (s < 0) s = 0;
        x0 = s >> 8; f = s & 255;
        if (x0 >= rr->sw - 1) { x0 = rr->sw - 1; f = 0; }
        rr->xtab[x] = (x0 << 8) | f;
    }
    return 1; /* new buffers: caller must redraw everything */
}

/* Shrink base into small (box average of each D x D block, background
 * subtracted so only the lit glyphs glow). */
static void glow_downsample(rain_renderer *rr)
{
    int sx, sy, br = (rr->bg >> 16) & 255, bgc = (rr->bg >> 8) & 255, bb = rr->bg & 255;
    if (rr->bg == 0) {
        /* Black background (the usual case): sum two channels per add.
         * A lane holds at most 16 * 255, so nothing carries over. */
        for (sy = 0; sy < rr->sh; sy++)
            for (sx = 0; sx < rr->sw; sx++) {
                uint32_t rb = 0, g = 0;
                int x, y, n = 0;
                for (y = sy * GLOW_DOWN; y < sy * GLOW_DOWN + GLOW_DOWN && y < rr->bh; y++) {
                    const uint32_t *row = rr->base + (size_t)y * (size_t)rr->bw;
                    int xe = sx * GLOW_DOWN + GLOW_DOWN < rr->bw ? sx * GLOW_DOWN + GLOW_DOWN : rr->bw;
                    for (x = sx * GLOW_DOWN; x < xe; x++) {
                        rb += row[x] & 0x00FF00FFu;
                        g += (row[x] >> 8) & 0xFFu;
                        n++;
                    }
                }
                if (n == GLOW_DOWN * GLOW_DOWN) {
                    rb = (rb >> 4) & 0x00FF00FFu;
                    g >>= 4;
                } else if (n) {
                    rb = ((((rb >> 16) & 0xFFFF) / n) << 16) | ((rb & 0xFFFF) / n);
                    g /= n;
                }
                rr->small[(size_t)sy * (size_t)rr->sw + (size_t)sx] = rb | (g << 8);
            }
        return;
    }
    for (sy = 0; sy < rr->sh; sy++)
        for (sx = 0; sx < rr->sw; sx++) {
            int x, y, n = 0, r = 0, g = 0, b = 0;
            for (y = sy * GLOW_DOWN; y < sy * GLOW_DOWN + GLOW_DOWN && y < rr->bh; y++) {
                const uint32_t *row = rr->base + (size_t)y * (size_t)rr->bw;
                for (x = sx * GLOW_DOWN; x < sx * GLOW_DOWN + GLOW_DOWN && x < rr->bw; x++) {
                    uint32_t p = row[x];
                    int pr = (int)((p >> 16) & 255) - br, pg = (int)((p >> 8) & 255) - bgc, pb = (int)(p & 255) - bb;
                    r += pr > 0 ? pr : 0; g += pg > 0 ? pg : 0; b += pb > 0 ? pb : 0;
                    n++;
                }
            }
            if (n) { r /= n; g /= n; b /= n; }
            rr->small[(size_t)sy * (size_t)rr->sw + (size_t)sx] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
}

/* One box-blur pass of radius rad along a line of n pixels, step apart. */
static void blur_line(const uint32_t *src, uint32_t *dst, int n, int step, int rad)
{
    int i, sr = 0, sg = 0, sb = 0, div = 2 * rad + 1;
    for (i = -rad; i <= rad; i++) {
        uint32_t p = src[(size_t)(i < 0 ? 0 : i >= n ? n - 1 : i) * (size_t)step];
        sr += (p >> 16) & 255; sg += (p >> 8) & 255; sb += p & 255;
    }
    for (i = 0; i < n; i++) {
        uint32_t add, sub;
        int ia = i + rad + 1, is = i - rad;
        dst[(size_t)i * (size_t)step] = ((uint32_t)(sr / div) << 16) | ((uint32_t)(sg / div) << 8) | (uint32_t)(sb / div);
        add = src[(size_t)(ia >= n ? n - 1 : ia) * (size_t)step];
        sub = src[(size_t)(is < 0 ? 0 : is) * (size_t)step];
        sr += (int)((add >> 16) & 255) - (int)((sub >> 16) & 255);
        sg += (int)((add >> 8) & 255) - (int)((sub >> 8) & 255);
        sb += (int)(add & 255) - (int)(sub & 255);
    }
}

/* Vertical box blur of src into dst, walking rows in order (cache
 * friendly) with one running sum per column and channel. */
static void blur_cols(const uint32_t *src, uint32_t *dst, int w, int h, int rad, int *sum)
{
    int x, y, i, div = 2 * rad + 1;
    memset(sum, 0, (size_t)w * 3 * sizeof(int));
    for (i = -rad; i <= rad; i++) {
        const uint32_t *row = src + (size_t)(i < 0 ? 0 : i >= h ? h - 1 : i) * (size_t)w;
        for (x = 0; x < w; x++) {
            sum[3 * x] += (row[x] >> 16) & 255; sum[3 * x + 1] += (row[x] >> 8) & 255; sum[3 * x + 2] += row[x] & 255;
        }
    }
    for (y = 0; y < h; y++) {
        int ia = y + rad + 1, is = y - rad;
        const uint32_t *add = src + (size_t)(ia >= h ? h - 1 : ia) * (size_t)w;
        const uint32_t *sub = src + (size_t)(is < 0 ? 0 : is) * (size_t)w;
        uint32_t *out = dst + (size_t)y * (size_t)w;
        for (x = 0; x < w; x++) {
            int *s = sum + 3 * x;
            out[x] = ((uint32_t)(s[0] / div) << 16) | ((uint32_t)(s[1] / div) << 8) | (uint32_t)(s[2] / div);
            s[0] += (int)((add[x] >> 16) & 255) - (int)((sub[x] >> 16) & 255);
            s[1] += (int)((add[x] >> 8) & 255) - (int)((sub[x] >> 8) & 255);
            s[2] += (int)(add[x] & 255) - (int)(sub[x] & 255);
        }
    }
}

static void glow_blur(rain_renderer *rr, int rad)
{
    int pass, y;
    for (pass = 0; pass < 2; pass++) {
        for (y = 0; y < rr->sh; y++)
            blur_line(rr->small + (size_t)y * rr->sw, rr->tmp + (size_t)y * rr->sw, rr->sw, 1, rad);
        blur_cols(rr->tmp, rr->small, rr->sw, rr->sh, rad, rr->colsum);
    }
}

/* Scale the blurred image by the glow strength (glow_percent 25 = 1x;
 * thin strokes average out dim, so it needs gain), capped at 255 per
 * channel. Done once per small pixel, not per output pixel. */
static void glow_gain(rain_renderer *rr)
{
    int k = rr->glow * 4 * 256 / 100;
    size_t i, n = (size_t)rr->sw * (size_t)rr->sh;
    for (i = 0; i < n; i++) {
        uint32_t p = rr->small[i], out = 0;
        int c;
        for (c = 0; c <= 16; c += 8) {
            int v = (int)(((p >> c) & 255) * (uint32_t)k) >> 8;
            out |= (uint32_t)(v > 255 ? 255 : v) << c;
        }
        rr->small[i] = out;
    }
}

/* px = base + glow, the glow upsampled bilinearly. Two channels per
 * integer op (R and B share a word, G alone), saturating adds. */
static void glow_composite(rain_renderer *rr, uint32_t *px, int stride)
{
    int x, y, sx;
    uint32_t *row = rr->tmp; /* sw entries: this output row, vertically filtered */
    for (y = 0; y < rr->bh; y++) {
        int s = ((2 * y + 1) * 256) / (2 * GLOW_DOWN) - 128, y0, fy;
        const uint32_t *r0, *r1, *src = rr->base + (size_t)y * (size_t)rr->bw;
        uint32_t *dst = px + (size_t)y * (size_t)stride;
        if (s < 0) s = 0;
        y0 = s >> 8; fy = s & 255;
        if (y0 >= rr->sh - 1) { y0 = rr->sh - 1; fy = 0; }
        r0 = rr->small + (size_t)y0 * rr->sw;
        r1 = rr->small + (size_t)(y0 + 1 < rr->sh ? y0 + 1 : y0) * rr->sw;
        for (sx = 0; sx < rr->sw; sx++) {
            uint32_t a = r0[sx], b = r1[sx];
            uint32_t rb = (((a & 0x00FF00FFu) * (uint32_t)(256 - fy) + (b & 0x00FF00FFu) * (uint32_t)fy) >> 8) & 0x00FF00FFu;
            uint32_t g = (((a & 0x0000FF00u) * (uint32_t)(256 - fy) + (b & 0x0000FF00u) * (uint32_t)fy) >> 8) & 0x0000FF00u;
            row[sx] = rb | g;
        }
        x = 0;
#if RAIN_SSE2
        {
            /* 4 pixels at a time: expand to 16-bit lanes, lerp, pack, and a
             * saturating byte add onto the base frame. */
            const __m128i zero = _mm_setzero_si128();
            for (; x + 4 <= rr->bw; x += 4) {
                const int *t = rr->xtab + x;
                int i0 = t[0] >> 8, i1 = t[1] >> 8, i2 = t[2] >> 8, i3 = t[3] >> 8, last = rr->sw - 1;
                short f0 = (short)(t[0] & 255), f1 = (short)(t[1] & 255), f2 = (short)(t[2] & 255), f3 = (short)(t[3] & 255);
                __m128i a = _mm_set_epi32((int)row[i3], (int)row[i2], (int)row[i1], (int)row[i0]);
                __m128i b = _mm_set_epi32((int)row[i3 < last ? i3 + 1 : i3], (int)row[i2 < last ? i2 + 1 : i2],
                                          (int)row[i1 < last ? i1 + 1 : i1], (int)row[i0 < last ? i0 + 1 : i0]);
                __m128i flo = _mm_set_epi16(f1, f1, f1, f1, f0, f0, f0, f0);
                __m128i fhi = _mm_set_epi16(f3, f3, f3, f3, f2, f2, f2, f2);
                __m128i k256 = _mm_set1_epi16(256);
                __m128i lo = _mm_add_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(a, zero), _mm_sub_epi16(k256, flo)),
                                           _mm_mullo_epi16(_mm_unpacklo_epi8(b, zero), flo));
                __m128i hi = _mm_add_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(a, zero), _mm_sub_epi16(k256, fhi)),
                                           _mm_mullo_epi16(_mm_unpackhi_epi8(b, zero), fhi));
                __m128i glow = _mm_packus_epi16(_mm_srli_epi16(lo, 8), _mm_srli_epi16(hi, 8));
                __m128i base = _mm_loadu_si128((const __m128i *)(src + x));
                _mm_storeu_si128((__m128i *)(dst + x), _mm_adds_epu8(base, glow));
            }
        }
#endif
        for (; x < rr->bw; x++) {
            int t = rr->xtab[x], x0 = t >> 8;
            uint32_t fx = (uint32_t)(t & 255), a = row[x0], b = row[x0 + 1 < rr->sw ? x0 + 1 : x0], p = src[x];
            uint32_t rb = (((a & 0x00FF00FFu) * (256 - fx) + (b & 0x00FF00FFu) * fx) >> 8) & 0x00FF00FFu;
            uint32_t g = (((a & 0x0000FF00u) * (256 - fx) + (b & 0x0000FF00u) * fx) >> 8) & 0x0000FF00u;
            uint32_t srb = (p & 0x00FF00FFu) + rb, sg = (p & 0x0000FF00u) + g;
            srb = (srb | ((srb >> 8) & 0x00010001u) * 0xFFu) & 0x00FF00FFu;
            sg = (sg | ((sg >> 16) & 1u) * 0xFF00u) & 0x0000FF00u;
            dst[x] = srb | sg;
        }
    }
}

/* ---- draw ------------------------------------------------------------- */

int renderer_draw(rain_renderer *rr, const rain_t *r, uint32_t *px, int w, int h, int stride_px, int full)
{
    int cols = rain_cols(r), rows = rain_rows(r);
    int cell_w = rr->atlas->cell_w, cell_h = rr->atlas->cell_h;
    int x, y, painted = 0;
    uint32_t *target = px;
    int stride = stride_px;

    if (cols != rr->cols || rows != rr->rows || !rr->last) {
        free(rr->last);
        rr->last = (uint64_t *)calloc((size_t)cols * (size_t)rows, sizeof(uint64_t));
        rr->cols = cols;
        rr->rows = rows;
        full = 1;
        if (!rr->last) return 0;
    }
    if (rr->glow > 0) {
        int g = glow_alloc(rr, w, h);
        if (g < 0) rr->glow = 0; /* out of memory: draw without glow */
        else {
            if (g > 0) full = 1;
            target = rr->base;
            stride = w;
        }
    }
    if (full) {
        for (y = 0; y < h; y++) {
            uint32_t *row = target + (size_t)y * (size_t)stride;
            for (x = 0; x < w; x++) row[x] = rr->bg;
        }
    }

    for (x = 0; x < cols; x++) {
        int x0 = x * cell_w, cw = cell_w;
        if (x0 >= w) break;
        if (x0 + cw > w) cw = w - x0;
        for (y = 0; y < rows; y++) {
            int y0 = y * cell_h, ch = cell_h, yy;
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
                    uint32_t *row = target + (size_t)(y0 + yy) * (size_t)stride + x0;
                    int xx;
                    for (xx = 0; xx < cw; xx++) row[xx] = rr->bg;
                }
            }
            if (!v.visible) continue;
            blend_mask(target, stride, x0, y0, cw, ch, atlas_mask(rr->atlas, v.glyph, v.flipped),
                       v.dissolving ? atlas_mask(rr->atlas, v.swap_from, v.flipped) : NULL,
                       cell_w, rr->shades[v.palette < RAIN_MAX_MIX ? v.palette : 0][v.shade < RAIN_SHADES ? v.shade : 1]);
        }
    }

    if (rr->glow > 0 && (painted || full)) {
        int rad = (cell_h * 3) / (4 * GLOW_DOWN); /* about 3/4 of a glyph wide */
        glow_downsample(rr);
        glow_blur(rr, rad < 1 ? 1 : rad);
        glow_gain(rr);
        glow_composite(rr, px, stride_px);
    }
    return painted;
}
