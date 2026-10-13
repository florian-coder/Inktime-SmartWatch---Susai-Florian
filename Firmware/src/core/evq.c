#include "evq.h"

#include <string.h>

void evq_init(evq_t *q)
{
    memset(q, 0, sizeof(*q));
}

bool evq_push(evq_t *q, event_t ev)
{
    if (q->count == EVQ_CAPACITY) {
        q->dropped++;
        return false;
    }
    q->buf[(q->head + q->count) % EVQ_CAPACITY] = ev;
    q->count++;
    return true;
}

bool evq_pop(evq_t *q, event_t *ev)
{
    if (q->count == 0) {
        return false;
    }
    *ev = q->buf[q->head];
    q->head = (uint8_t)((q->head + 1) % EVQ_CAPACITY);
    q->count--;
    return true;
}
