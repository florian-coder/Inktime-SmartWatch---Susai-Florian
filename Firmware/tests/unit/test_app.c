/* Application state machine, exercised without any hardware. */
#include "app/app.h"
#include "drivers/drv2605.h"
#include "services/buttons.h"
#include "tst.h"

static app_t a;
static uint64_t now;

static void setup(uint32_t epoch)
{
    now = 1000;
    app_init(&a, epoch, now);
}

static void press(btn_id_t b, press_t kind)
{
    event_t ev = { .type = EV_BUTTON, .a = (uint8_t)b, .b = (uint8_t)kind };
    app_handle(&a, &ev, now);
}

static void tick_to(uint64_t t)
{
    while (now < t) {
        now += 1000;
        event_t ev = { .type = EV_TICK, .u32 = app_epoch(&a, now) };
        app_handle(&a, &ev, now);
    }
}

static uint32_t at(int h, int m, int s)
{
    datetime_t dt = {
        .year = 2026, .month = 10, .day = 13, .hour = (uint8_t)h, .minute = (uint8_t)m, .second = (uint8_t)s
    };
    return dt_to_epoch(&dt);
}

TEST(app_starts_on_watchface_and_opens_menu)
{
    setup(APP_DEFAULT_EPOCH);
    CHECK_EQ(a.screen, SCR_WATCHFACE);
    press(BTN_ENTER, PRESS_SHORT);
    CHECK_EQ(a.screen, SCR_MENU);
    CHECK_EQ(a.menu_idx, 0);
    CHECK_EQ(a.haptic, HAPTIC_CLICK);
}

TEST(app_menu_navigation_wraps_both_ways)
{
    setup(APP_DEFAULT_EPOCH);
    press(BTN_ENTER, PRESS_SHORT);
    press(BTN_UP, PRESS_SHORT);
    CHECK_EQ(a.menu_idx, MENU_COUNT - 1);
    press(BTN_DOWN, PRESS_SHORT);
    CHECK_EQ(a.menu_idx, 0);
    press(BTN_DOWN, PRESS_SHORT);
    press(BTN_DOWN, PRESS_SHORT);
    CHECK_EQ(a.menu_idx, MENU_ALARM);
    press(BTN_ENTER, PRESS_LONG);
    CHECK_EQ(a.screen, SCR_WATCHFACE);
}

TEST(app_every_menu_item_opens_its_screen)
{
    static const screen_t expect[MENU_COUNT] = { SCR_STEPS,   SCR_STOPWATCH, SCR_ALARM, SCR_SETTIME,
                                                 SCR_BATTERY, SCR_SETTINGS,  SCR_ABOUT };
    for (int i = 0; i < MENU_COUNT; i++) {
        setup(APP_DEFAULT_EPOCH);
        press(BTN_ENTER, PRESS_SHORT);
        for (int k = 0; k < i; k++) {
            press(BTN_DOWN, PRESS_SHORT);
        }
        press(BTN_ENTER, PRESS_SHORT);
        CHECK_EQ(a.screen, expect[i]);
    }
}

TEST(app_reachable_with_up_and_enter_only)
{
    /* PCB rev v1 has no working DOWN button: UP alone must reach every menu entry. */
    setup(APP_DEFAULT_EPOCH);
    press(BTN_ENTER, PRESS_SHORT);
    bool seen[MENU_COUNT] = { false };
    for (int i = 0; i < MENU_COUNT; i++) {
        seen[a.menu_idx] = true;
        press(BTN_UP, PRESS_SHORT);
    }
    for (int i = 0; i < MENU_COUNT; i++) {
        CHECK(seen[i]);
    }
}

