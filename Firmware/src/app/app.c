#include "app.h"

#include <string.h>

#include "core/log.h"
#include "drivers/drv2605.h"
#include "services/buttons.h"

static const uint8_t full_every_options[] = { 10, 30, 60 };

void app_init(app_t *a, uint32_t epoch, uint64_t now)
{
    memset(a, 0, sizeof(*a));
    a->screen = SCR_WATCHFACE;
    a->return_to = SCR_WATCHFACE;
    a->epoch_base = epoch;
    a->ms_base = now;
    a->last_epoch = epoch;
    a->alarm_h = 7;
    a->alarm_m = 0;
    a->vibrate = true;
    a->full_every = 30;
    a->power.pct = 100;
    a->force_full = true;
}

uint32_t app_epoch(const app_t *a, uint64_t now)
{
    return a->epoch_base + (uint32_t)((now - a->ms_base) / 1000);
}

void app_set_epoch(app_t *a, uint32_t epoch, uint64_t now)
{
    a->epoch_base = epoch;
    a->ms_base = now;
    a->last_epoch = epoch;
}

uint64_t app_next_tick_ms(const app_t *a, uint64_t now)
{
    return a->ms_base + ((now - a->ms_base) / 1000 + 1) * 1000;
}

uint32_t app_steps_today(const app_t *a)
{
    return a->raw_steps >= a->steps_base ? a->raw_steps - a->steps_base : a->raw_steps;
}

void app_set_notification(app_t *a, const char *text)
{
    strncpy(a->notify, text, sizeof(a->notify) - 1);
    a->notify[sizeof(a->notify) - 1] = '\0';
}

const char *app_screen_name(screen_t s)
{
    static const char *const names[SCR_COUNT] = {
        "watchface", "menu",  "steps",  "stopwatch", "alarm",   "settime", "battery",
        "settings",  "about", "notify", "ringing",   "lowbatt", "off",
    };
    return s < SCR_COUNT ? names[s] : "?";
}

static bool is_overlay(screen_t s)
{
    return s == SCR_NOTIFY || s == SCR_RINGING || s == SCR_LOWBATT;
}

static void go(app_t *a, screen_t s)
{
    if (a->screen != s) {
        a->screen = s;
        log_printf("screen: %s", app_screen_name(s));
    }
}

static void overlay(app_t *a, screen_t s)
{
    if (!is_overlay(a->screen) && a->screen != SCR_OFF) {
        a->return_to = a->screen;
    }
    go(a, s);
}

static void request_haptic(app_t *a, uint8_t effect)
{
    if (a->vibrate) {
        a->haptic = effect;
    }
}

static int wrap(int v, int lo, int hi)
{
    if (v < lo) return hi;
    if (v > hi) return lo;
    return v;
}

static void open_menu_item(app_t *a, uint64_t now)
{
    switch ((menu_item_t)a->menu_idx) {
    case MENU_STEPS: go(a, SCR_STEPS); break;
    case MENU_STOPWATCH: go(a, SCR_STOPWATCH); break;
    case MENU_ALARM:
        a->edit_field = 0;
        a->edit_alarm_on = a->alarm_on;
        a->edit_alarm_h = a->alarm_h;
        a->edit_alarm_m = a->alarm_m;
        go(a, SCR_ALARM);
        break;
    case MENU_SETTIME:
        a->edit_field = 0;
        dt_from_epoch(app_epoch(a, now), &a->edit);
        go(a, SCR_SETTIME);
        break;
    case MENU_BATTERY: go(a, SCR_BATTERY); break;
    case MENU_SETTINGS:
        a->settings_idx = 0;
        go(a, SCR_SETTINGS);
        break;
    case MENU_ABOUT: go(a, SCR_ABOUT); break;
    default: break;
    }
}

