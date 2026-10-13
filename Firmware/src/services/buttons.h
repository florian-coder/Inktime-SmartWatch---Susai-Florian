/* Debounce, short / long press and auto-repeat for the three active-low buttons. */
#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { BTN_UP, BTN_ENTER, BTN_DOWN, BTN_COUNT } btn_id_t;
typedef enum { PRESS_SHORT = 1, PRESS_LONG = 2, PRESS_REPEAT = 3 } press_t;

#define BTN_DEBOUNCE_MS 25
#define BTN_LONG_MS     700
#define BTN_REPEAT_MS   200

typedef void (*btn_emit_fn)(btn_id_t id, press_t kind, void *ctx);

void buttons_init(const uint8_t pins[BTN_COUNT]);
/* Called when the HAL reports a level change on a button pin. */
void buttons_on_edge(btn_id_t id, uint64_t now);
/* Runs the state machines; returns the next time it needs to run (0 = idle). */
uint64_t buttons_poll(uint64_t now, btn_emit_fn emit, void *ctx);
bool buttons_any_pressed(void);

#endif
