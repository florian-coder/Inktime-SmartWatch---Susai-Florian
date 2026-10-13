#include "bq25180.h"

#include "board.h"
#include "core/log.h"
#include "drivers/i2c_reg.h"

#define ADDR I2C_ADDR_BQ25180

uint8_t bq25180_ichg_code(uint16_t ma)
{
    if (ma < 5) ma = 5;
    if (ma > 1000) ma = 1000;
    if (ma <= 35) {
        return (uint8_t)(ma - 5);
    }
    return (uint8_t)((ma - 40) / 10 + 31);
}

uint8_t bq25180_vbat_code(uint16_t mv)
{
    if (mv < 3500) mv = 3500;
    if (mv > 4650) mv = 4650;
    return (uint8_t)((mv - 3500) / 10);
}

int bq25180_init(uint16_t ichg_ma, uint16_t vreg_mv)
{
    uint8_t id = 0xFF;
    int rc = i2c_reg_read8(ADDR, BQ25180_REG_MASK_ID, &id);
    if (rc != 0 || (id & 0x0F) != BQ25180_DEVICE_ID) {
        log_printf("bq25180: not found (rc %d id 0x%02x)", rc, id);
        return -1;
    }
    /* No I2C watchdog: otherwise the charger falls back to defaults after 160 s. */
    rc = i2c_reg_write8(ADDR, BQ25180_REG_IC_CTRL,
                        BQ25180_IC_CTRL_TS_AUTO_EN | BQ25180_IC_CTRL_SAFETY_6H | BQ25180_IC_CTRL_WDOG_DIS);
    if (rc == 0) rc = i2c_reg_update8(ADDR, BQ25180_REG_VBAT_CTRL, 0x7F, bq25180_vbat_code(vreg_mv));
    if (rc == 0) rc = i2c_reg_write8(ADDR, BQ25180_REG_ICHG_CTRL, bq25180_ichg_code(ichg_ma)); /* CHG_DIS = 0 */
    /* Interrupt on charge-status changes only (mask ILIM and VINDPM). */
    if (rc == 0) rc = i2c_reg_update8(ADDR, BQ25180_REG_CHARGECTRL1, 0x07, 0x03);
    uint8_t flags;
    if (rc == 0) rc = i2c_reg_read8(ADDR, BQ25180_REG_FLAG0, &flags);
    return rc;
}

int bq25180_read_status(bq25180_status_t *st)
{
    uint8_t stat0, flags;
    int rc = i2c_reg_read8(ADDR, BQ25180_REG_STAT0, &stat0);
    if (rc == 0) rc = i2c_reg_read8(ADDR, BQ25180_REG_FLAG0, &flags);
    if (rc == 0) {
        st->vin_good = (stat0 & BQ25180_STAT0_VIN_PGOOD) != 0;
        st->state = (chg_state_t)((stat0 >> 5) & 0x03);
    }
    return rc;
}
