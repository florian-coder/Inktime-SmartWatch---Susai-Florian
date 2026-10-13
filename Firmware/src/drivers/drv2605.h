/* TI DRV2605 haptic driver, driving the FIT0774 coin ERM motor in open loop. */
#ifndef DRV2605_H
#define DRV2605_H

#include <stdbool.h>
#include <stdint.h>

#define DRV2605_REG_STATUS   0x00
#define DRV2605_REG_MODE     0x01
#define DRV2605_REG_LIBRARY  0x03
#define DRV2605_REG_WAVESEQ1 0x04
#define DRV2605_REG_WAVESEQ2 0x05
#define DRV2605_REG_GO       0x0C
#define DRV2605_REG_RATED_V  0x16
#define DRV2605_REG_OD_CLAMP 0x17
#define DRV2605_REG_FEEDBACK 0x1A
#define DRV2605_REG_CONTROL3 0x1D

#define DRV2605_MODE_RESET     0x80
#define DRV2605_MODE_STANDBY   0x40
#define DRV2605_MODE_INT_TRIG  0x00
#define DRV2605_FEEDBACK_LRA   0x80
#define DRV2605_CTRL3_ERM_OPEN 0x20
#define DRV2605_LIB_ERM_A      0x01

/* Effects from the ERM library (DRV2605 datasheet, table 11.2). */
#define HAPTIC_CLICK        1  /* Strong Click 100% */
#define HAPTIC_DOUBLE_CLICK 10 /* Double Click 100% */
#define HAPTIC_BUZZ         14 /* Strong Buzz 100% */

int drv2605_init(void);
int drv2605_play(uint8_t effect);
int drv2605_is_playing(bool *playing);
int drv2605_standby(void);

#endif
