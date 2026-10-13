#include "gfx.h"

#include <stdlib.h>
#include <string.h>

extern const uint8_t font5x7[95][5];

void gfx_clear(gfx_t *g)
{
    memset(g->px, 0, sizeof(g->px));
}

void gfx_pixel(gfx_t *g, int x, int y, bool black)
{
    if (x < 0 || y < 0 || x >= GFX_W || y >= GFX_H) {
        return;
    }
    uint8_t *b = &g->px[y * GFX_STRIDE + x / 8];
    uint8_t m = (uint8_t)(0x80u >> (x & 7));
    if (black) {
        *b |= m;
    } else {
        *b &= (uint8_t)~m;
    }
}

bool gfx_get(const gfx_t *g, int x, int y)
{
    if (x < 0 || y < 0 || x >= GFX_W || y >= GFX_H) {
        return false;
    }
    return (g->px[y * GFX_STRIDE + x / 8] & (0x80u >> (x & 7))) != 0;
}

void gfx_fill(gfx_t *g, int x, int y, int w, int h, bool black)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > GFX_W ? GFX_W : x + w;
    int y1 = y + h > GFX_H ? GFX_H : y + h;
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            gfx_pixel(g, xx, yy, black);
        }
    }
}

void gfx_hline(gfx_t *g, int x, int y, int w, bool black)
{
    gfx_fill(g, x, y, w, 1, black);
}

void gfx_vline(gfx_t *g, int x, int y, int h, bool black)
{
    gfx_fill(g, x, y, 1, h, black);
}

void gfx_rect(gfx_t *g, int x, int y, int w, int h, bool black)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    gfx_hline(g, x, y, w, black);
    gfx_hline(g, x, y + h - 1, w, black);
    gfx_vline(g, x, y, h, black);
    gfx_vline(g, x + w - 1, y, h, black);
}

void gfx_invert(gfx_t *g, int x, int y, int w, int h)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            gfx_pixel(g, xx, yy, !gfx_get(g, xx, yy));
        }
    }
}

void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, bool black)
{
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(g, x0, y0, black);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void draw_glyph(gfx_t *g, int x, int y, char c, int scale, bool black)
{
    if (c < 0x20 || c > 0x7E) {
        c = '?';
    }
    const uint8_t *cols = font5x7[c - 0x20];
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (cols[col] & (1u << row)) {
                gfx_fill(g, x + col * scale, y + row * scale, scale, scale, black);
            }
        }
    }
}

int gfx_text_width(const char *s, int scale)
{
    int n = (int)strlen(s);
    return n == 0 ? 0 : n * FONT_W * scale - scale; /* no trailing spacing column */
}

int gfx_text(gfx_t *g, int x, int y, const char *s, int scale, bool black)
{
    for (; *s; s++) {
        draw_glyph(g, x, y, *s, scale, black);
        x += FONT_W * scale;
    }
    return x;
}

void gfx_text_center(gfx_t *g, int y, const char *s, int scale, bool black)
{
    gfx_text(g, (GFX_W - gfx_text_width(s, scale)) / 2, y, s, scale, black);
}

void gfx_text_right(gfx_t *g, int right_x, int y, const char *s, int scale, bool black)
{
    gfx_text(g, right_x - gfx_text_width(s, scale), y, s, scale, black);
}

