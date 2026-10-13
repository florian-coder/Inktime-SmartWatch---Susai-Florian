/*
 * Host implementation of the HAL: a virtual MCU with simulated time.
 *
 * The simulator (sim/) installs hooks to receive SPI / I2C traffic and pin
 * writes, and drives input pins the way the real board's peripherals would.
 * Time only moves when the simulator advances it, so runs are deterministic.
 */
#ifndef HAL_HOST_H
#define HAL_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HOST_NUM_PINS 64

typedef struct {
    void (*on_output)(uint8_t pin, bool level, void *ctx);
    void (*on_spi)(const uint8_t *buf, size_t len, void *ctx);
    int (*on_i2c)(uint8_t addr, const uint8_t *w, size_t wl, uint8_t *r, size_t rl, void *ctx);
    /* The MCU sleeps until `deadline` (0 = no timer). The hook should advance time. */
    void (*on_idle)(uint64_t deadline, void *ctx);
    void (*on_advance)(uint64_t now, void *ctx);
    void (*on_log)(const char *s, size_t len, void *ctx);
    void (*on_system_off)(uint8_t wake_pin, void *ctx);
    void *ctx;
} host_hooks_t;

/* Reset the virtual MCU (pins, time is kept unless reset_time). */
void host_init(const host_hooks_t *hooks, bool reset_time, uint32_t reset_reason);
uint64_t host_now(void);
void host_advance_to(uint64_t t);

void host_pin_drive(uint8_t pin, bool level);
void host_pin_release(uint8_t pin);
bool host_pin_level(uint8_t pin);
bool host_pin_is_output(uint8_t pin);

uint64_t host_wakeup(void);
bool host_changes_pending(void);
bool host_is_off(void);
uint32_t host_spi_bytes(void);
uint32_t host_i2c_transactions(void);

#endif
