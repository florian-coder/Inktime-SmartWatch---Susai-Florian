#include "sim.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/app.h"
#include "board.h"
#include "hal/hal.h"
#include "hal/host/hal_host.h"
#include "models/models.h"
#include "services/display.h"
#include "watch.h"

#define LIVELOCK_STEPS 20000

static uint64_t s_target;
static bool s_echo;
static char s_log[SIM_LOG_LINES][112];
static int s_log_count;
static char s_partial[112];
static size_t s_partial_len;
static uint32_t s_extra_errors;
static char s_extra_error[96];

static const uint8_t btn_pin[BTN_COUNT] = { PIN_BTN_UP, PIN_BTN_ENTER, PIN_BTN_DOWN };

static uint64_t models_next_event(uint64_t now)
{
    uint64_t next = 0;
    const uint64_t ev[] = { epdm_next_event(), chgm_next_event(), hapm_next_event() };
    for (size_t i = 0; i < sizeof(ev) / sizeof(ev[0]); i++) {
        if (ev[i] > now && (next == 0 || ev[i] < next)) {
            next = ev[i];
        }
    }
    return next;
}

static void on_output(uint8_t pin, bool level, void *ctx)
{
    (void)ctx;
    epdm_on_output(pin, level, host_now());
}

static void on_spi(const uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    epdm_on_spi(buf, len, host_now());
}

static int on_i2c(uint8_t addr, const uint8_t *w, size_t wl, uint8_t *r, size_t rl, void *ctx)
{
    (void)ctx;
    switch (addr) {
    case I2C_ADDR_BMA423: return bmam_i2c(w, wl, r, rl);
    case I2C_ADDR_MAX17048: return fgm_i2c(w, wl, r, rl);
    case I2C_ADDR_BQ25180: return chgm_i2c(w, wl, r, rl, host_now());
    case I2C_ADDR_DRV2605: return hapm_i2c(w, wl, r, rl, host_now());
    default: return -1; /* NACK */
    }
}

static void on_idle(uint64_t deadline, void *ctx)
{
    (void)ctx;
    const uint64_t now = host_now();
    uint64_t t = s_target;
    if (deadline && deadline < t) {
        t = deadline;
    }
    uint64_t m = models_next_event(now);
    if (m && m < t) {
        t = m;
    }
    if (t > now) {
        host_advance_to(t);
    }
}

static void on_advance(uint64_t now, void *ctx)
{
    (void)ctx;
    epdm_advance(now);
    chgm_advance(now);
    hapm_advance(now);
}

static void store_line(const char *line)
{
    if (s_echo) {
        printf("[%7.3f] %s\n", (double)host_now() / 1000.0, line);
        fflush(stdout);
    }
    if (s_log_count < SIM_LOG_LINES) {
        snprintf(s_log[s_log_count++], sizeof(s_log[0]), "%s", line);
    } else {
        memmove(s_log[0], s_log[1], sizeof(s_log[0]) * (SIM_LOG_LINES - 1));
        snprintf(s_log[SIM_LOG_LINES - 1], sizeof(s_log[0]), "%s", line);
    }
}

static void on_log(const char *s, size_t len, void *ctx)
{
    (void)ctx;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n' || s_partial_len == sizeof(s_partial) - 1) {
            s_partial[s_partial_len] = '\0';
            store_line(s_partial);
            s_partial_len = 0;
            if (c == '\n') {
                continue;
            }
        }
        s_partial[s_partial_len++] = c;
    }
}

static const host_hooks_t hooks = {
    .on_output = on_output,
    .on_spi = on_spi,
    .on_i2c = on_i2c,
    .on_idle = on_idle,
    .on_advance = on_advance,
    .on_log = on_log,
    .on_system_off = NULL,
    .ctx = NULL,
};

static void release_buttons(void)
{
    /* Released buttons read high through the board's 10k pull-ups. */
    for (int i = 0; i < BTN_COUNT; i++) {
        host_pin_drive(btn_pin[i], true);
    }
}

void sim_init(void)
{
    if (getenv("SIM_ECHO")) {
        s_echo = true;
    }
    s_log_count = 0;
    s_partial_len = 0;
    s_extra_errors = 0;
    s_extra_error[0] = '\0';
    host_init(&hooks, true, HAL_RESET_POWER_ON);
    epdm_reset();
    bmam_reset();
    fgm_reset();
    chgm_reset();
    hapm_reset();
    release_buttons();
    s_target = 0;
    watch_init();
}

