/*
 * SSD1681 + 200x200 panel model.
 *
 * Decodes the SPI command stream using the DC and CS pins, keeps both RAM banks,
 * and on master activation transfers RAM to the "glass":
 *   full refresh  -> glass = BW RAM
 *   partial (mode 2, differential) -> only pixels where BW != RED are driven,
 *     so a driver that loads the wrong "previous" image leaves visible garbage.
 * BUSY is high while resetting, refreshing and in deep sleep, as on the real chip.
 */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "hal/host/hal_host.h"
#include "models.h"

#define W_BYTES 25
#define H       200

static struct {
    bool powered;
    bool in_reset;
    bool sleeping;
    uint64_t busy_until;
    uint8_t cmd;
    int data_idx;
    uint8_t ctrl2;
    uint16_t mux;
    uint8_t entry;
    uint8_t x_start, x_end, x_cnt;
    uint16_t y_start, y_end, y_cnt;
    uint8_t bw[EPDM_BYTES];
    uint8_t red[EPDM_BYTES];
    uint8_t glass[EPDM_BYTES];
    epdm_stats_t st;
} m;

static void error(const char *msg)
{
    m.st.errors++;
    snprintf(m.st.last_error, sizeof(m.st.last_error), "%s", msg);
}

static void reset_registers(void)
{
    m.cmd = 0;
    m.data_idx = 0;
    m.ctrl2 = 0;
    m.mux = 0;
    m.entry = 0x03;
    m.x_start = 0;
    m.x_end = W_BYTES - 1;
    m.y_start = 0;
    m.y_end = H - 1;
    m.x_cnt = 0;
    m.y_cnt = 0;
}

static void drive_busy(uint64_t now)
{
    if (!m.powered) {
        host_pin_release(PIN_EPD_BUSY);
        return;
    }
    bool busy = m.sleeping || m.in_reset || now < m.busy_until;
    host_pin_drive(PIN_EPD_BUSY, busy);
}

void epdm_reset(void)
{
    memset(&m, 0, sizeof(m));
    memset(m.glass, 0xFF, sizeof(m.glass)); /* blank white panel */
    reset_registers();
    m.sleeping = true; /* controller starts unpowered/asleep */
    m.powered = false;
}

void epdm_on_output(uint8_t pin, bool level, uint64_t now)
{
    if (pin == PIN_EPD_PWR_N) {
        bool on = !level;
        if (on && !m.powered) {
            m.powered = true;
            m.sleeping = false;
            m.busy_until = now + 2; /* power-on reset */
            reset_registers();
        } else if (!on && m.powered) {
            m.powered = false;
            m.sleeping = true;
        }
    } else if (pin == PIN_EPD_RST && m.powered) {
        if (!level) {
            m.in_reset = true;
        } else if (m.in_reset) {
            m.in_reset = false;
            m.sleeping = false;
            m.busy_until = now + 2;
            m.st.hw_resets++;
            reset_registers();
        }
    }
    drive_busy(now);
}

static void advance_counter(void)
{
    if (++m.x_cnt > m.x_end) {
        m.x_cnt = m.x_start;
        if (++m.y_cnt > m.y_end) {
            m.y_cnt = m.y_start;
        }
    }
}

static void ram_write(uint8_t *ram, uint8_t b)
{
    if (m.x_cnt >= W_BYTES || m.y_cnt >= H) {
        error("RAM write outside panel");
        return;
    }
    ram[m.y_cnt * W_BYTES + m.x_cnt] = b;
    advance_counter();
}

