/* Waveshare 1.54" V2 e-paper (SSD1681 controller, 200x200, black/white). */
#ifndef EPD_SSD1681_H
#define EPD_SSD1681_H

#include <stdbool.h>
#include <stdint.h>

#define EPD_WIDTH  200
#define EPD_HEIGHT 200
#define EPD_BYTES  (EPD_WIDTH / 8 * EPD_HEIGHT)

/* SSD1681 commands used by the driver (subset of the datasheet). */
#define EPD_CMD_DRIVER_OUTPUT 0x01
#define EPD_CMD_DEEP_SLEEP    0x10
#define EPD_CMD_DATA_ENTRY    0x11
#define EPD_CMD_SW_RESET      0x12
#define EPD_CMD_TEMP_SENSOR   0x18
#define EPD_CMD_MASTER_ACT    0x20
#define EPD_CMD_UPDATE_CTRL2  0x22
#define EPD_CMD_WRITE_RAM_BW  0x24
#define EPD_CMD_WRITE_RAM_RED 0x26
#define EPD_CMD_BORDER        0x3C
#define EPD_CMD_RAM_X_RANGE   0x44
#define EPD_CMD_RAM_Y_RANGE   0x45
#define EPD_CMD_RAM_X_COUNTER 0x4E
#define EPD_CMD_RAM_Y_COUNTER 0x4F

#define EPD_UPDATE_FULL    0xF7 /* clock+analog on, load temp+LUT, mode 1, power off */
#define EPD_UPDATE_PARTIAL 0xFF /* same, display mode 2 (differential) */

/* Configure pins and SPI, power the panel. The controller stays asleep until the first update. */
int epd_init(void);
/* Wake the controller, upload the frame and start a refresh. Non-blocking: poll epd_busy().
 * fb uses the gfx format (1 = black). A partial refresh needs a previous frame. */
int epd_update(const uint8_t *fb, bool full);
bool epd_busy(void);
/* Put the controller into deep sleep mode 1 (RAM retained). Call after BUSY went low. */
void epd_sleep(void);
/* Cut panel power (before System OFF). */
void epd_power_off(void);

#endif