void sim_reboot(uint32_t reset_reason)
{
    host_init(&hooks, false, reset_reason);
    watch_init();
}

uint64_t sim_now(void)
{
    return host_now();
}

bool sim_run_for(uint64_t ms)
{
    s_target = host_now() + ms;
    int stalled = 0;
    while (host_now() < s_target) {
        if (host_is_off()) {
            host_advance_to(s_target);
            break;
        }
        const uint64_t before = host_now();
        watch_step();
        if (host_now() == before) {
            if (++stalled > LIVELOCK_STEPS) {
                s_extra_errors++;
                snprintf(s_extra_error, sizeof(s_extra_error), "livelock at t=%" PRIu64 " ms", before);
                fprintf(stderr, "sim: %s\n", s_extra_error);
                return false;
            }
        } else {
            stalled = 0;
        }
    }
    /* Deliver anything that became pending exactly at the target time. */
    if (!host_is_off()) {
        for (int i = 0; i < 4 && (host_changes_pending() || (host_wakeup() && host_wakeup() <= host_now())); i++) {
            s_target = host_now();
            watch_step();
        }
    }
    return true;
}

bool sim_settle(uint64_t max_ms)
{
    const uint64_t end = host_now() + max_ms;
    while (host_now() < end) {
        if (!sim_run_for(10)) {
            return false;
        }
        if (host_is_off() || (display_idle() && !epdm_refreshing(host_now()) && !buttons_any_pressed())) {
            /* One more step so the post-refresh deep sleep command is sent. */
            return sim_run_for(30);
        }
    }
    return false;
}

void sim_button(btn_id_t b, bool pressed)
{
    if (b >= BTN_COUNT) {
        return;
    }
    if (host_is_off()) {
        host_pin_drive(btn_pin[b], !pressed);
        if (pressed && b == BTN_ENTER) {
            /* Pin SENSE wakes the chip from System OFF through a reset. */
            sim_reboot(HAL_RESET_WAKE_OFF);
        }
        return;
    }
    host_pin_drive(btn_pin[b], !pressed);
}

void sim_click(btn_id_t b, uint32_t hold_ms)
{
    sim_button(b, true);
    sim_run_for(hold_ms);
    sim_button(b, false);
    sim_run_for(BTN_DEBOUNCE_MS + 20);
}

void sim_add_steps(uint32_t n)
{
    bmam_add_steps(n);
}

void sim_set_battery(uint8_t pct, uint16_t mv)
{
    fgm_set(pct, mv);
}

void sim_set_charger(bool vin, chg_state_t state)
{
    chgm_set(vin, (uint8_t)state, host_now());
}

void sim_notify(const char *text)
{
    watch_notify(text);
}

void sim_set_epoch(uint32_t epoch)
{
    watch_set_epoch(epoch);
}

void sim_panel(gfx_t *out)
{
    const uint8_t *p = epdm_panel();
    for (int i = 0; i < GFX_BYTES; i++) {
        out->px[i] = (uint8_t)~p[i];
    }
}

bool sim_is_off(void)
{
    return host_is_off();
}

uint32_t sim_model_errors(void)
{
    return epdm_stats()->errors + bmam_errors() + fgm_errors() + chgm_errors() + hapm_errors() + s_extra_errors;
}

const char *sim_model_error_text(void)
{
    if (epdm_stats()->errors) return epdm_stats()->last_error;
    if (bmam_errors()) return "bma423 protocol error";
    if (fgm_errors()) return "max17048 protocol error";
    if (chgm_errors()) return "bq25180 protocol error";
    if (hapm_errors()) return "drv2605 protocol error";
    return s_extra_error;
}

void sim_log_echo(bool on)
{
    s_echo = on;
}

int sim_log_count(void)
{
    return s_log_count;
}

const char *sim_log_line(int i)
{
    return (i >= 0 && i < s_log_count) ? s_log[i] : "";
}

int sim_log_find(const char *needle, int from)
{
    for (int i = from < 0 ? 0 : from; i < s_log_count; i++) {
        if (strstr(s_log[i], needle)) {
            return i;
        }
    }
    return -1;
}