static void master_activation(uint64_t now)
{
    if (m.ctrl2 == 0) {
        error("master activation without display update control");
        return;
    }
    if (!(m.ctrl2 & 0x04)) {
        m.busy_until = now + 5; /* power on/off only */
        return;
    }
    if (m.mux != H - 1) {
        error("driver output control not set to 200 gates");
    }
    const bool partial = (m.ctrl2 & 0x08) != 0;
    if (partial) {
        for (int i = 0; i < EPDM_BYTES; i++) {
            uint8_t changed = (uint8_t)(m.bw[i] ^ m.red[i]);
            m.glass[i] = (uint8_t)((m.glass[i] & ~changed) | (m.bw[i] & changed));
        }
        m.st.partial++;
        m.busy_until = now + EPDM_PARTIAL_MS;
    } else {
        memcpy(m.glass, m.bw, EPDM_BYTES);
        m.st.full++;
        m.busy_until = now + EPDM_FULL_MS;
    }
}

static void command(uint8_t c, uint64_t now)
{
    if (m.sleeping) {
        error("command while in deep sleep (needs HW reset)");
        return;
    }
    if (now < m.busy_until) {
        error("command while BUSY");
    }
    m.cmd = c;
    m.data_idx = 0;
    switch (c) {
    case 0x12: /* SW reset */
        reset_registers();
        m.busy_until = now + 2;
        m.st.sw_resets++;
        break;
    case 0x20: master_activation(now); break;
    default: break;
    }
}

static void data(uint8_t b, uint64_t now)
{
    if (m.sleeping) {
        error("data while in deep sleep");
        return;
    }
    if (now < m.busy_until) {
        error("data while BUSY");
    }
    int i = m.data_idx++;
    switch (m.cmd) {
    case 0x01:
        if (i == 0) m.mux = (uint16_t)((m.mux & 0x100) | b);
        if (i == 1) m.mux = (uint16_t)((m.mux & 0xFF) | ((b & 1) << 8));
        break;
    case 0x10:
        if (i == 0 && b != 0) {
            m.sleeping = true;
            m.st.sleeps++;
        }
        break;
    case 0x11: m.entry = b; break;
    case 0x22: m.ctrl2 = b; break;
    case 0x24: ram_write(m.bw, b); break;
    case 0x26: ram_write(m.red, b); break;
    case 0x44:
        if (i == 0) m.x_start = b;
        if (i == 1) m.x_end = b;
        break;
    case 0x45:
        if (i == 0) m.y_start = (uint16_t)((m.y_start & 0x100) | b);
        if (i == 1) m.y_start = (uint16_t)((m.y_start & 0xFF) | ((b & 1) << 8));
        if (i == 2) m.y_end = (uint16_t)((m.y_end & 0x100) | b);
        if (i == 3) m.y_end = (uint16_t)((m.y_end & 0xFF) | ((b & 1) << 8));
        break;
    case 0x4E:
        if (i == 0) m.x_cnt = b;
        break;
    case 0x4F:
        if (i == 0) m.y_cnt = (uint16_t)((m.y_cnt & 0x100) | b);
        if (i == 1) m.y_cnt = (uint16_t)((m.y_cnt & 0xFF) | ((b & 1) << 8));
        break;
    default: break; /* border, temperature sensor, ... */
    }
}

void epdm_on_spi(const uint8_t *buf, size_t len, uint64_t now)
{
    if (!m.powered) {
        error("SPI traffic while panel unpowered");
        return;
    }
    if (host_pin_level(PIN_EPD_CS)) {
        error("SPI traffic with CS high");
        return;
    }
    if (m.entry != 0x03 && m.cmd != 0x11) {
        error("unsupported data entry mode");
    }
    const bool is_data = host_pin_level(PIN_EPD_DC);
    for (size_t i = 0; i < len; i++) {
        if (is_data) {
            data(buf[i], now);
        } else {
            command(buf[i], now);
        }
    }
    drive_busy(now);
}

void epdm_advance(uint64_t now)
{
    drive_busy(now);
}

uint64_t epdm_next_event(void)
{
    return m.busy_until;
}

const uint8_t *epdm_panel(void)
{
    return m.glass;
}

const epdm_stats_t *epdm_stats(void)
{
    return &m.st;
}

bool epdm_sleeping(void)
{
    return m.sleeping;
}

bool epdm_refreshing(uint64_t now)
{
    return now < m.busy_until;
}
