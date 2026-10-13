/* System layer: wires HAL, drivers, services and the app into one event loop. */
#ifndef WATCH_H
#define WATCH_H

#include <stdint.h>

#include "app/app.h"
#include "gfx/gfx.h"

void watch_init(void);
/* One loop iteration: handle pending events, refresh the screen, sleep until the next deadline. */
void watch_step(void);

/* Show a notification (the simulator uses this; a BLE service would too). */
void watch_notify(const char *text);
/* Set the wall clock (time sync from the simulator / a future BLE service). */
void watch_set_epoch(uint32_t epoch);

const app_t *watch_app(void);
const gfx_t *watch_framebuffer(void);
uint32_t watch_hw_status(void);

#endif