TEST(app_set_time_flow_updates_clock)
{
    setup(at(9, 41, 30));
    press(BTN_ENTER, PRESS_SHORT);
    a.menu_idx = MENU_SETTIME;
    press(BTN_ENTER, PRESS_SHORT);
    REQUIRE(a.screen == SCR_SETTIME);
    press(BTN_UP, PRESS_SHORT); /* hour 9 -> 10 */
    press(BTN_ENTER, PRESS_SHORT);
    press(BTN_DOWN, PRESS_SHORT); /* minute 41 -> 40 */
    press(BTN_ENTER, PRESS_SHORT);
    press(BTN_UP, PRESS_SHORT); /* day 13 -> 14 */
    press(BTN_ENTER, PRESS_SHORT);
    press(BTN_ENTER, PRESS_SHORT); /* month unchanged */
    press(BTN_ENTER, PRESS_SHORT); /* year unchanged -> apply */
    CHECK_EQ(a.screen, SCR_WATCHFACE);
    CHECK(a.force_full);
    datetime_t dt;
    dt_from_epoch(app_epoch(&a, now), &dt);
    CHECK_EQ(dt.hour, 10);
    CHECK_EQ(dt.minute, 40);
    CHECK_EQ(dt.second, 0);
    CHECK_EQ(dt.day, 14);
    CHECK_EQ(dt.month, 10);
    CHECK_EQ(dt.year, 2026);
}

TEST(app_set_time_values_wrap)
{
    setup(at(23, 59, 0));
    press(BTN_ENTER, PRESS_SHORT);
    a.menu_idx = MENU_SETTIME;
    press(BTN_ENTER, PRESS_SHORT);
    press(BTN_UP, PRESS_SHORT);
    CHECK_EQ(a.edit.hour, 0);
    press(BTN_ENTER, PRESS_SHORT);
    press(BTN_UP, PRESS_SHORT);
    CHECK_EQ(a.edit.minute, 0);
    press(BTN_ENTER, PRESS_LONG); /* cancel keeps the old time */
    datetime_t dt;
    dt_from_epoch(app_epoch(&a, now), &dt);
    CHECK_EQ(dt.hour, 23);
}

TEST(app_alarm_rings_buzzes_and_dismisses)
{
    setup(at(6, 59, 50));
    press(BTN_ENTER, PRESS_SHORT);
    a.menu_idx = MENU_ALARM;
    press(BTN_ENTER, PRESS_SHORT);
    press(BTN_UP, PRESS_SHORT);    /* off -> on */
    press(BTN_ENTER, PRESS_SHORT); /* hour 7 */
    press(BTN_ENTER, PRESS_SHORT); /* minute 0 */
    press(BTN_ENTER, PRESS_SHORT); /* save */
    CHECK(a.alarm_on);
    CHECK_EQ(a.screen, SCR_MENU);
    press(BTN_ENTER, PRESS_LONG);
    a.haptic = 0;

    tick_to(now + 9000);
    CHECK_EQ(a.screen, SCR_WATCHFACE);
    tick_to(now + 1000); /* 07:00:00 */
    CHECK_EQ(a.screen, SCR_RINGING);
    CHECK_EQ(a.haptic, HAPTIC_BUZZ);
    a.haptic = 0;
    tick_to(now + 2000);
    CHECK_EQ(a.haptic, HAPTIC_BUZZ);
    press(BTN_UP, PRESS_SHORT);
    CHECK_EQ(a.screen, SCR_WATCHFACE);
}

TEST(app_alarm_stops_by_itself)
{
    setup(at(7, 0, 0) - 1);
    a.alarm_on = true;
    tick_to(now + 1000);
    REQUIRE(a.screen == SCR_RINGING);
    tick_to(now + (APP_RING_SECONDS + 1) * 1000);
    CHECK_EQ(a.screen, SCR_WATCHFACE);
}

TEST(app_stopwatch_measures_and_resets)
{
    setup(APP_DEFAULT_EPOCH);
    press(BTN_ENTER, PRESS_SHORT);
    a.menu_idx = MENU_STOPWATCH;
    press(BTN_ENTER, PRESS_SHORT);
    press(BTN_ENTER, PRESS_SHORT); /* start */
    CHECK(a.sw_running);
    now += 5500;
    press(BTN_UP, PRESS_SHORT);    /* reset ignored while running */
    press(BTN_ENTER, PRESS_SHORT); /* stop */
    CHECK(!a.sw_running);
    CHECK_EQ(a.sw_accum_ms, 5500);
    press(BTN_ENTER, PRESS_SHORT);
    now += 500;
    press(BTN_ENTER, PRESS_SHORT);
    CHECK_EQ(a.sw_accum_ms, 6000);
    press(BTN_UP, PRESS_SHORT);
    CHECK_EQ(a.sw_accum_ms, 0);
}

