/*
 * BMA423 bring-up: the step counter runs on an internal feature engine whose
 * firmware (6 KiB from Bosch) has to be streamed in at every power-up.
 * Sequence follows the Bosch Sensor API (bma4_write_config_file / bma423_feature_enable).
 */
#include "bma423.h"

#include <stdbool.h>

#include "bma423_config.h"
#include "board.h"
#include "core/log.h"
#include "drivers/i2c_reg.h"
#include "hal/hal.h"

#define ADDR I2C_ADDR_BMA423

static uint8_t s_feat_lsb;
static uint8_t s_feat_msb;

static int set_asic_addr(uint16_t word_addr)
{
    int rc = i2c_reg_write8(ADDR, BMA423_REG_ASIC_LSB, (uint8_t)(word_addr & 0x0F));
    if (rc == 0) {
        rc = i2c_reg_write8(ADDR, BMA423_REG_ASIC_MSB, (uint8_t)(word_addr >> 4));
    }
    return rc;
}

static int upload_config(void)
{
    int rc = i2c_reg_write8(ADDR, BMA423_REG_INIT_CTRL, 0x00);
    for (uint16_t idx = 0; rc == 0 && idx < BMA423_CONFIG_SIZE; idx += BMA423_CONFIG_CHUNK) {
        rc = set_asic_addr(idx / 2);
        if (rc == 0) {
            rc = i2c_reg_write(ADDR, BMA423_REG_FEATURE_CONFIG, &bma423_config_file[idx], BMA423_CONFIG_CHUNK);
        }
    }
    if (rc != 0) {
        return rc;
    }
    rc = i2c_reg_write8(ADDR, BMA423_REG_INIT_CTRL, 0x01);
    if (rc != 0) {
        return rc;
    }
    hal_delay_ms(150); /* ASIC initialisation */

    uint8_t status = 0;
    rc = i2c_reg_read8(ADDR, BMA423_REG_INTERNAL_STATUS, &status);
    if (rc != 0) {
        return rc;
    }
    if ((status & 0x0F) != 0x01) {
        log_printf("bma423: feature engine init failed (status 0x%02x)", status);
        return -2;
    }
    /* The engine leaves the feature-config start address in 0x5B/0x5C. */
    rc = i2c_reg_read8(ADDR, BMA423_REG_ASIC_LSB, &s_feat_lsb);
    if (rc == 0) {
        rc = i2c_reg_read8(ADDR, BMA423_REG_ASIC_MSB, &s_feat_msb);
    }
    return rc;
}

static int restore_feature_addr(void)
{
    int rc = i2c_reg_write8(ADDR, BMA423_REG_ASIC_LSB, s_feat_lsb);
    if (rc == 0) {
        rc = i2c_reg_write8(ADDR, BMA423_REG_ASIC_MSB, s_feat_msb);
    }
    return rc;
}

/* The watermark is 10 bits spread over two bytes; the enable bit shares the second one. */
static void set_step_feature(uint8_t *feat, uint16_t watermark)
{
    feat[BMA423_STEP_CNTR_OFFSET] = (uint8_t)(watermark & 0xFF);
    uint8_t hi = (uint8_t)(feat[BMA423_STEP_CNTR_OFFSET + 1] & ~0x03);
    hi |= (uint8_t)((watermark >> 8) & 0x03);
    feat[BMA423_STEP_CNTR_OFFSET + 1] = (uint8_t)(hi | BMA423_STEP_CNTR_EN);
}

static int enable_step_counter(void)
{
    uint8_t feat[BMA423_FEATURE_SIZE];
    int rc = restore_feature_addr();
    if (rc == 0) {
        rc = i2c_reg_read(ADDR, BMA423_REG_FEATURE_CONFIG, feat, sizeof(feat));
    }
    if (rc != 0) {
        return rc;
    }
    set_step_feature(feat, BMA423_STEP_WATERMARK);
    rc = restore_feature_addr();
    if (rc == 0) {
        rc = i2c_reg_write(ADDR, BMA423_REG_FEATURE_CONFIG, feat, sizeof(feat));
    }
    return rc;
}

int bma423_init(void)
{
    uint8_t id = 0;

    /* The chip may not ACK the reset command itself, so ignore the result. */
    (void)i2c_reg_write8(ADDR, BMA423_REG_CMD, BMA423_CMD_SOFT_RESET);
    hal_delay_ms(10);

    int rc = i2c_reg_read8(ADDR, BMA423_REG_CHIP_ID, &id);
    if (rc != 0 || id != BMA423_CHIP_ID) {
        log_printf("bma423: not found (rc %d id 0x%02x)", rc, id);
        return -1;
    }

    /* Advanced power save must be off while the feature memory is written. */
    rc = i2c_reg_write8(ADDR, BMA423_REG_PWR_CONF, 0x00);
    hal_delay_ms(1);
    if (rc == 0) rc = upload_config();
    if (rc == 0) rc = i2c_reg_write8(ADDR, BMA423_REG_ACC_CONF, 0x17);  /* 50 Hz, avg2 */
    if (rc == 0) rc = i2c_reg_write8(ADDR, BMA423_REG_ACC_RANGE, 0x00); /* +-2 g */
    if (rc == 0) rc = i2c_reg_write8(ADDR, BMA423_REG_PWR_CTRL, 0x04);  /* acc_en */
    if (rc == 0) rc = enable_step_counter();
    if (rc == 0) rc = i2c_reg_write8(ADDR, BMA423_REG_INT1_IO_CTRL, 0x0A); /* out, push-pull, active high */
    if (rc == 0) rc = i2c_reg_write8(ADDR, BMA423_REG_INT_LATCH, 0x01);
    if (rc == 0) rc = i2c_reg_write8(ADDR, BMA423_REG_INT1_MAP, BMA423_INT_STEP_COUNTER);
    if (rc == 0) rc = i2c_reg_write8(ADDR, BMA423_REG_PWR_CONF, 0x03); /* APS on */
    if (rc != 0) {
        log_printf("bma423: init failed (%d)", rc);
    }
    return rc;
}

int bma423_read_steps(uint32_t *steps)
{
    uint8_t b[4];
    int rc = i2c_reg_read(ADDR, BMA423_REG_STEP_COUNTER_0, b, sizeof(b));
    if (rc == 0) {
        *steps = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    }
    return rc;
}

int bma423_read_int_status(uint8_t *status)
{
    return i2c_reg_read8(ADDR, BMA423_REG_INT_STATUS_0, status);
}

int bma423_read_accel(int16_t xyz_mg[3])
{
    uint8_t b[6];
    int rc = i2c_reg_read(ADDR, BMA423_REG_DATA_X_LSB, b, sizeof(b));
    if (rc != 0) {
        return rc;
    }
    for (int i = 0; i < 3; i++) {
        /* 12-bit left-justified two's complement, 1024 LSB/g at +-2 g. */
        int16_t raw = (int16_t)((uint16_t)b[2 * i + 1] << 8 | b[2 * i]) >> 4;
        xyz_mg[i] = (int16_t)((int32_t)raw * 1000 / 1024);
    }
    return 0;
}
