#include "hal_host.h"

#include <stdio.h>
#include <string.h>

#include "hal/hal.h"

#define MAX_WATCH 8

typedef struct {
    bool output;
    bool out_level;
    hal_pull_t pull;
    bool ext_driven;
    bool ext_level;
} pin_t;

static host_hooks_t s_hooks;
static pin_t s_pins[HOST_NUM_PINS];
static uint64_t s_now;
static uint64_t s_wakeup;
static uint8_t s_watch_pin[MAX_WATCH];
static bool s_watch_level[MAX_WATCH];
static int s_watch_count;
static uint32_t s_changes;
static bool s_off;
static uint32_t s_reset_reason;
static bool s_spi_ready;
static bool s_i2c_ready;
static uint32_t s_spi_bytes;
static uint32_t s_i2c_xfers;

static bool level_of(const pin_t *p)
{
    if (p->output) {
        return p->out_level;
    }
    if (p->ext_driven) {
        return p->ext_level;
    }
    return p->pull == HAL_PULL_UP;
}

static void update_watch(void)
{
    for (int i = 0; i < s_watch_count; i++) {
        bool lvl = level_of(&s_pins[s_watch_pin[i]]);
        if (lvl != s_watch_level[i]) {
            s_watch_level[i] = lvl;
            s_changes |= 1u << i;
        }
    }
}

void host_init(const host_hooks_t *hooks, bool reset_time, uint32_t reset_reason)
{
    if (hooks) {
        s_hooks = *hooks;
    }
    /* Externally driven levels survive an MCU reset; MCU-side config does not. */
    for (int i = 0; i < HOST_NUM_PINS; i++) {
        s_pins[i].output = false;
        s_pins[i].out_level = false;
        s_pins[i].pull = HAL_PULL_NONE;
    }
    if (reset_time) {
        memset(s_pins, 0, sizeof(s_pins));
        s_now = 0;
    }
    s_wakeup = 0;
    s_watch_count = 0;
    s_changes = 0;
    s_off = false;
    s_reset_reason = reset_reason;
    s_spi_ready = false;
    s_i2c_ready = false;
    s_spi_bytes = 0;
    s_i2c_xfers = 0;
}

uint64_t host_now(void)
{
    return s_now;
}

void host_advance_to(uint64_t t)
{
    if (t > s_now) {
        s_now = t;
    }
    if (s_hooks.on_advance) {
        s_hooks.on_advance(s_now, s_hooks.ctx);
    }
}

void host_pin_drive(uint8_t pin, bool level)
{
    if (pin < HOST_NUM_PINS) {
        s_pins[pin].ext_driven = true;
        s_pins[pin].ext_level = level;
        update_watch();
    }
}

void host_pin_release(uint8_t pin)
{
    if (pin < HOST_NUM_PINS) {
        s_pins[pin].ext_driven = false;
        update_watch();
    }
}

bool host_pin_level(uint8_t pin)
{
    return pin < HOST_NUM_PINS && level_of(&s_pins[pin]);
}

bool host_pin_is_output(uint8_t pin)
{
    return pin < HOST_NUM_PINS && s_pins[pin].output;
}

uint64_t host_wakeup(void)
{
    return s_wakeup;
}

bool host_changes_pending(void)
{
    return s_changes != 0;
}

bool host_is_off(void)
{
    return s_off;
}

uint32_t host_spi_bytes(void)
{
    return s_spi_bytes;
}

uint32_t host_i2c_transactions(void)
{
    return s_i2c_xfers;
}

/* ---- HAL API ---- */

void hal_init(void)
{
}

uint64_t hal_millis(void)
{
    return s_now;
}

void hal_set_wakeup(uint64_t at_ms)
{
    s_wakeup = at_ms;
}

void hal_sleep(void)
{
    if (s_off || s_changes != 0 || (s_wakeup != 0 && s_now >= s_wakeup)) {
        return;
    }
    if (s_hooks.on_idle) {
        s_hooks.on_idle(s_wakeup, s_hooks.ctx);
    }
}