TEST(app_steps_reset_at_midnight)
{
    setup(at(23, 59, 58));
    event_t ev = { .type = EV_STEPS, .u32 = 500 };
    app_handle(&a, &ev, now);
    CHECK_EQ(app_steps_today(&a), 500);
    tick_to(now + 3000);
    CHECK_EQ(app_steps_today(&a), 0);
    ev.u32 = 520;
    app_handle(&a, &ev, now);
    CHECK_EQ(app_steps_today(&a), 20);
}

TEST(app_watchface_steps_update_once_per_minute)
{
    setup(at(12, 0, 30));
    event_t ev = { .type = EV_STEPS, .u32 = 42 };
    app_handle(&a, &ev, now);
    tick_to(now + 1000);
    CHECK_EQ(a.wf_steps, 0);
    tick_to(now + 30000);
    CHECK_EQ(a.wf_steps, 42);
}

TEST(app_notification_overlay_returns_to_previous_screen)
{
    setup(APP_DEFAULT_EPOCH);
    press(BTN_ENTER, PRESS_SHORT);
    app_set_notification(&a, "Meeting in 5 minutes");
    event_t ev = { .type = EV_NOTIFY };
    app_handle(&a, &ev, now);
    CHECK_EQ(a.screen, SCR_NOTIFY);
    CHECK_EQ(a.haptic, HAPTIC_DOUBLE_CLICK);
    press(BTN_DOWN, PRESS_SHORT);
    CHECK_EQ(a.screen, SCR_MENU);
}

TEST(app_low_battery_overlay_and_shutdown)
{
    setup(APP_DEFAULT_EPOCH);
    a.power = (app_power_t){ .pct = 4, .mv = 3550 };
    event_t low = { .type = EV_BATT_LOW };
    app_handle(&a, &low, now);
    CHECK_EQ(a.screen, SCR_LOWBATT);
    a.power.vin = true;
    event_t pw = { .type = EV_POWER };
    app_handle(&a, &pw, now);
    CHECK_EQ(a.screen, SCR_WATCHFACE); /* plugging in dismisses the warning */
    a.power = (app_power_t){ .pct = 2, .mv = 3400 };
    app_handle(&a, &pw, now);
    CHECK_EQ(a.screen, SCR_OFF);
    CHECK(a.poweroff);
}

TEST(app_settings_vibration_and_refresh_interval)
{
    setup(APP_DEFAULT_EPOCH);
    press(BTN_ENTER, PRESS_SHORT);
    a.menu_idx = MENU_SETTINGS;
    press(BTN_ENTER, PRESS_SHORT);
    press(BTN_ENTER, PRESS_SHORT); /* vibration off */
    CHECK(!a.vibrate);
    a.haptic = 0;
    press(BTN_DOWN, PRESS_SHORT);
    CHECK_EQ(a.haptic, 0);
    CHECK_EQ(a.full_every, 30);
    press(BTN_ENTER, PRESS_SHORT);
    CHECK_EQ(a.full_every, 60);
    press(BTN_ENTER, PRESS_SHORT);
    CHECK_EQ(a.full_every, 10);
    press(BTN_DOWN, PRESS_SHORT);
    press(BTN_ENTER, PRESS_SHORT); /* power off */
    CHECK_EQ(a.screen, SCR_OFF);
    CHECK(a.poweroff);
}

TEST(app_every_screen_renders_something)
{
    static gfx_t g;
    setup(at(8, 30, 0));
    a.alarm_on = true;
    app_set_notification(&a, "A rather long notification that needs wrapping over lines");
    for (int s = 0; s < SCR_COUNT; s++) {
        a.screen = (screen_t)s;
        app_render(&a, &g, now);
        CHECK(gfx_count_black(&g) > 50);
    }
}

TEST(app_render_is_deterministic)
{
    static gfx_t g1, g2;
    setup(at(8, 30, 0));
    app_render(&a, &g1, now);
    app_render(&a, &g2, now + 999);
    CHECK(memcmp(g1.px, g2.px, GFX_BYTES) == 0);
    app_render(&a, &g2, now + 60000);
    CHECK(memcmp(g1.px, g2.px, GFX_BYTES) != 0);
}