static void edit_settime(app_t *a, int delta)
{
    datetime_t *e = &a->edit;
    switch (a->edit_field) {
    case 0: e->hour = (uint8_t)wrap(e->hour + delta, 0, 23); break;
    case 1: e->minute = (uint8_t)wrap(e->minute + delta, 0, 59); break;
    case 2: e->day = (uint8_t)wrap(e->day + delta, 1, dt_days_in_month(e->year, e->month)); break;
    case 3: e->month = (uint8_t)wrap(e->month + delta, 1, 12); break;
    case 4: e->year = (uint16_t)wrap(e->year + delta, 2024, 2099); break;
    default: break;
    }
    dt_normalize(e);
}

static void edit_alarm(app_t *a, int delta)
{
    switch (a->edit_field) {
    case 0: a->edit_alarm_on = !a->edit_alarm_on; break;
    case 1: a->edit_alarm_h = (uint8_t)wrap(a->edit_alarm_h + delta, 0, 23); break;
    case 2: a->edit_alarm_m = (uint8_t)wrap(a->edit_alarm_m + delta, 0, 59); break;
    default: break;
    }
}

static void activate_setting(app_t *a)
{
    switch ((settings_item_t)a->settings_idx) {
    case SET_VIBRATION: a->vibrate = !a->vibrate; break;
    case SET_FULL_REFRESH: {
        size_t i = 0;
        while (i < sizeof(full_every_options) && full_every_options[i] != a->full_every) {
            i++;
        }
        a->full_every = full_every_options[(i + 1) % sizeof(full_every_options)];
        break;
    }
    case SET_POWER_OFF:
        go(a, SCR_OFF);
        a->poweroff = true;
        a->force_full = true;
        break;
    default: break;
    }
}

static void on_button(app_t *a, btn_id_t id, press_t kind, uint64_t now)
{
    const bool shortp = kind == PRESS_SHORT;
    const bool longp = kind == PRESS_LONG;
    const int step = id == BTN_UP ? 1 : (id == BTN_DOWN ? -1 : 0);

    if (kind != PRESS_REPEAT) {
        request_haptic(a, HAPTIC_CLICK);
    }

    /* Overlays: any button dismisses. */
    if (is_overlay(a->screen)) {
        if (shortp || longp) {
            go(a, a->return_to);
        }
        return;
    }

    /* Long ENTER always goes home (cancelling edits). */
    if (id == BTN_ENTER && longp) {
        go(a, SCR_WATCHFACE);
        return;
    }

    switch (a->screen) {
    case SCR_WATCHFACE:
        if (id == BTN_ENTER && shortp) {
            a->menu_idx = 0;
            go(a, SCR_MENU);
        } else if (id == BTN_UP && shortp) {
            go(a, SCR_STEPS);
        } else if (id == BTN_DOWN && shortp) {
            go(a, SCR_BATTERY);
        }
        break;

    case SCR_MENU:
        if (id == BTN_ENTER && shortp) {
            open_menu_item(a, now);
        } else if (step != 0) {
            /* UP moves the cursor up the list. */
            a->menu_idx = (uint8_t)wrap(a->menu_idx - step, 0, MENU_COUNT - 1);
        }
        break;

    case SCR_STEPS:
    case SCR_BATTERY:
    case SCR_ABOUT:
        if (id == BTN_ENTER && shortp) {
            go(a, SCR_MENU);
        }
        break;

    case SCR_STOPWATCH:
        if (id == BTN_ENTER && shortp) {
            if (a->sw_running) {
                a->sw_accum_ms += now - a->sw_start_ms;
                a->sw_running = false;
            } else {
                a->sw_start_ms = now;
                a->sw_running = true;
            }
        } else if ((id == BTN_UP || id == BTN_DOWN) && shortp && !a->sw_running) {
            a->sw_accum_ms = 0;
        }
        break;

    case SCR_ALARM:
        if (id == BTN_ENTER && shortp) {
            if (++a->edit_field > 2) {
                a->alarm_on = a->edit_alarm_on;
                a->alarm_h = a->edit_alarm_h;
                a->alarm_m = a->edit_alarm_m;
                log_printf("alarm: %s %02u:%02u", a->alarm_on ? "on" : "off", a->alarm_h, a->alarm_m);
                go(a, SCR_MENU);
            }
        } else if (step != 0) {
            edit_alarm(a, step);
        }
        break;

    case SCR_SETTIME:
        if (id == BTN_ENTER && shortp) {
            if (++a->edit_field > 4) {
                a->edit.second = 0;
                dt_normalize(&a->edit);
                app_set_epoch(a, dt_to_epoch(&a->edit), now);
                log_printf("clock: set to %04u-%02u-%02u %02u:%02u", a->edit.year, a->edit.month, a->edit.day,
                           a->edit.hour, a->edit.minute);
                a->force_full = true;
                go(a, SCR_WATCHFACE);
            }
        } else if (step != 0) {
            edit_settime(a, step);
        }
        break;

    case SCR_SETTINGS:
        if (id == BTN_ENTER && shortp) {
            activate_setting(a);
        } else if (step != 0) {
            a->settings_idx = (uint8_t)wrap(a->settings_idx - step, 0, SET_COUNT - 1);
        }
        break;

    default: break;
    }
}

