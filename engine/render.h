/* render.h - draws the rain into a 32-bit pixel buffer, the same way on
 * every OS. Each shell only has to (1) rasterize every glyph once into an
 * 8-bit coverage mask with its native font API, and (2) put the finished
 * pixel buffer on screen.
 *
 * Pixels are uint32_t 0x00RRGGBB (little-endian bytes B,G,R,X): a Windows
 * 32bpp DIB, a TrueColor X11 ZPixmap, and a CGImage with
 * kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst all read it
 * directly.
 */
#ifndef MATRIXRAIN_RENDER_H
#define MATRIXRAIN_RENDER_H

#include <stdint.h>
#include "rain.h"

#define RAIN_VARIANTS 3   /* as drawn, mirrored left-right, upside down */
#define RAIN_SHADES 9     /* see rain_cell_view.shade */

/* Coverage masks: one cell_w x cell_h byte image per glyph and variant.
 * Row 0 is the top of the glyph; a row is cell_w bytes. */
typedef struct rain_atlas {
    int cell_w, cell_h;
    int glyph_count;
    uint8_t *masks; /* glyph_count * RAIN_VARIANTS * cell_w * cell_h */
} rain_atlas;

rain_atlas *atlas_create(int cell_w, int cell_h, int glyph_count);
void atlas_destroy(rain_atlas *a);
uint8_t *atlas_mask(rain_atlas *a, int glyph, int variant);
/* Call after rasterizing every variant-0 mask: builds the other variants. */
void atlas_finish(rain_atlas *a);

/* Cell size for a font size in device pixels and cfg->cell_width_percent. */
void rain_cell_size(const rain_config *cfg, int font_px, int *cell_w, int *cell_h);

typedef struct rain_renderer rain_renderer;

rain_renderer *renderer_create(const rain_config *cfg, rain_atlas *atlas);
void renderer_destroy(rain_renderer *rr);

/* Draw the current frame. Only cells that changed since the last call are
 * repainted unless `full` is set (first frame, after a resize or expose);
 * with glow on, the glow is then recomputed over the whole frame.
 * px is w x h with a row stride of stride_px pixels. Returns the number
 * of cells repainted. */
int renderer_draw(rain_renderer *rr, const rain_t *r, uint32_t *px, int w, int h, int stride_px, int full);

/* The shade colours of colour set `palette` (config_palette):
 * [0] head, [1..7] rain -> tail, [8] half highlight. */
void rain_shade_colors(const rain_config *cfg, int palette, uint32_t out[RAIN_SHADES]);

#endif
