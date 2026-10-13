/* TI BQ25180 linear Li-Ion charger with power path. */
#ifndef BQ25180_H
#define BQ25180_H

#include <stdbool.h>
#include <stdint.h>

#define BQ25180_REG_STAT0       0x00
#define BQ25180_REG_STAT1       0x01
#define BQ25180_REG_FLAG0       0x02
#define BQ25180_REG_VBAT_CTRL   0x03
#define BQ25180_REG_ICHG_CTRL   0x04
#define BQ25180_REG_CHARGECTRL0 0x05
#define BQ25180_REG_CHARGECTRL1 0x06
#define BQ25180_REG_IC_CTRL     0x07
#define BQ25180_REG_SHIP_RST    0x09
#define BQ25180_REG_MASK_ID     0x0C

#define BQ25180_DEVICE_ID          0x00
#define BQ25180_STAT0_VIN_PGOOD    0x01
#define BQ25180_ICHG_CHG_DIS       0x80
#define BQ25180_IC_CTRL_TS_AUTO_EN 0x80
#define BQ25180_IC_CTRL_SAFETY_6H  0x04
#define BQ25180_IC_CTRL_WDOG_DIS   0x03

typedef enum {
    CHG_NOT_CHARGING = 0,
    CHG_CONSTANT_CURRENT = 1,
    CHG_CONSTANT_VOLTAGE = 2,
    CHG_DONE = 3,
} chg_state_t;

typedef struct {
    bool vin_good;
    chg_state_t state;
} bq25180_status_t;

uint8_t bq25180_ichg_code(uint16_t ma);
uint8_t bq25180_vbat_code(uint16_t mv);
int bq25180_init(uint16_t ichg_ma, uint16_t vreg_mv);
/* Reads STAT0 and FLAG0 (reading FLAG0 clears the interrupt flags). */
int bq25180_read_status(bq25180_status_t *st);

#endif
