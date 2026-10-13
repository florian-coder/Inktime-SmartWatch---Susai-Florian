/* Bosch BMA423 accelerometer with on-chip step counter. */
#ifndef BMA423_H
#define BMA423_H

#include <stdint.h>

#define BMA423_REG_CHIP_ID         0x00
#define BMA423_REG_DATA_X_LSB      0x12
#define BMA423_REG_INT_STATUS_0    0x1C
#define BMA423_REG_STEP_COUNTER_0  0x1E
#define BMA423_REG_INTERNAL_STATUS 0x2A
#define BMA423_REG_ACC_CONF        0x40
#define BMA423_REG_ACC_RANGE       0x41
#define BMA423_REG_INT1_IO_CTRL    0x53
#define BMA423_REG_INT_LATCH       0x55
#define BMA423_REG_INT1_MAP        0x56
#define BMA423_REG_INIT_CTRL       0x59
#define BMA423_REG_ASIC_LSB        0x5B /* undocumented feature-memory address */
#define BMA423_REG_ASIC_MSB        0x5C
#define BMA423_REG_FEATURE_CONFIG  0x5E
#define BMA423_REG_PWR_CONF        0x7C
#define BMA423_REG_PWR_CTRL        0x7D
#define BMA423_REG_CMD             0x7E

#define BMA423_CHIP_ID          0x13
#define BMA423_CMD_SOFT_RESET   0xB6
#define BMA423_FEATURE_SIZE     70
#define BMA423_STEP_CNTR_OFFSET 0x3A /* watermark (10 bit) + enable bit in the next byte */
#define BMA423_STEP_CNTR_EN     0x10
#define BMA423_INT_STEP_COUNTER 0x02
#define BMA423_CONFIG_CHUNK     32

/* Steps between watermark interrupts = BMA423_STEP_WATERMARK * 20. */
#define BMA423_STEP_WATERMARK 1

int bma423_init(void);
int bma423_read_steps(uint32_t *steps);
/* Reads (and so clears) the latched feature interrupt status. */
int bma423_read_int_status(uint8_t *status);
/* Acceleration in milli-g, +-2g range. */
int bma423_read_accel(int16_t xyz_mg[3]);

#endif
