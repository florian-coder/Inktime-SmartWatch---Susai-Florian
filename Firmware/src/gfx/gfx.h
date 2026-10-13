/* 1-bit framebuffer for the 200x200 e-paper. Bit = 1 means black, MSB is the leftmost pixel. */
#ifndef GFX_GFX_H
#define GFX_GFX_H

#include <stdbool.h>
#include <stdint.h>

#define GFX_W      200
#define GFX_H      200
#define GFX_STRIDE (GFX_W / 8)
#define GFX_BYTES  (GFX_STRIDE * GFX_H)

#define FONT_W 6 /* 5x7 glyph in a 6x8 cell */
#define FONT_H 8

typedef struct {
    uint8_t px[GFX_BYTES];
} gfx_t;

typedef enum {
    ICON_BATTERY_BOLT,
    ICON_STEPS,
    ICON_BELL,
    ICON_MAIL,
} gfx_icon_t;

void gfx_clear(gfx_t *g);
void gfx_pixel(gfx_t *g, int x, int y, bool black);
bool gfx_get(const gfx_t *g, int x, int y);
void gfx_hline(gfx_t *g, int x, int y, int w, bool black);
void gfx_vline(gfx_t *g, int x, int y, int h, bool black);
void gfx_rect(gfx_t *g, int x, int y, int w, int h, bool black);
void gfx_fill(gfx_t *g, int x, int y, int w, int h, bool black);
void gfx_invert(gfx_t *g, int x, int y, int w, int h);
void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, bool black);

int gfx_text_width(const char *s, int scale);
/* Draws text with its top-left corner at (x, y). Returns the x after the last glyph. */
int gfx_text(gfx_t *g, int x, int y, const char *s, int scale, bool black);
void gfx_text_center(gfx_t *g, int y, const char *s, int scale, bool black);
void gfx_text_right(gfx_t *g, int right_x, int y, const char *s, int scale, bool black);

/* Seven-segment digit (0-9; anything else draws nothing). */
void gfx_seg_digit(gfx_t *g, int x, int y, int w, int h, int thick, int digit);
/* Battery outline with fill level 0..100. */
void gfx_battery(gfx_t *g, int x, int y, int w, int h, int pct);
void gfx_icon(gfx_t *g, int x, int y, gfx_icon_t icon);
/* Horizontal progress bar with border, fill 0..1000 permille. */
void gfx_progress(gfx_t *g, int x, int y, int w, int h, int permille);

uint32_t gfx_count_black(const gfx_t *g);

#endif
