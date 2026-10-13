/*
 * Virtual InkTime board: the unmodified firmware (watch.c and everything below it)
 * running on the host HAL, wired to the peripheral models. Used by the unit tests,
 * the scenario runner and the interactive simulator.
 */
#ifndef SIM_H
#define SIM_H

#include <stdbool.h>
#include <stdint.h>

#include "drivers/bq25180.h"
#include "gfx/gfx.h"
#include "services/buttons.h"

#define SIM_LOG_LINES 512

/* Fresh board (time = 0, default world state) and firmware boot. */
void sim_init(void);
/* MCU-only reset (peripheral models keep their state), then boot. */
void sim_reboot(uint32_t reset_reason);

/* Run the firmware for `ms` of virtual time. Returns false on livelock. */
bool sim_run_for(uint64_t ms);
/* Run until the display and haptics are idle (or max_ms elapsed). */
bool sim_settle(uint64_t max_ms);
uint64_t sim_now(void);

void sim_button(btn_id_t b, bool pressed);
/* Press, hold for hold_ms, release, and let debouncing finish. */
void sim_click(btn_id_t b, uint32_t hold_ms);

void sim_add_steps(uint32_t n);
void sim_set_battery(uint8_t pct, uint16_t mv);
void sim_set_charger(bool vin, chg_state_t state);
void sim_notify(const char *text);
void sim_set_epoch(uint32_t epoch);

/* Panel contents as a gfx framebuffer (1 = black). */
void sim_panel(gfx_t *out);
bool sim_is_off(void);
/* Sum of protocol errors reported by all peripheral models. */
uint32_t sim_model_errors(void);
const char *sim_model_error_text(void);

void sim_log_echo(bool on);
int sim_log_count(void);
const char *sim_log_line(int i);
/* Index of the first line at or after `from` that contains `needle`, or -1. */
int sim_log_find(const char *needle, int from);

#endif
