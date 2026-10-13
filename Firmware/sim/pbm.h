/* Binary PBM (P4) images: 1 = black, rows padded to bytes, MSB first - same as gfx_t. */
#ifndef PBM_H
#define PBM_H

#include <stdbool.h>

#include "gfx/gfx.h"

bool pbm_write(const char *path, const gfx_t *g);
bool pbm_read(const char *path, gfx_t *g);
/* Number of differing pixels; writes a diff image (black = mismatch) if diff != NULL. */
int pbm_compare(const gfx_t *a, const gfx_t *b, gfx_t *diff);
/* Print the image as text, two pixels per character row (for terminals). */
void pbm_print_ascii(const gfx_t *g);

#endif
