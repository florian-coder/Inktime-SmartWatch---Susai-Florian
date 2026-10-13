#include "i2c_reg.h"

#include <string.h>

#include "hal/hal.h"

int i2c_reg_read(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n)
{
    return hal_i2c_write_read(addr, &reg, 1, buf, n);
}

int i2c_reg_write(uint8_t addr, uint8_t reg, const uint8_t *buf, size_t n)
{
    uint8_t tx[1 + I2C_REG_MAX_WRITE];
    if (n > I2C_REG_MAX_WRITE) {
        return -1;
    }
    tx[0] = reg;
    memcpy(&tx[1], buf, n);
    return hal_i2c_write(addr, tx, n + 1);
}

int i2c_reg_read8(uint8_t addr, uint8_t reg, uint8_t *val)
{
    return i2c_reg_read(addr, reg, val, 1);
}

int i2c_reg_write8(uint8_t addr, uint8_t reg, uint8_t val)
{
    return i2c_reg_write(addr, reg, &val, 1);
}

int i2c_reg_update8(uint8_t addr, uint8_t reg, uint8_t mask, uint8_t val)
{
    uint8_t cur;
    int rc = i2c_reg_read8(addr, reg, &cur);
    if (rc != 0) {
        return rc;
    }
    return i2c_reg_write8(addr, reg, (uint8_t)((cur & ~mask) | (val & mask)));
}

int i2c_reg_read16be(uint8_t addr, uint8_t reg, uint16_t *val)
{
    uint8_t b[2];
    int rc = i2c_reg_read(addr, reg, b, 2);
    if (rc == 0) {
        *val = (uint16_t)((b[0] << 8) | b[1]);
    }
    return rc;
}

int i2c_reg_write16be(uint8_t addr, uint8_t reg, uint16_t val)
{
    const uint8_t b[2] = { (uint8_t)(val >> 8), (uint8_t)val };
    return i2c_reg_write(addr, reg, b, 2);
}
