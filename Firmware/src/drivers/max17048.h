/* Maxim MAX17048 ModelGauge fuel gauge. 16-bit big-endian registers. */
#ifndef MAX17048_H
#define MAX17048_H

#include <stdint.h>

#define MAX17048_REG_VCELL   0x02
#define MAX17048_REG_SOC     0x04
#define MAX17048_REG_MODE    0x06
#define MAX17048_REG_VERSION 0x08
#define MAX17048_REG_CONFIG  0x0C
#define MAX17048_REG_CRATE   0x16
#define MAX17048_REG_STATUS  0x1A
#define MAX17048_REG_CMD     0xFE

#define MAX17048_CONFIG_ALRT     0x0020
#define MAX17048_CONFIG_ATHD_MSK 0x001F
#define MAX17048_STATUS_RI       0x0100
#define MAX17048_STATUS_FLAGS    0x3E00 /* VH VL VR HD SC */

typedef struct {
    uint16_t mv;
    uint8_t pct;
    int16_t crate_x10; /* charge rate in 0.1 %/h, negative when discharging */
} max17048_reading_t;

/* Checks the version, clears the reset indicator and sets the low-SOC alert threshold. */
int max17048_init(uint8_t alert_pct);
int max17048_read(max17048_reading_t *r);
/* Clears the alert pin and status flags; returns the status register before clearing. */
int max17048_clear_alert(uint16_t *status);

#endif
