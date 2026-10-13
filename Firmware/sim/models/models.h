/*
 * Behavioural models of the InkTime peripherals, driven by the host HAL.
 * They model the register/protocol level closely enough to catch driver bugs:
 * every model counts protocol violations in an `errors` counter.
 */
#ifndef MODELS_H
#define MODELS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- SSD1681 e-paper controller + panel ---- */
#define EPDM_FULL_MS    1800
#define EPDM_PARTIAL_MS 400
#define EPDM_BYTES      5000

typedef struct {
    uint32_t full;
    uint32_t partial;
    uint32_t sleeps;
    uint32_t hw_resets;
    uint32_t sw_resets;
    uint32_t errors;
    char last_error[96];
} epdm_stats_t;

void epdm_reset(void);
void epdm_on_output(uint8_t pin, bool level, uint64_t now);
void epdm_on_spi(const uint8_t *buf, size_t len, uint64_t now);
void epdm_advance(uint64_t now);
uint64_t epdm_next_event(void);
const uint8_t *epdm_panel(void); /* what the glass shows, 1 = white (panel format) */
const epdm_stats_t *epdm_stats(void);
bool epdm_sleeping(void);
bool epdm_refreshing(uint64_t now);

/* ---- BMA423 accelerometer ---- */
void bmam_reset(void);
int bmam_i2c(const uint8_t *w, size_t wl, uint8_t *r, size_t rl);
void bmam_add_steps(uint32_t n);
uint32_t bmam_steps(void);
bool bmam_feature_engine_ok(void);
bool bmam_step_counter_enabled(void);
uint32_t bmam_errors(void);

/* ---- MAX17048 fuel gauge ---- */
void fgm_reset(void);
int fgm_i2c(const uint8_t *w, size_t wl, uint8_t *r, size_t rl);
void fgm_set(uint8_t pct, uint16_t mv);
uint8_t fgm_alert_threshold(void);
bool fgm_alert_active(void);
uint32_t fgm_errors(void);

/* ---- BQ25180 charger ---- */
#define CHGM_WATCHDOG_MS 160000u
void chgm_reset(void);
int chgm_i2c(const uint8_t *w, size_t wl, uint8_t *r, size_t rl, uint64_t now);
void chgm_set(bool vin, uint8_t state, uint64_t now);
void chgm_advance(uint64_t now);
uint64_t chgm_next_event(void);
uint16_t chgm_ichg_ma(void);
uint16_t chgm_vreg_mv(void);
uint32_t chgm_watchdog_resets(void);
uint32_t chgm_errors(void);

/* ---- DRV2605 haptic driver ---- */
void hapm_reset(void);
int hapm_i2c(const uint8_t *w, size_t wl, uint8_t *r, size_t rl, uint64_t now);
void hapm_advance(uint64_t now);
uint64_t hapm_next_event(void);
uint32_t hapm_play_count(void);
uint8_t hapm_last_effect(void);
bool hapm_configured_erm_open_loop(void);
bool hapm_in_standby(void);
uint32_t hapm_errors(void);

#endif
