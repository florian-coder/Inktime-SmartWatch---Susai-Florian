#include "watch.h"

#include "board.h"
#include "core/evq.h"
#include "core/log.h"
#include "core/util.h"
#include "drivers/bma423.h"
#include "drivers/bq25180.h"
#include "drivers/drv2605.h"
#include "drivers/epd_ssd1681.h"
#include "drivers/max17048.h"
#include "hal/hal.h"
#include "services/buttons.h"
#include "services/display.h"
#include "services/haptics.h"

#define POWER_POLL_MS 60000
#define FG_ALERT_PCT  5

enum { IRQ_BTN_UP, IRQ_BTN_ENTER, IRQ_BTN_DOWN, IRQ_IMU, IRQ_PMIC, IRQ_FG, IRQ_COUNT };

static app_t s_app;
static gfx_t s_fb;
static evq_t s_q;
static uint32_t s_hw;
static int s_irq_bit[IRQ_COUNT];
static uint64_t s_next_tick;
static uint64_t s_next_power_poll;

static bool irq_hit(uint32_t changes, int irq)
{
    return s_irq_bit[irq] >= 0 && (changes & (1u << s_irq_bit[irq]));
}

static void emit_button(btn_id_t id, press_t kind, void *ctx)
{
    static const char *const names[BTN_COUNT] = { "up", "enter", "down" };
    static const char *const kinds[] = { "", "short", "long", "repeat" };
    (void)ctx;
    log_printf("button: %s %s", names[id], kinds[kind]);
    evq_push(&s_q, (event_t){ .type = EV_BUTTON, .a = (uint8_t)id, .b = (uint8_t)kind });
}

static void read_steps(void)
{
    uint32_t steps;
    if ((s_hw & HW_IMU) && bma423_read_steps(&steps) == 0) {
        log_printf("steps: %lu", (unsigned long)steps);
        evq_push(&s_q, (event_t){ .type = EV_STEPS, .u32 = steps });
    }
}

static void read_power(void)
{
    app_power_t p = s_app.power;
    if (s_hw & HW_CHG) {
        bq25180_status_t st;
        if (bq25180_read_status(&st) == 0) {
            p.vin = st.vin_good;
            p.charging = st.vin_good && (st.state == CHG_CONSTANT_CURRENT || st.state == CHG_CONSTANT_VOLTAGE);
            p.full = st.vin_good && st.state == CHG_DONE;
        }
    }
    if (s_hw & HW_FG) {
        max17048_reading_t r;
        if (max17048_read(&r) == 0) {
            p.pct = r.pct;
            p.mv = r.mv;
        }
    }
    if (p.pct != s_app.power.pct || p.mv != s_app.power.mv || p.vin != s_app.power.vin ||
        p.charging != s_app.power.charging || p.full != s_app.power.full) {
        log_printf("power: %u%% %umV usb=%d charging=%d full=%d", p.pct, p.mv, p.vin, p.charging, p.full);
        s_app.power = p;
        evq_push(&s_q, (event_t){ .type = EV_POWER });
    }
}

void watch_init(void)
{
    hal_init();
    log_printf("InkTime fw %s booting (reset 0x%lx)", FW_VERSION, (unsigned long)hal_reset_reason());

    hal_i2c_init(PIN_I2C_SDA, PIN_I2C_SCL);
    s_hw = 0;
    if (epd_init() == 0) s_hw |= HW_EPD;
    if (bma423_init() == 0) s_hw |= HW_IMU;
    if (max17048_init(FG_ALERT_PCT) == 0) s_hw |= HW_FG;
    if (bq25180_init(BATTERY_CHARGE_MA, BATTERY_VREG_MV) == 0) s_hw |= HW_CHG;
    if (drv2605_init() == 0) s_hw |= HW_HAP;
    log_printf("hw: epd %s imu %s fg %s chg %s hap %s", (s_hw & HW_EPD) ? "ok" : "FAIL",
               (s_hw & HW_IMU) ? "ok" : "FAIL", (s_hw & HW_FG) ? "ok" : "FAIL", (s_hw & HW_CHG) ? "ok" : "FAIL",
               (s_hw & HW_HAP) ? "ok" : "FAIL");

    static const uint8_t btn_pins[BTN_COUNT] = { PIN_BTN_UP, PIN_BTN_ENTER, PIN_BTN_DOWN };
    buttons_init(btn_pins);
    hal_gpio_input(PIN_IMU_INT1, HAL_PULL_DOWN);
    hal_gpio_input(PIN_PMIC_INT, HAL_PULL_UP);
    hal_gpio_input(PIN_FG_ALERT, HAL_PULL_UP);
    s_irq_bit[IRQ_BTN_UP] = hal_gpio_watch(PIN_BTN_UP);
    s_irq_bit[IRQ_BTN_ENTER] = hal_gpio_watch(PIN_BTN_ENTER);
    s_irq_bit[IRQ_BTN_DOWN] = PIN_BTN_DOWN == PIN_NC ? -1 : hal_gpio_watch(PIN_BTN_DOWN);
    s_irq_bit[IRQ_IMU] = hal_gpio_watch(PIN_IMU_INT1);
    s_irq_bit[IRQ_PMIC] = hal_gpio_watch(PIN_PMIC_INT);
    s_irq_bit[IRQ_FG] = hal_gpio_watch(PIN_FG_ALERT);

    display_init(s_hw & HW_EPD);
    haptics_init(s_hw & HW_HAP);
    evq_init(&s_q);

    const uint64_t now = hal_millis();
    app_init(&s_app, APP_DEFAULT_EPOCH, now);
    s_app.hw_ok = s_hw;
    s_next_tick = app_next_tick_ms(&s_app, now);
    s_next_power_poll = now + POWER_POLL_MS;

    read_power();
    read_steps();
    /* First frame: push a tick so the app renders the watch face. */
    evq_push(&s_q, (event_t){ .type = EV_TICK, .u32 = app_epoch(&s_app, now) });
    log_printf("screen: %s", app_screen_name(s_app.screen));
}

