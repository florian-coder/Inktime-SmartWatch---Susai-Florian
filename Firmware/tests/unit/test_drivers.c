/* Driver behaviour verified against the peripheral models (after a normal boot). */
#include <string.h>

#include "drivers/bma423.h"
#include "drivers/bq25180.h"
#include "drivers/drv2605.h"
#include "drivers/epd_ssd1681.h"
#include "drivers/max17048.h"
#include "../../sim/models/models.h"
#include "../../sim/sim.h"
#include "tst.h"

static void boot(void)
{
    sim_init();
    REQUIRE(sim_settle(5000));
    if (sim_model_errors()) {
        tst_fail(__FILE__, __LINE__, "model error after boot: %s", sim_model_error_text());
    }
}

TEST(drv_bma423_uploads_feature_engine_and_enables_step_counter)
{
    boot();
    CHECK(bmam_feature_engine_ok());
    CHECK(bmam_step_counter_enabled());
    sim_add_steps(123);
    uint32_t steps = 0;
    CHECK_EQ(bma423_read_steps(&steps), 0);
    CHECK_EQ(steps, 123);
    int16_t acc[3];
    CHECK_EQ(bma423_read_accel(acc), 0);
    CHECK_EQ(acc[0], 0);
    CHECK_EQ(acc[2], 1000); /* +1 g */
    CHECK_EQ(bmam_errors(), 0);
}

TEST(drv_max17048_converts_soc_and_voltage)
{
    boot();
    sim_set_battery(50, 3700);
    max17048_reading_t r;
    CHECK_EQ(max17048_read(&r), 0);
    CHECK_EQ(r.pct, 50);
    CHECK(r.mv >= 3699 && r.mv <= 3700);
    CHECK_EQ(fgm_alert_threshold(), 5);
    CHECK(!fgm_alert_active());
    sim_set_battery(4, 3550);
    CHECK(fgm_alert_active());
    uint16_t status = 0;
    CHECK_EQ(max17048_clear_alert(&status), 0);
    CHECK(status & 0x1000); /* HD flag was set */
    CHECK(!fgm_alert_active());
}

TEST(drv_bq25180_code_conversions)
{
    CHECK_EQ(bq25180_ichg_code(5), 0);
    CHECK_EQ(bq25180_ichg_code(35), 30);
    CHECK_EQ(bq25180_ichg_code(40), 31);
    CHECK_EQ(bq25180_ichg_code(120), 39);
    CHECK_EQ(bq25180_ichg_code(1000), 127);
    CHECK_EQ(bq25180_ichg_code(1), 0);
    CHECK_EQ(bq25180_vbat_code(4200), 70);
    CHECK_EQ(bq25180_vbat_code(3000), 0);
}

TEST(drv_bq25180_configures_charge_and_disables_watchdog)
{
    boot();
    CHECK_EQ(chgm_ichg_ma(), 120);
    CHECK_EQ(chgm_vreg_mv(), 4200);
    sim_set_charger(true, CHG_CONSTANT_CURRENT);
    REQUIRE(sim_run_for(200000)); /* longer than the 160 s watchdog */
    CHECK_EQ(chgm_watchdog_resets(), 0);
    CHECK_EQ(chgm_ichg_ma(), 120);
    bq25180_status_t st;
    CHECK_EQ(bq25180_read_status(&st), 0);
    CHECK(st.vin_good);
    CHECK_EQ(st.state, CHG_CONSTANT_CURRENT);
}

TEST(drv_drv2605_erm_open_loop_and_standby)
{
    boot();
    CHECK(hapm_configured_erm_open_loop());
    CHECK(hapm_in_standby());
    uint32_t before = hapm_play_count();
    CHECK_EQ(drv2605_play(HAPTIC_BUZZ), 0);
    CHECK_EQ(hapm_play_count(), before + 1);
    CHECK_EQ(hapm_last_effect(), HAPTIC_BUZZ);
    bool playing = false;
    CHECK_EQ(drv2605_is_playing(&playing), 0);
    CHECK(playing);
    REQUIRE(sim_run_for(600));
    CHECK_EQ(drv2605_is_playing(&playing), 0);
    CHECK(!playing);
    CHECK_EQ(hapm_errors(), 0);
}

TEST(drv_epd_full_then_partial_shows_exact_frames)
{
    static gfx_t a, b, panel;
    boot();
    const epdm_stats_t *st = epdm_stats();
    const uint32_t full0 = st->full, part0 = st->partial;

    gfx_clear(&a);
    gfx_fill(&a, 0, 0, 100, 200, true);
    gfx_text(&a, 120, 90, "A", 4, true);
    REQUIRE(epd_update(a.px, true) == 0);
    CHECK(epd_busy());
    REQUIRE(sim_run_for(EPDM_FULL_MS + 10));
    CHECK(!epd_busy());
    epd_sleep();
    sim_panel(&panel);
    CHECK(memcmp(panel.px, a.px, GFX_BYTES) == 0);

    gfx_clear(&b);
    gfx_fill(&b, 50, 0, 150, 100, true);
    REQUIRE(epd_update(b.px, false) == 0);
    REQUIRE(sim_run_for(EPDM_PARTIAL_MS + 10));
    epd_sleep();
    sim_panel(&panel);
    CHECK(memcmp(panel.px, b.px, GFX_BYTES) == 0);

    CHECK_EQ(st->full, full0 + 1);
    CHECK_EQ(st->partial, part0 + 1);
    CHECK(epdm_sleeping());
    CHECK_EQ(st->errors, 0);
}

TEST(drv_epd_busy_high_in_deep_sleep)
{
    boot();
    /* After boot the controller is in deep sleep: BUSY reads high. */
    CHECK(epdm_sleeping());
    CHECK(epd_busy());
}
