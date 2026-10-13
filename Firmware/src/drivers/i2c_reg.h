/* Register access helpers for the 8-bit-register I2C devices on the shared bus. */
#ifndef I2C_REG_H
#define I2C_REG_H

#include <stddef.h>
#include <stdint.h>

#define I2C_REG_MAX_WRITE 72 /* largest burst: BMA423 feature config (70 bytes) */

int i2c_reg_read(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
int i2c_reg_write(uint8_t addr, uint8_t reg, const uint8_t *buf, size_t n);
int i2c_reg_read8(uint8_t addr, uint8_t reg, uint8_t *val);
int i2c_reg_write8(uint8_t addr, uint8_t reg, uint8_t val);
int i2c_reg_update8(uint8_t addr, uint8_t reg, uint8_t mask, uint8_t val);
/* Big-endian 16-bit registers (MAX17048). */
int i2c_reg_read16be(uint8_t addr, uint8_t reg, uint16_t *val);
int i2c_reg_write16be(uint8_t addr, uint8_t reg, uint16_t val);

#endif
