/*
 * Watch application: a pure state machine. It never touches hardware; it consumes
 * events, exposes requests (haptic effect, power off, full refresh) and renders
 * into a framebuffer. That makes every screen unit-testable on the host.
 */
#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

#include "core/datetime.h"
#include "core/evq.h"
#include "gfx/gfx.h"

#define FW_VERSION "1.0.0"

/* 2026-01-01 00:00:00 UTC, used until the user sets the clock. */
#define APP_DEFAULT_EPOCH 1767225600u

#define APP_STEP_GOAL       8000u
#define APP_STEP_LENGTH_MM  762u
#define APP_RING_SECONDS    60u
#define APP_LOWBATT_OFF_PCT 2u

#define HW_EPD (1u << 0)
#define HW_IMU (1u << 1)
#define HW_FG  (1u << 2)
#define HW_CHG (1u << 3)
#define HW_HAP (1u << 4)
#define HW_ALL (HW_EPD | HW_IMU | HW_FG | HW_CHG | HW_HAP)

typedef enum {
    SCR_WATCHFACE,
    SCR_MENU,
    SCR_STEPS,
    SCR_STOPWATCH,
    SCR_ALARM,
    SCR_SETTIME,
    SCR_BATTERY,
    SCR_SETTINGS,
    SCR_ABOUT,
    SCR_NOTIFY,
    SCR_RINGING,
    SCR_LOWBATT,
    SCR_OFF,
    SCR_COUNT
} screen_t;

/* Menu entries, in display order. */
typedef enum {
    MENU_STEPS,
    MENU_STOPWATCH,
    MENU_ALARM,
    MENU_SETTIME,
    MENU_BATTERY,
    MENU_SETTINGS,
    MENU_ABOUT,
    MENU_COUNT
} menu_item_t;

typedef enum { SET_VIBRATION, SET_FULL_REFRESH, SET_POWER_OFF, SET_COUNT } settings_item_t;

typedef struct {
    uint8_t pct;
    uint16_t mv;
    bool vin;      /* USB power present */
    bool charging; /* CC or CV phase */
    bool full;     /* charge done */
} app_power_t;

typedef struct {
    screen_t screen;
    screen_t return_to; /* where overlays (notify / ringing / low battery) go back to */
    uint8_t menu_idx;
    uint8_t settings_idx;
    uint8_t edit_field;
    datetime_t edit; /* scratch copy for the set-time screen */
    uint8_t edit_alarm_h, edit_alarm_m;
    bool edit_alarm_on;

    /* clock: epoch = epoch_base + (now - ms_base) / 1000 */
    uint32_t epoch_base;
    uint64_t ms_base;
    uint32_t last_epoch;

    /* alarm */
    bool alarm_on;
    uint8_t alarm_h, alarm_m;
    uint32_t ring_until; /* epoch */
    uint32_t last_buzz;  /* epoch */

    /* stopwatch */
    bool sw_running;
    uint64_t sw_start_ms;
    uint64_t sw_accum_ms;

    /* settings */
    bool vibrate;
    uint8_t full_every;

    /* inputs */
    app_power_t power;
    uint32_t raw_steps;
    uint32_t steps_base; /* raw counter value at the start of the day */
    uint32_t wf_steps;   /* step count drawn on the watch face (refreshed each minute) */
    uint32_t hw_ok;
    char notify[72];

    /* requests to the system layer, cleared by it */
    uint8_t haptic;
    bool poweroff;
    bool force_full;
} app_t;

void app_init(app_t *a, uint32_t epoch, uint64_t now);
uint32_t app_epoch(const app_t *a, uint64_t now);
void app_set_epoch(app_t *a, uint32_t epoch, uint64_t now);
/* Time of the next second boundary of the wall clock. */
uint64_t app_next_tick_ms(const app_t *a, uint64_t now);
uint32_t app_steps_today(const app_t *a);
void app_set_notification(app_t *a, const char *text);

void app_handle(app_t *a, const event_t *ev, uint64_t now);
void app_render(const app_t *a, gfx_t *g, uint64_t now);

const char *app_screen_name(screen_t s);

#endif
