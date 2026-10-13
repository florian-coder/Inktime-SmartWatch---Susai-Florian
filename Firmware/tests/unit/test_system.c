/* Whole-firmware tests: watch.c + drivers + app on the virtual board. */
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "drivers/drv2605.h"
#include "hal/hal.h"
#include "watch.h"
#include "../../sim/models/models.h"
#include "../../sim/sim.h"
#include "tst.h"

static void boot(void)
{
    sim_init();
    REQUIRE(sim_settle(5000));
}

static void no_model_errors(int line)
{
    if (sim_model_errors()) {
        tst_fail(__FILE__, line, "peripheral model error: %s", sim_model_error_text());
    }
}

static bool panel_shows_framebuffer(void)
{
    static gfx_t panel;
    sim_panel(&panel);
    return memcmp(panel.px, watch_framebuffer()->px, GFX_BYTES) == 0;
}

static uint32_t at(int h, int m, int s)
{
    datetime_t dt = {
        .year = 2026, .month = 10, .day = 13, .hour = (uint8_t)h, .minute = (uint8_t)m, .second = (uint8_t)s
    };
    return dt_to_epoch(&dt);
}

TEST(sys_boot_detects_all_hardware_and_draws_watchface)
{
    boot();
    CHECK(sim_log_find("hw: epd ok imu ok fg ok chg ok hap ok", 0) >= 0);
    CHECK_EQ(watch_hw_status(), HW_ALL);
    CHECK_EQ(watch_app()->screen, SCR_WATCHFACE);
    CHECK_EQ(epdm_stats()->full, 1);
    CHECK_EQ(epdm_stats()->partial, 0);
    CHECK(epdm_sleeping());
    CHECK(panel_shows_framebuffer());
    CHECK_EQ(watch_app()->power.pct, 87);
    no_model_errors(__LINE__);
}

TEST(sys_one_partial_refresh_per_minute)
{
    boot();
    sim_set_epoch(at(10, 0, 5));
    REQUIRE(sim_settle(5000));
    const uint32_t p0 = epdm_stats()->partial;
    REQUIRE(sim_run_for(50 * 1000)); /* 10:00:55, still same minute */
    CHECK_EQ(epdm_stats()->partial, p0);
    REQUIRE(sim_run_for(10 * 1000)); /* crosses 10:01:00 */
    REQUIRE(sim_settle(3000));
    CHECK_EQ(epdm_stats()->partial, p0 + 1);
    CHECK(panel_shows_framebuffer());
    no_model_errors(__LINE__);
}

TEST(sys_full_refresh_every_30_partials)
{
    boot();
    const uint32_t f0 = epdm_stats()->full;
    REQUIRE(sim_run_for(31u * 60u * 1000u));
    REQUIRE(sim_settle(3000));
    CHECK_EQ(epdm_stats()->full, f0 + 1);
    CHECK(epdm_stats()->partial >= 29);
    CHECK(panel_shows_framebuffer());
    no_model_errors(__LINE__);
}

TEST(sys_button_opens_menu_with_haptic_click)
{
    boot();
    const uint32_t plays = hapm_play_count();
    sim_click(BTN_ENTER, 120);
    REQUIRE(sim_settle(3000));
    CHECK_EQ(watch_app()->screen, SCR_MENU);
    CHECK_EQ(hapm_play_count(), plays + 1);
    CHECK_EQ(hapm_last_effect(), HAPTIC_CLICK);
    CHECK(hapm_in_standby());
    CHECK(panel_shows_framebuffer());
    CHECK(sim_log_find("button: enter short", 0) >= 0);
    no_model_errors(__LINE__);
}

TEST(sys_long_press_goes_home)
{
    boot();
    sim_click(BTN_ENTER, 100);
    sim_click(BTN_DOWN, 100);
    sim_click(BTN_ENTER, 100);
    REQUIRE(sim_settle(3000));
    CHECK_EQ(watch_app()->screen, SCR_STOPWATCH);
    sim_click(BTN_ENTER, 1000);
    REQUIRE(sim_settle(3000));
    CHECK_EQ(watch_app()->screen, SCR_WATCHFACE);
    no_model_errors(__LINE__);
}

TEST(sys_step_interrupt_updates_count)
{
    boot();
    sim_add_steps(45);
    REQUIRE(sim_run_for(100));
    CHECK_EQ(app_steps_today(watch_app()), 45);
    CHECK(sim_log_find("steps: 45", 0) >= 0);
    sim_add_steps(10); /* below the next watermark: no interrupt yet */
    REQUIRE(sim_run_for(100));
    CHECK_EQ(app_steps_today(watch_app()), 45);
    REQUIRE(sim_run_for(61000)); /* periodic poll catches up */
    CHECK_EQ(app_steps_today(watch_app()), 55);
    no_model_errors(__LINE__);
}

TEST(sys_charger_interrupt_shows_charging)
{
    boot();
    sim_set_charger(true, CHG_CONSTANT_CURRENT);
    REQUIRE(sim_run_for(100));
    CHECK(watch_app()->power.vin);
    CHECK(watch_app()->power.charging);
    REQUIRE(sim_settle(3000));
    CHECK(panel_shows_framebuffer());
    sim_set_charger(true, CHG_DONE);
    REQUIRE(sim_run_for(100));
    CHECK(watch_app()->power.full);
    CHECK(!watch_app()->power.charging);
    sim_set_charger(false, CHG_NOT_CHARGING);
    REQUIRE(sim_run_for(100));
    CHECK(!watch_app()->power.vin);
    no_model_errors(__LINE__);
}

