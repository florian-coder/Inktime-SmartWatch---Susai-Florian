#include "max17048.h"

#include "board.h"
#include "core/log.h"
#include "drivers/i2c_reg.h"

#define ADDR I2C_ADDR_MAX17048

int max17048_init(uint8_t alert_pct)
{
    uint16_t version = 0;
    int rc = i2c_reg_read16be(ADDR, MAX17048_REG_VERSION, &version);
    if (rc != 0 || (version & 0xFFF0) != 0x0010) {
        log_printf("max17048: not found (rc %d version 0x%04x)", rc, version);
        return -1;
    }

    uint16_t status;
    rc = i2c_reg_read16be(ADDR, MAX17048_REG_STATUS, &status);
    if (rc == 0 && (status & MAX17048_STATUS_RI)) {
        rc = i2c_reg_write16be(ADDR, MAX17048_REG_STATUS, status & (uint16_t)~MAX17048_STATUS_RI);
    }

    if (alert_pct < 1) alert_pct = 1;
    if (alert_pct > 32) alert_pct = 32;
    uint16_t config;
    if (rc == 0) rc = i2c_reg_read16be(ADDR, MAX17048_REG_CONFIG, &config);
    if (rc == 0) {
        config &= (uint16_t)~(MAX17048_CONFIG_ATHD_MSK | MAX17048_CONFIG_ALRT);
        config |= (uint16_t)(32 - alert_pct);
        rc = i2c_reg_write16be(ADDR, MAX17048_REG_CONFIG, config);
    }
    return rc;
}

int max17048_read(max17048_reading_t *r)
{
    uint16_t vcell, soc, crate;
    int rc = i2c_reg_read16be(ADDR, MAX17048_REG_VCELL, &vcell);
    if (rc == 0) rc = i2c_reg_read16be(ADDR, MAX17048_REG_SOC, &soc);
    if (rc == 0) rc = i2c_reg_read16be(ADDR, MAX17048_REG_CRATE, &crate);
    if (rc != 0) {
        return rc;
    }
    r->mv = (uint16_t)((uint32_t)vcell * 5 / 64); /* 78.125 uV/LSB */
    uint32_t pct = ((uint32_t)soc + 128) >> 8;    /* 1/256 % per LSB */
    r->pct = (uint8_t)(pct > 100 ? 100 : pct);
    r->crate_x10 = (int16_t)((int32_t)(int16_t)crate * 208 / 100); /* 0.208 %/h per LSB */
    return 0;
}

int max17048_clear_alert(uint16_t *status)
{
    uint16_t st, config;
    int rc = i2c_reg_read16be(ADDR, MAX17048_REG_STATUS, &st);
    if (rc == 0) rc = i2c_reg_write16be(ADDR, MAX17048_REG_STATUS, st & (uint16_t)~MAX17048_STATUS_FLAGS);
    if (rc == 0) rc = i2c_reg_read16be(ADDR, MAX17048_REG_CONFIG, &config);
    if (rc == 0) rc = i2c_reg_write16be(ADDR, MAX17048_REG_CONFIG, config & (uint16_t)~MAX17048_CONFIG_ALRT);
    if (rc == 0 && status) {
        *status = st;
    }
    return rc;
}