void hal_delay_ms(uint32_t ms)
{
    host_advance_to(s_now + ms);
}

void hal_gpio_output(uint8_t pin, bool level)
{
    if (pin >= HOST_NUM_PINS) {
        return;
    }
    const bool was_output = s_pins[pin].output;
    s_pins[pin].output = true;
    s_pins[pin].out_level = level;
    update_watch();
    /* Switching to output starts driving the pin: always tell the board. */
    if (!was_output && s_hooks.on_output) {
        s_hooks.on_output(pin, level, s_hooks.ctx);
    }
}

void hal_gpio_input(uint8_t pin, hal_pull_t pull)
{
    if (pin >= HOST_NUM_PINS) {
        return;
    }
    s_pins[pin].output = false;
    s_pins[pin].pull = pull;
    update_watch();
}

void hal_gpio_write(uint8_t pin, bool level)
{
    if (pin >= HOST_NUM_PINS || !s_pins[pin].output) {
        return;
    }
    bool changed = s_pins[pin].out_level != level;
    s_pins[pin].out_level = level;
    update_watch();
    if (changed && s_hooks.on_output) {
        s_hooks.on_output(pin, level, s_hooks.ctx);
    }
}

bool hal_gpio_read(uint8_t pin)
{
    return host_pin_level(pin);
}

int hal_gpio_watch(uint8_t pin)
{
    if (s_watch_count >= MAX_WATCH || pin >= HOST_NUM_PINS) {
        return -1;
    }
    s_watch_pin[s_watch_count] = pin;
    s_watch_level[s_watch_count] = level_of(&s_pins[pin]);
    return s_watch_count++;
}

uint32_t hal_gpio_take_changes(void)
{
    uint32_t c = s_changes;
    s_changes = 0;
    return c;
}

void hal_spi_init(uint8_t sck, uint8_t mosi)
{
    (void)sck;
    (void)mosi;
    s_spi_ready = true;
}

void hal_spi_write(const uint8_t *buf, size_t len)
{
    if (!s_spi_ready) {
        fprintf(stderr, "hal_host: SPI used before init\n");
        return;
    }
    s_spi_bytes += (uint32_t)len;
    if (s_hooks.on_spi) {
        s_hooks.on_spi(buf, len, s_hooks.ctx);
    }
}

void hal_i2c_init(uint8_t sda, uint8_t scl)
{
    (void)sda;
    (void)scl;
    s_i2c_ready = true;
}

static int i2c_xfer(uint8_t addr, const uint8_t *w, size_t wl, uint8_t *r, size_t rl)
{
    if (!s_i2c_ready) {
        fprintf(stderr, "hal_host: I2C used before init\n");
        return -1;
    }
    s_i2c_xfers++;
    if (!s_hooks.on_i2c) {
        return -1;
    }
    return s_hooks.on_i2c(addr, w, wl, r, rl, s_hooks.ctx);
}

int hal_i2c_write(uint8_t addr, const uint8_t *buf, size_t len)
{
    return i2c_xfer(addr, buf, len, NULL, 0);
}

int hal_i2c_write_read(uint8_t addr, const uint8_t *wbuf, size_t wlen, uint8_t *rbuf, size_t rlen)
{
    return i2c_xfer(addr, wbuf, wlen, rbuf, rlen);
}

void hal_log_write(const char *s, size_t len)
{
    if (s_hooks.on_log) {
        s_hooks.on_log(s, len, s_hooks.ctx);
    }
}

void hal_watchdog_feed(void)
{
}

uint32_t hal_reset_reason(void)
{
    return s_reset_reason;
}

void hal_system_off(uint8_t wake_pin)
{
    s_off = true;
    if (s_hooks.on_system_off) {
        s_hooks.on_system_off(wake_pin, s_hooks.ctx);
    }
}