TEST(sys_fuel_gauge_alert_shows_low_battery)
{
    boot();
    sim_set_battery(4, 3550);
    REQUIRE(sim_run_for(100));
    CHECK_EQ(watch_app()->screen, SCR_LOWBATT);
    CHECK(!fgm_alert_active()); /* firmware cleared the alert */
    REQUIRE(sim_settle(3000));
    sim_click(BTN_UP, 100);
    CHECK_EQ(watch_app()->screen, SCR_WATCHFACE);
    no_model_errors(__LINE__);
}

TEST(sys_notification_overlay)
{
    boot();
    sim_notify("Lunch at 13:00");
    REQUIRE(sim_settle(3000));
    CHECK_EQ(watch_app()->screen, SCR_NOTIFY);
    CHECK_EQ(hapm_last_effect(), HAPTIC_DOUBLE_CLICK);
    CHECK(panel_shows_framebuffer());
    sim_click(BTN_ENTER, 100);
    REQUIRE(sim_settle(3000));
    CHECK_EQ(watch_app()->screen, SCR_WATCHFACE);
    no_model_errors(__LINE__);
}

TEST(sys_alarm_end_to_end)
{
    boot();
    sim_set_epoch(at(6, 59, 0));
    REQUIRE(sim_settle(3000));
    sim_click(BTN_ENTER, 100); /* menu */
    sim_click(BTN_DOWN, 100);
    sim_click(BTN_DOWN, 100); /* ALARM */
    sim_click(BTN_ENTER, 100);
    sim_click(BTN_UP, 100);    /* ON */
    sim_click(BTN_ENTER, 100); /* hour 07 */
    sim_click(BTN_ENTER, 100); /* minute 00 */
    sim_click(BTN_ENTER, 100); /* save */
    CHECK(watch_app()->alarm_on);
    sim_click(BTN_ENTER, 900); /* home */
    REQUIRE(sim_run_for(60000));
    CHECK_EQ(watch_app()->screen, SCR_RINGING);
    CHECK_EQ(hapm_last_effect(), HAPTIC_BUZZ);
    REQUIRE(sim_settle(3000));
    CHECK(panel_shows_framebuffer());
    sim_click(BTN_ENTER, 100);
    CHECK_EQ(watch_app()->screen, SCR_WATCHFACE);
    no_model_errors(__LINE__);
}

TEST(sys_power_off_and_wake_with_enter)
{
    boot();
    sim_click(BTN_ENTER, 100); /* menu */
    sim_click(BTN_UP, 100);    /* ABOUT */
    sim_click(BTN_UP, 100);    /* SETTINGS */
    sim_click(BTN_ENTER, 100);
    sim_click(BTN_UP, 100); /* POWER OFF (wraps) */
    sim_click(BTN_ENTER, 100);
    REQUIRE(sim_settle(5000));
    CHECK(sim_is_off());
    CHECK(sim_log_find("power: system off", 0) >= 0);
    /* The e-paper keeps the goodbye screen without power. */
    static gfx_t panel;
    sim_panel(&panel);
    CHECK(gfx_count_black(&panel) > 100);
    REQUIRE(sim_run_for(10000));
    CHECK(sim_is_off());

    sim_button(BTN_ENTER, true);
    CHECK(!sim_is_off());
    sim_button(BTN_ENTER, false);
    REQUIRE(sim_settle(5000));
    char expect[48];
    snprintf(expect, sizeof(expect), "(reset 0x%lx)", (unsigned long)HAL_RESET_WAKE_OFF);
    CHECK(sim_log_find(expect, 0) >= 0);
    CHECK_EQ(watch_app()->screen, SCR_WATCHFACE);
    CHECK(panel_shows_framebuffer());
    no_model_errors(__LINE__);
}

TEST(sys_midnight_rollover_resets_steps_and_date)
{
    boot();
    sim_set_epoch(at(23, 59, 0));
    sim_add_steps(400);
    REQUIRE(sim_run_for(2000));
    CHECK_EQ(app_steps_today(watch_app()), 400);
    REQUIRE(sim_run_for(60000));
    CHECK_EQ(app_steps_today(watch_app()), 0);
    datetime_t dt;
    dt_from_epoch(app_epoch(watch_app(), sim_now()), &dt);
    CHECK_EQ(dt.day, 14);
    no_model_errors(__LINE__);
}

TEST(sys_runs_six_hours_without_errors)
{
    boot();
    for (int h = 0; h < 6; h++) {
        sim_add_steps(900);
        REQUIRE(sim_run_for(3600u * 1000u));
    }
    REQUIRE(sim_settle(3000));
    const epdm_stats_t *st = epdm_stats();
    CHECK(st->partial >= 340 && st->partial <= 360);
    CHECK(st->full >= 12 && st->full <= 14);
    CHECK_EQ(app_steps_today(watch_app()), 5400);
    CHECK(panel_shows_framebuffer());
    no_model_errors(__LINE__);
}
