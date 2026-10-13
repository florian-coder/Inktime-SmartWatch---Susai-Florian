/*
 * SSD1681 driver. Init sequence follows GxEPD2_154_D67 (Waveshare 1.54" V2 / GDEH0154D67).
 *
 * Partial refreshes use the controller's differential mode: RAM 0x26 must hold the
 * image currently on the glass and RAM 0x24 the new one. The controller is put into
 * deep sleep after every refresh, so each update starts with a hardware reset.
 */
#include "epd_ssd1681.h"

#include <string.h>

#include "board.h"
#include "core/log.h"
#include "hal/hal.h"

#define EPD_RESET_TIMEOUT_MS 200

static uint8_t s_prev[EPD_BYTES]; /* frame currently on the glass (gfx format) */
static bool s_have_prev;
/* EasyDMA can only read RAM, so every SPI transfer goes through this buffer. */
static uint8_t s_dma[128];

static void spi_cmd(uint8_t cmd)
{
    hal_gpio_write(PIN_EPD_DC, false);
    hal_gpio_write(PIN_EPD_CS, false);
    s_dma[0] = cmd;
    hal_spi_write(s_dma, 1);
    hal_gpio_write(PIN_EPD_CS, true);
}

static void spi_data(const uint8_t *data, size_t len)
{
    hal_gpio_write(PIN_EPD_DC, true);
    hal_gpio_write(PIN_EPD_CS, false);
    while (len > 0) {
        size_t n = len < sizeof(s_dma) ? len : sizeof(s_dma);
        memcpy(s_dma, data, n);
        hal_spi_write(s_dma, n);
        data += n;
        len -= n;
    }
    hal_gpio_write(PIN_EPD_CS, true);
}

static void cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    spi_cmd(cmd);
    spi_data(data, len);
}

/* Stream a frame into a RAM bank, inverting it: the panel uses 0 = black. */
static void write_ram(uint8_t ram_cmd, const uint8_t *fb)
{
    static const uint8_t x0[] = { 0x00 };
    static const uint8_t y0[] = { 0x00, 0x00 };
    cmd_data(EPD_CMD_RAM_X_COUNTER, x0, sizeof(x0));
    cmd_data(EPD_CMD_RAM_Y_COUNTER, y0, sizeof(y0));
    spi_cmd(ram_cmd);

    hal_gpio_write(PIN_EPD_DC, true);
    hal_gpio_write(PIN_EPD_CS, false);
    for (size_t off = 0; off < EPD_BYTES; off += sizeof(s_dma)) {
        size_t n = EPD_BYTES - off < sizeof(s_dma) ? EPD_BYTES - off : sizeof(s_dma);
        for (size_t i = 0; i < n; i++) {
            s_dma[i] = (uint8_t)~fb[off + i];
        }
        hal_spi_write(s_dma, n);
    }
    hal_gpio_write(PIN_EPD_CS, true);
}

static int wait_idle(uint32_t timeout_ms)
{
    uint64_t start = hal_millis();
    while (hal_gpio_read(PIN_EPD_BUSY)) {
        if (hal_millis() - start > timeout_ms) {
            return -1;
        }
        hal_delay_ms(1);
    }
    return 0;
}

int epd_init(void)
{
    hal_gpio_output(PIN_EPD_PWR_N, false); /* power the panel */
    hal_gpio_output(PIN_EPD_CS, true);
    hal_gpio_output(PIN_EPD_DC, true);
    hal_gpio_output(PIN_EPD_RST, true);
    hal_gpio_input(PIN_EPD_BUSY, HAL_PULL_NONE);
    hal_spi_init(PIN_EPD_SCK, PIN_EPD_MOSI);
    s_have_prev = false;
    hal_delay_ms(10);
    return 0;
}

int epd_update(const uint8_t *fb, bool full)
{
    static const uint8_t driver_output[] = { (EPD_HEIGHT - 1) & 0xFF, (EPD_HEIGHT - 1) >> 8, 0x00 };
    static const uint8_t entry_mode[] = { 0x03 }; /* X and Y increment */
    static const uint8_t x_range[] = { 0x00, EPD_WIDTH / 8 - 1 };
    static const uint8_t y_range[] = { 0x00, 0x00, (EPD_HEIGHT - 1) & 0xFF, (EPD_HEIGHT - 1) >> 8 };
    static const uint8_t border[] = { 0x05 };
    static const uint8_t temp_internal[] = { 0x80 };

    if (!s_have_prev) {
        full = true;
    }

    /* Hardware reset wakes the controller from deep sleep. */
    hal_gpio_write(PIN_EPD_RST, false);
    hal_delay_ms(10);
    hal_gpio_write(PIN_EPD_RST, true);
    hal_delay_ms(10);
    if (wait_idle(EPD_RESET_TIMEOUT_MS) != 0) {
        log_printf("epd: busy stuck after reset");
        return -1;
    }
    spi_cmd(EPD_CMD_SW_RESET);
    hal_delay_ms(10);
    if (wait_idle(EPD_RESET_TIMEOUT_MS) != 0) {
        log_printf("epd: busy stuck after sw reset");
        return -1;
    }

    cmd_data(EPD_CMD_DRIVER_OUTPUT, driver_output, sizeof(driver_output));
    cmd_data(EPD_CMD_DATA_ENTRY, entry_mode, sizeof(entry_mode));
    cmd_data(EPD_CMD_RAM_X_RANGE, x_range, sizeof(x_range));
    cmd_data(EPD_CMD_RAM_Y_RANGE, y_range, sizeof(y_range));
    cmd_data(EPD_CMD_BORDER, border, sizeof(border));
    cmd_data(EPD_CMD_TEMP_SENSOR, temp_internal, sizeof(temp_internal));

    write_ram(EPD_CMD_WRITE_RAM_BW, fb);
    write_ram(EPD_CMD_WRITE_RAM_RED, full ? fb : s_prev);

    const uint8_t mode = full ? EPD_UPDATE_FULL : EPD_UPDATE_PARTIAL;
    cmd_data(EPD_CMD_UPDATE_CTRL2, &mode, 1);
    spi_cmd(EPD_CMD_MASTER_ACT);

    memcpy(s_prev, fb, EPD_BYTES);
    s_have_prev = true;
    return 0;
}

bool epd_busy(void)
{
    return hal_gpio_read(PIN_EPD_BUSY);
}

void epd_sleep(void)
{
    static const uint8_t mode1[] = { 0x01 };
    cmd_data(EPD_CMD_DEEP_SLEEP, mode1, sizeof(mode1));
}

void epd_power_off(void)
{
    hal_gpio_write(PIN_EPD_PWR_N, true);
    s_have_prev = false;
}