static void on_tick(app_t *a, uint32_t epoch)
{
    const uint32_t prev = a->last_epoch;
    a->last_epoch = epoch;

    if (epoch / 86400 != prev / 86400) {
        /* Midnight: today's step count starts from zero. */
        a->steps_base = a->raw_steps;
    }
    if (epoch / 60 != prev / 60) {
        a->wf_steps = app_steps_today(a);
        datetime_t dt;
        dt_from_epoch(epoch, &dt);
        if (a->alarm_on && dt.hour == a->alarm_h && dt.minute == a->alarm_m && a->screen != SCR_OFF) {
            a->ring_until = epoch + APP_RING_SECONDS;
            a->last_buzz = 0;
            log_printf("alarm: ringing");
            overlay(a, SCR_RINGING);
        }
    }
    if (a->screen == SCR_RINGING) {
        if (epoch >= a->ring_until) {
            go(a, a->return_to);
        } else if (epoch >= a->last_buzz + 2) {
            a->last_buzz = epoch;
            a->haptic = HAPTIC_BUZZ; /* alarms vibrate even with clicks disabled */
        }
    }
}

static void on_power(app_t *a)
{
    if (!a->power.vin && a->power.pct <= APP_LOWBATT_OFF_PCT && a->screen != SCR_OFF) {
        log_printf("power: battery empty, shutting down");
        go(a, SCR_OFF);
        a->poweroff = true;
        a->force_full = true;
    } else if (a->power.vin && a->screen == SCR_LOWBATT) {
        go(a, a->return_to);
    }
}

void app_handle(app_t *a, const event_t *ev, uint64_t now)
{
    if (a->screen == SCR_OFF) {
        return;
    }
    switch ((ev_type_t)ev->type) {
    case EV_TICK:
        /* Use the clock, not the epoch captured when the tick was queued: a time
         * sync in between would otherwise look like a jump across midnight. */
        on_tick(a, app_epoch(a, now));
        break;
    case EV_BUTTON: on_button(a, (btn_id_t)ev->a, (press_t)ev->b, now); break;
    case EV_STEPS:
        a->raw_steps = ev->u32;
        if (a->steps_base > a->raw_steps) {
            a->steps_base = 0; /* sensor was reset */
        }
        break;
    case EV_POWER: on_power(a); break;
    case EV_BATT_LOW:
        if (!a->power.vin) {
            overlay(a, SCR_LOWBATT);
            request_haptic(a, HAPTIC_DOUBLE_CLICK);
        }
        break;
    case EV_NOTIFY:
        overlay(a, SCR_NOTIFY);
        a->haptic = HAPTIC_DOUBLE_CLICK;
        break;
    default: break;
    }
}
