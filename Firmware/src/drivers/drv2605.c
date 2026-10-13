#include "drv2605.h"

#include "board.h"
#include "core/log.h"
#include "drivers/i2c_reg.h"
#include "hal/hal.h"

#define ADDR I2C_ADDR_DRV2605

/* FIT0774 is a 3 V ERM; in open loop the output is set by OD_CLAMP (~21.6 mV/LSB). */
#define MOTOR_OD_CLAMP 0x8C

int drv2605_init(void)
{
    /* EN must be high for the I2C interface to respond. It stays high; idle
     * consumption is kept low with the STANDBY bit instead. */
    hal_gpio_output(PIN_HAPTIC_EN, true);
    hal_delay_ms(1);

    uint8_t status = 0;
    int rc = i2c_reg_read8(ADDR, DRV2605_REG_STATUS, &status);
    uint8_t id = status >> 5;
    if (rc != 0 || (id != 3 && id != 7)) {
        log_printf("drv2605: not found (rc %d status 0x%02x)", rc, status);
        return -1;
    }

    rc = i2c_reg_write8(ADDR, DRV2605_REG_MODE, DRV2605_MODE_RESET);
    for (int tries = 0; rc == 0 && tries < 10; tries++) {
        uint8_t mode;
        hal_delay_ms(2);
        rc = i2c_reg_read8(ADDR, DRV2605_REG_MODE, &mode);
        if (rc == 0 && !(mode & DRV2605_MODE_RESET)) {
            break;
        }
    }
    if (rc == 0) rc = i2c_reg_write8(ADDR, DRV2605_REG_MODE, DRV2605_MODE_INT_TRIG);
    if (rc == 0) rc = i2c_reg_update8(ADDR, DRV2605_REG_FEEDBACK, DRV2605_FEEDBACK_LRA, 0); /* ERM */
    if (rc == 0) rc = i2c_reg_update8(ADDR, DRV2605_REG_CONTROL3, DRV2605_CTRL3_ERM_OPEN, DRV2605_CTRL3_ERM_OPEN);
    if (rc == 0) rc = i2c_reg_write8(ADDR, DRV2605_REG_LIBRARY, DRV2605_LIB_ERM_A);
    if (rc == 0) rc = i2c_reg_write8(ADDR, DRV2605_REG_RATED_V, MOTOR_OD_CLAMP);
    if (rc == 0) rc = i2c_reg_write8(ADDR, DRV2605_REG_OD_CLAMP, MOTOR_OD_CLAMP);
    if (rc == 0) rc = drv2605_standby();
    return rc;
}

int drv2605_play(uint8_t effect)
{
    const uint8_t seq[2] = { effect, 0 }; /* one effect, then end of sequence */
    int rc = i2c_reg_write8(ADDR, DRV2605_REG_MODE, DRV2605_MODE_INT_TRIG);
    if (rc == 0) rc = i2c_reg_write(ADDR, DRV2605_REG_WAVESEQ1, seq, sizeof(seq));
    if (rc == 0) rc = i2c_reg_write8(ADDR, DRV2605_REG_GO, 0x01);
    return rc;
}

int drv2605_is_playing(bool *playing)
{
    uint8_t go;
    int rc = i2c_reg_read8(ADDR, DRV2605_REG_GO, &go);
    if (rc == 0) {
        *playing = (go & 0x01) != 0;
    }
    return rc;
}

int drv2605_standby(void)
{
    return i2c_reg_write8(ADDR, DRV2605_REG_MODE, DRV2605_MODE_STANDBY);
}
