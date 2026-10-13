/* Plays DRV2605 effects and puts the driver back into standby when they end. */
#ifndef HAPTICS_H
#define HAPTICS_H

#include <stdbool.h>
#include <stdint.h>

#define HAPTICS_POLL_MS 50

void haptics_init(bool hw_ok);
void haptics_play(uint8_t effect, uint64_t now);
uint64_t haptics_poll(uint64_t now);
uint32_t haptics_count(void);
uint8_t haptics_last_effect(void);

#endif