/* Segment bits: a=0 b=1 c=2 d=3 e=4 f=5 g=6 */
static const uint8_t seg_map[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

/* Segments taper towards their ends, like a classic LCD. */
static void seg_h(gfx_t *g, int x, int y, int w, int t)
{
    for (int r = 0; r < t; r++) {
        int inset = abs(2 * r - (t - 1)) / 2;
        gfx_hline(g, x + inset, y + r, w - 2 * inset, true);
    }
}

static void seg_v(gfx_t *g, int x, int y, int h, int t)
{
    for (int c = 0; c < t; c++) {
        int inset = abs(2 * c - (t - 1)) / 2;
        gfx_vline(g, x + c, y + inset, h - 2 * inset, true);
    }
}

void gfx_seg_digit(gfx_t *g, int x, int y, int w, int h, int t, int digit)
{
    if (digit < 0 || digit > 9) {
        return;
    }
    const int gap = 1;
    const uint8_t s = seg_map[digit];
    const int half = t / 2;
    const int mid = y + (h - t) / 2; /* top of the middle bar */
    const int hx = x + half + gap;   /* horizontal bars span between vertical centres */
    const int hw = w - 2 * half - 2 * gap;
    const int up_y = y + half + gap;
    const int up_h = mid + half - up_y - gap;
    const int lo_y = mid + half + gap;
    const int lo_h = y + h - half - gap - lo_y;

    if (s & 0x01) seg_h(g, hx, y, hw, t);
    if (s & 0x40) seg_h(g, hx, mid, hw, t);
    if (s & 0x08) seg_h(g, hx, y + h - t, hw, t);
    if (s & 0x20) seg_v(g, x, up_y, up_h, t);
    if (s & 0x02) seg_v(g, x + w - t, up_y, up_h, t);
    if (s & 0x10) seg_v(g, x, lo_y, lo_h, t);
    if (s & 0x04) seg_v(g, x + w - t, lo_y, lo_h, t);
}

void gfx_battery(gfx_t *g, int x, int y, int w, int h, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    gfx_rect(g, x, y, w - 3, h, true);
    gfx_rect(g, x + 1, y + 1, w - 5, h - 2, true);
    gfx_fill(g, x + w - 3, y + h / 4, 3, h - 2 * (h / 4), true);
    int inner = w - 9;
    gfx_fill(g, x + 3, y + 3, inner * pct / 100, h - 6, true);
}

void gfx_progress(gfx_t *g, int x, int y, int w, int h, int permille)
{
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    gfx_rect(g, x, y, w, h, true);
    gfx_fill(g, x + 2, y + 2, (w - 4) * permille / 1000, h - 4, true);
}

/* 16x16 icons, '#' = black. */
static const char *const icon_bolt[16] = {
    "..........##....", ".........###....", "........###.....", ".......###......",
    "......###.......", ".....###........", "....##########..", "...##########...",
    "........###.....", ".......###......", "......###.......", ".....###........",
    "....###.........", "....##..........", "...##...........", "...#............",
};
static const char *const icon_steps[16] = {
    "................", "...###..........", "..#####.........", "..#####.........",
    "..#####....###..", "..#####...#####.", "...###....#####.", "..........#####.",
    "...###....#####.", "...###.....###..", "...###..........", "...........###..",
    "...........###..", "...........###..", "................", "................",
};
static const char *const icon_bell[16] = {
    ".......##.......", "......####......", "....########....", "...##########...",
    "...##########...", "...##########...", "...##########...", "...##########...",
    "..############..", "..############..", ".##############.", ".##############.",
    "................", "......####......", ".......##.......", "................",
};
static const char *const icon_mail[16] = {
    "................", "................", "................", ".##############.",
    ".##..........##.", ".#.#........#.#.", ".#..#......#..#.", ".#...#....#...#.",
    ".#....#..#....#.", ".#.....##.....#.", ".#............#.", ".#............#.",
    ".##############.", "................", "................", "................",
};

void gfx_icon(gfx_t *g, int x, int y, gfx_icon_t icon)
{
    const char *const *rows;
    switch (icon) {
    case ICON_BATTERY_BOLT: rows = icon_bolt; break;
    case ICON_STEPS: rows = icon_steps; break;
    case ICON_BELL: rows = icon_bell; break;
    case ICON_MAIL: rows = icon_mail; break;
    default: return;
    }
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 16; c++) {
            if (rows[r][c] == '#') {
                gfx_pixel(g, x + c, y + r, true);
            }
        }
    }
}

uint32_t gfx_count_black(const gfx_t *g)
{
    uint32_t n = 0;
    for (int i = 0; i < GFX_BYTES; i++) {
        n += (uint32_t)__builtin_popcount(g->px[i]);
    }
    return n;
}
