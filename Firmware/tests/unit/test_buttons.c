/* Button debouncing, long press and auto-repeat against the host HAL. */
#include "board.h"
#include "hal/host/hal_host.h"
#include "services/buttons.h"
#include "tst.h"

typedef struct {
    int count;
    btn_id_t id[16];
    press_t kind[16];
} events_t;

static void record(btn_id_t id, press_t kind, void *ctx)
{
    events_t *e = ctx;
    if (e->count < 16) {
        e->id[e->count] = id;
        e->kind[e->count] = kind;
        e->count++;
    }
}

static const uint8_t pins[BTN_COUNT] = { PIN_BTN_UP, PIN_BTN_ENTER, PIN_BTN_DOWN };

static void setup(void)
{
    static const host_hooks_t none = { 0 };
    host_init(&none, true, 0);
    for (int i = 0; i < BTN_COUNT; i++) {
        host_pin_drive(pins[i], true);
    }
    buttons_init(pins);
}

static void set(btn_id_t b, bool pressed, uint64_t t)
{
    host_pin_drive(pins[b], !pressed);
    buttons_on_edge(b, t);
}

/* Poll every millisecond from `from` to `to` inclusive. */
static void run(events_t *e, uint64_t from, uint64_t to)
{
    for (uint64_t t = from; t <= to; t++) {
        buttons_poll(t, record, e);
    }
}

TEST(buttons_short_press_emits_on_release)
{
    events_t e = { 0 };
    setup();
    set(BTN_ENTER, true, 0);
    run(&e, 0, 100);
    CHECK_EQ(e.count, 0);
    CHECK(buttons_any_pressed());
    set(BTN_ENTER, false, 101);
    run(&e, 101, 200);
    REQUIRE(e.count == 1);
    CHECK_EQ(e.id[0], BTN_ENTER);
    CHECK_EQ(e.kind[0], PRESS_SHORT);
    CHECK(!buttons_any_pressed());
}

TEST(buttons_bounce_shorter_than_debounce_is_ignored)
{
    events_t e = { 0 };
    setup();
    set(BTN_UP, true, 0);
    set(BTN_UP, false, 3);
    set(BTN_UP, true, 6);
    set(BTN_UP, false, 9);
    run(&e, 0, 300);
    CHECK_EQ(e.count, 0);
    CHECK(!buttons_any_pressed());
}

TEST(buttons_long_press_suppresses_short)
{
    events_t e = { 0 };
    setup();
    set(BTN_ENTER, true, 0);
    run(&e, 0, BTN_DEBOUNCE_MS + BTN_LONG_MS + 300);
    REQUIRE(e.count == 1);
    CHECK_EQ(e.kind[0], PRESS_LONG);
    set(BTN_ENTER, false, 2000);
    run(&e, 2000, 2100);
    CHECK_EQ(e.count, 1); /* no short press after a long one, and ENTER never repeats */
}

TEST(buttons_up_down_auto_repeat)
{
    events_t e = { 0 };
    setup();
    set(BTN_DOWN, true, 0);
    const uint64_t long_at = BTN_DEBOUNCE_MS + BTN_LONG_MS;
    run(&e, 0, long_at + 2 * BTN_REPEAT_MS);
    REQUIRE(e.count == 3);
    CHECK_EQ(e.kind[0], PRESS_LONG);
    CHECK_EQ(e.kind[1], PRESS_REPEAT);
    CHECK_EQ(e.kind[2], PRESS_REPEAT);
    CHECK_EQ(e.id[2], BTN_DOWN);
}

TEST(buttons_report_next_deadline)
{
    events_t e = { 0 };
    setup();
    CHECK_EQ(buttons_poll(0, record, &e), 0);
    set(BTN_UP, true, 10);
    CHECK_EQ(buttons_poll(10, record, &e), 10 + BTN_DEBOUNCE_MS);
    CHECK_EQ(buttons_poll(10 + BTN_DEBOUNCE_MS, record, &e), 10 + BTN_DEBOUNCE_MS + BTN_LONG_MS);
}

TEST(buttons_independent_channels)
{
    events_t e = { 0 };
    setup();
    set(BTN_UP, true, 0);
    run(&e, 0, 4);
    set(BTN_DOWN, true, 5);
    run(&e, 5, 99);
    set(BTN_UP, false, 100);
    run(&e, 100, 149);
    set(BTN_DOWN, false, 150);
    run(&e, 150, 300);
    REQUIRE(e.count == 2);
    CHECK_EQ(e.id[0], BTN_UP);
    CHECK_EQ(e.id[1], BTN_DOWN);
}