void watch_step(void)
{
    hal_watchdog_feed();
    const uint64_t now = hal_millis();
    const uint32_t changes = hal_gpio_take_changes();

    for (int b = 0; b < BTN_COUNT; b++) {
        if (irq_hit(changes, IRQ_BTN_UP + b)) {
            buttons_on_edge((btn_id_t)b, now);
        }
    }
    if (irq_hit(changes, IRQ_IMU) && hal_gpio_read(PIN_IMU_INT1)) {
        uint8_t st;
        if (bma423_read_int_status(&st) == 0 && (st & BMA423_INT_STEP_COUNTER)) {
            read_steps();
        }
    }
    if (irq_hit(changes, IRQ_PMIC)) {
        read_power();
    }
    if (irq_hit(changes, IRQ_FG) && !hal_gpio_read(PIN_FG_ALERT)) {
        uint16_t status = 0;
        max17048_clear_alert(&status);
        log_printf("power: fuel gauge alert (status 0x%04x)", status);
        read_power();
        evq_push(&s_q, (event_t){ .type = EV_BATT_LOW });
    }

    const uint64_t btn_deadline = buttons_poll(now, emit_button, NULL);

    if (now >= s_next_tick) {
        s_next_tick = app_next_tick_ms(&s_app, now);
        evq_push(&s_q, (event_t){ .type = EV_TICK, .u32 = app_epoch(&s_app, now) });
    }
    if (now >= s_next_power_poll) {
        s_next_power_poll = now + POWER_POLL_MS;
        read_power();
        read_steps();
    }

    bool handled = false;
    event_t ev;
    while (evq_pop(&s_q, &ev)) {
        const uint32_t epoch_before = s_app.epoch_base;
        app_handle(&s_app, &ev, now);
        if (s_app.epoch_base != epoch_before) {
            s_next_tick = app_next_tick_ms(&s_app, now); /* clock was set */
        }
        if (s_app.haptic) {
            haptics_play(s_app.haptic, now);
            s_app.haptic = 0;
        }
        handled = true;
    }
    if (handled) {
        display_set_full_every(s_app.full_every);
        app_render(&s_app, &s_fb, now);
        display_show(&s_fb, s_app.force_full);
        s_app.force_full = false;
    }

    const uint64_t disp_deadline = display_poll(now);
    const uint64_t hap_deadline = haptics_poll(now);

    if (s_app.poweroff && display_idle()) {
        log_printf("power: system off");
        epd_power_off();
        hal_system_off(PIN_BTN_ENTER);
    }

    uint64_t next = s_next_tick;
    next = DEADLINE_MIN(next, s_next_power_poll);
    next = DEADLINE_MIN(next, btn_deadline);
    next = DEADLINE_MIN(next, disp_deadline);
    next = DEADLINE_MIN(next, hap_deadline);
    hal_set_wakeup(next);
    hal_sleep();
}

void watch_notify(const char *text)
{
    app_set_notification(&s_app, text);
    log_printf("notify: %s", s_app.notify);
    evq_push(&s_q, (event_t){ .type = EV_NOTIFY });
}

void watch_set_epoch(uint32_t epoch)
{
    const uint64_t now = hal_millis();
    app_set_epoch(&s_app, epoch, now);
    s_next_tick = now; /* deliver a tick right away so the screen updates */
}

const app_t *watch_app(void)
{
    return &s_app;
}

const gfx_t *watch_framebuffer(void)
{
    return &s_fb;
}

uint32_t watch_hw_status(void)
{
    return s_hw;
}
