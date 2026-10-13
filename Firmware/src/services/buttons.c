#include "buttons.h"

#include "board.h"
#include "core/util.h"
#include "hal/hal.h"

typedef struct {
    uint8_t pin;
    bool pressed; /* debounced state */
    bool long_fired;
    uint64_t check_at; /* debounce sample time, 0 = none */
    uint64_t pressed_at;
    uint64_t next_repeat;
} btn_t;

static btn_t s_btn[BTN_COUNT];

void buttons_init(const uint8_t pins[BTN_COUNT])
{
    for (int i = 0; i < BTN_COUNT; i++) {
        s_btn[i] = (btn_t){ .pin = pins[i] };
        if (pins[i] != PIN_NC) {
            /* External 10k pull-ups exist; the internal one is a cheap safety net. */
            hal_gpio_input(pins[i], HAL_PULL_UP);
        }
    }
}

void buttons_on_edge(btn_id_t id, uint64_t now)
{
    if (id < BTN_COUNT) {
        s_btn[id].check_at = now + BTN_DEBOUNCE_MS;
    }
}

static bool repeats(btn_id_t id)
{
    return id == BTN_UP || id == BTN_DOWN;
}

uint64_t buttons_poll(uint64_t now, btn_emit_fn emit, void *ctx)
{
    uint64_t next = 0;
    for (int i = 0; i < BTN_COUNT; i++) {
        btn_t *b = &s_btn[i];

        if (b->pin == PIN_NC) {
            continue;
        }
        if (b->check_at && now >= b->check_at) {
            b->check_at = 0;
            bool level_pressed = !hal_gpio_read(b->pin);
            if (level_pressed != b->pressed) {
                b->pressed = level_pressed;
                if (level_pressed) {
                    b->pressed_at = now;
                    b->long_fired = false;
                } else if (!b->long_fired) {
                    emit((btn_id_t)i, PRESS_SHORT, ctx);
                }
            }
        }

        if (b->pressed && !b->long_fired && now >= b->pressed_at + BTN_LONG_MS) {
            b->long_fired = true;
            b->next_repeat = now + BTN_REPEAT_MS;
            emit((btn_id_t)i, PRESS_LONG, ctx);
        } else if (b->pressed && b->long_fired && repeats((btn_id_t)i) && now >= b->next_repeat) {
            b->next_repeat = now + BTN_REPEAT_MS;
            emit((btn_id_t)i, PRESS_REPEAT, ctx);
        }

        next = DEADLINE_MIN(next, b->check_at);
        if (b->pressed && !b->long_fired) {
            next = DEADLINE_MIN(next, b->pressed_at + BTN_LONG_MS);
        } else if (b->pressed && repeats((btn_id_t)i)) {
            next = DEADLINE_MIN(next, b->next_repeat);
        }
    }
    return next;
}

bool buttons_any_pressed(void)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        if (s_btn[i].pressed) {
            return true;
        }
    }
    return false;
}
