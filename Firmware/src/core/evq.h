/* Fixed-size event queue. Only touched from the main loop (ISRs set flags in the HAL). */
#ifndef EVQ_H
#define EVQ_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EV_NONE = 0,
    EV_TICK,     /* once per second; u32 = epoch seconds */
    EV_BUTTON,   /* a = button id, b = press kind */
    EV_STEPS,    /* u32 = steps today */
    EV_POWER,    /* battery / charger state changed */
    EV_BATT_LOW, /* fuel gauge alert */
    EV_NOTIFY,   /* text in app notification buffer */
} ev_type_t;

typedef struct {
    uint8_t type;
    uint8_t a;
    uint8_t b;
    uint32_t u32;
} event_t;

#define EVQ_CAPACITY 16

typedef struct {
    event_t buf[EVQ_CAPACITY];
    uint8_t head;
    uint8_t count;
    uint16_t dropped;
} evq_t;

void evq_init(evq_t *q);
bool evq_push(evq_t *q, event_t ev);
bool evq_pop(evq_t *q, event_t *ev);

#endif
