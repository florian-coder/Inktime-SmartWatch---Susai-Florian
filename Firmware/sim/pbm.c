#include "pbm.h"

#include <stdio.h>
#include <string.h>

bool pbm_write(const char *path, const gfx_t *g)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    fprintf(f, "P4\n%d %d\n", GFX_W, GFX_H);
    bool ok = fwrite(g->px, 1, GFX_BYTES, f) == GFX_BYTES;
    return fclose(f) == 0 && ok;
}

bool pbm_read(const char *path, gfx_t *g)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    int w = 0, h = 0;
    char magic[3] = { 0 };
    bool ok = fscanf(f, "%2s %d %d", magic, &w, &h) == 3 && strcmp(magic, "P4") == 0 && w == GFX_W && h == GFX_H &&
              fgetc(f) != EOF && fread(g->px, 1, GFX_BYTES, f) == GFX_BYTES;
    fclose(f);
    return ok;
}

int pbm_compare(const gfx_t *a, const gfx_t *b, gfx_t *diff)
{
    int n = 0;
    for (int i = 0; i < GFX_BYTES; i++) {
        uint8_t x = (uint8_t)(a->px[i] ^ b->px[i]);
        if (diff) {
            diff->px[i] = x;
        }
        n += __builtin_popcount(x);
    }
    return n;
}

void pbm_print_ascii(const gfx_t *g)
{
    /* Upper/lower half blocks: each character cell shows two vertical pixels,
     * and we sample every other column to keep the width at 100 characters. */
    printf("+");
    for (int x = 0; x < GFX_W; x += 2)
        putchar('-');
    printf("+\n");
    for (int y = 0; y < GFX_H; y += 2) {
        printf("|");
        for (int x = 0; x < GFX_W; x += 2) {
            bool top = gfx_get(g, x, y) || gfx_get(g, x + 1, y);
            bool bot = gfx_get(g, x, y + 1) || gfx_get(g, x + 1, y + 1);
            fputs(top && bot ? "█" : top ? "▀" : bot ? "▄" : " ", stdout);
        }
        printf("|\n");
    }
    printf("+");
    for (int x = 0; x < GFX_W; x += 2)
        putchar('-');
    printf("+\n");
}
