/* Owns the e-paper refresh state machine: partial vs. full refreshes, BUSY polling, sleep. */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/gfx.h"

#define DISPLAY_POLL_MS    25
#define DISPLAY_TIMEOUT_MS 5000
#define DISPLAY_FULL_EVERY 30 /* partial refreshes between two full ones */

typedef struct {
    uint32_t full;
    uint32_t partial;
    uint32_t errors;
} display_stats_t;

int display_init(bool hw_ok);
/* Show fb if it differs from what is on screen. Queued if a refresh is running. */
void display_show(const gfx_t *fb, bool force_full);
uint64_t display_poll(uint64_t now);
bool display_idle(void);
void display_set_full_every(uint8_t n);
const display_stats_t *display_stats(void);

#endif
