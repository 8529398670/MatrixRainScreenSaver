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

/* Coverage masks: one cell x cell byte image per glyph, plus mirrored
 * copies. Row 0 is the top of the glyph. */
typedef struct rain_atlas {
    int cell;
    int glyph_count;
    uint8_t *masks; /* glyph_count * 2 * cell * cell */
} rain_atlas;

rain_atlas *atlas_create(int cell, int glyph_count);
void atlas_destroy(rain_atlas *a);
uint8_t *atlas_mask(rain_atlas *a, int glyph, int flipped);
/* Call after rasterizing every un-flipped mask: builds the mirrored ones. */
void atlas_finish(rain_atlas *a);

typedef struct rain_renderer rain_renderer;

rain_renderer *renderer_create(const rain_config *cfg, rain_atlas *atlas);
void renderer_destroy(rain_renderer *rr);

/* Draw the current frame. Only cells that changed since the last call are
 * repainted unless `full` is set (first frame, after a resize or expose).
 * px is w x h with a row stride of stride_px pixels. Returns the number
 * of cells repainted. */
int renderer_draw(rain_renderer *rr, const rain_t *r, uint32_t *px, int w, int h, int stride_px, int full);

/* The 8 shade colours: [0] head, [1..7] rain -> tail. */
void rain_shade_colors(const rain_config *cfg, uint32_t out[8]);

#endif
