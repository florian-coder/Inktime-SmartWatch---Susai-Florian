#include "display.h"

#include <string.h>

#include "core/log.h"
#include "drivers/epd_ssd1681.h"
#include "hal/hal.h"

typedef enum { DISP_OFF, DISP_IDLE, DISP_REFRESHING } disp_state_t;

static disp_state_t s_state;
static gfx_t s_shown;   /* last frame sent to the panel */
static gfx_t s_pending; /* frame waiting for the current refresh to finish */
static bool s_has_shown;
static bool s_has_pending;
static bool s_pending_full;
static uint8_t s_full_every = DISPLAY_FULL_EVERY;
static uint8_t s_since_full;
static uint64_t s_started;
static display_stats_t s_stats;

int display_init(bool hw_ok)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_has_shown = false;
    s_has_pending = false;
    s_since_full = 0;
    s_state = hw_ok ? DISP_IDLE : DISP_OFF;
    return hw_ok ? 0 : -1;
}

void display_set_full_every(uint8_t n)
{
    s_full_every = n ? n : 1;
}

static void start(const gfx_t *fb, bool full)
{
    if (!s_has_shown || s_since_full >= s_full_every) {
        full = true;
    }
    if (epd_update(fb->px, full) != 0) {
        s_stats.errors++;
        return;
    }
    s_shown = *fb;
    s_has_shown = true;
    s_state = DISP_REFRESHING;
    s_started = hal_millis();
    if (full) {
        s_since_full = 0;
        s_stats.full++;
    } else {
        s_since_full++;
        s_stats.partial++;
    }
    log_printf("display: %s refresh", full ? "full" : "partial");
}

void display_show(const gfx_t *fb, bool force_full)
{
    if (s_state == DISP_OFF) {
        return;
    }
    if (s_state == DISP_REFRESHING) {
        s_pending = *fb;
        s_has_pending = true;
        s_pending_full |= force_full;
        return;
    }
    if (!force_full && s_has_shown && memcmp(fb->px, s_shown.px, GFX_BYTES) == 0) {
        return;
    }
    start(fb, force_full);
}

uint64_t display_poll(uint64_t now)
{
    if (s_state != DISP_REFRESHING) {
        return 0;
    }
    if (epd_busy()) {
        /* `now` may predate s_started: the update itself takes a few ms of SPI and resets. */
        if (now > s_started && now - s_started > DISPLAY_TIMEOUT_MS) {
            log_printf("display: refresh timeout");
            s_stats.errors++;
            s_state = DISP_IDLE;
            return 0;
        }
        return now + DISPLAY_POLL_MS;
    }
    epd_sleep();
    s_state = DISP_IDLE;
    if (s_has_pending) {
        s_has_pending = false;
        bool full = s_pending_full;
        s_pending_full = false;
        if (full || memcmp(s_pending.px, s_shown.px, GFX_BYTES) != 0) {
            start(&s_pending, full);
            return now + DISPLAY_POLL_MS;
        }
    }
    return 0;
}

bool display_idle(void)
{
    return s_state != DISP_REFRESHING;
}

const display_stats_t *display_stats(void)
{
    return &s_stats;
}
