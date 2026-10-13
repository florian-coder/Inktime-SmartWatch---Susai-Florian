/*
 * Hardware abstraction layer.
 *
 * Everything above this header (drivers, services, app) is target-independent.
 * Two implementations exist:
 *   hal/nrf52/hal_nrf52.c  - bare-metal nRF52840 register code (real board / Renode)
 *   hal/host/hal_host.c    - virtual board used by the simulator and the tests
 */
#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HAL_PULL_NONE,
    HAL_PULL_UP,
    HAL_PULL_DOWN,
} hal_pull_t;

#define HAL_RESET_POWER_ON 0u
#define HAL_RESET_PIN      (1u << 0)
#define HAL_RESET_WATCHDOG (1u << 1)
#define HAL_RESET_SOFT     (1u << 2)
#define HAL_RESET_LOCKUP   (1u << 3)
#define HAL_RESET_WAKE_OFF (1u << 16) /* woke from System OFF */

void hal_init(void);

/* Monotonic milliseconds since boot (32.768 kHz RTC based). */
uint64_t hal_millis(void);
/* Arm the wake-up timer at absolute time at_ms. 0 disarms it. */
void hal_set_wakeup(uint64_t at_ms);
/* Sleep until an interrupt is pending (pin change or wake-up timer). */
void hal_sleep(void);
/* Blocking delay. Only for short waits during driver initialisation. */
void hal_delay_ms(uint32_t ms);

void hal_gpio_output(uint8_t pin, bool level);
void hal_gpio_input(uint8_t pin, hal_pull_t pull);
void hal_gpio_write(uint8_t pin, bool level);
bool hal_gpio_read(uint8_t pin);
/* Watch an input pin for level changes. Returns the bit used in the mask
 * returned by hal_gpio_take_changes(), or -1 if no slot is left. */
int hal_gpio_watch(uint8_t pin);
/* Atomically fetch and clear the mask of watched pins that changed. */
uint32_t hal_gpio_take_changes(void);

void hal_spi_init(uint8_t sck, uint8_t mosi);
void hal_spi_write(const uint8_t *buf, size_t len);

void hal_i2c_init(uint8_t sda, uint8_t scl);
/* Return 0 on success, negative on NACK / bus error. */
int hal_i2c_write(uint8_t addr, const uint8_t *buf, size_t len);
int hal_i2c_write_read(uint8_t addr, const uint8_t *wbuf, size_t wlen, uint8_t *rbuf, size_t rlen);

void hal_log_write(const char *s, size_t len);
/* Reload the watchdog (started by hal_init, timeout HAL_WATCHDOG_S, runs during sleep). */
#define HAL_WATCHDOG_S 120
void hal_watchdog_feed(void);
uint32_t hal_reset_reason(void);
/* Enter the lowest power state; a low level on wake_pin resets the chip. */
void hal_system_off(uint8_t wake_pin);

#endif
