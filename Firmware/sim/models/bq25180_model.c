/*
 * BQ25180 model: status registers, /INT pulse on charge-state changes and the
 * 160 s I2C watchdog that silently restores defaults if firmware forgets to disable it.
 */
#include <string.h>

#include "board.h"
#include "hal/host/hal_host.h"
#include "models.h"

static const uint8_t defaults[13] = {
    0x00, 0x00, 0x00,             /* STAT0 STAT1 FLAG0 */
    0x46,                         /* VBAT_CTRL: 4.2 V */
    0x05,                         /* ICHG_CTRL: 10 mA */
    0x2C, 0x56,                   /* CHARGECTRL0/1 */
    0x84,                         /* IC_CTRL: watchdog 160 s */
    0x4D, 0x11, 0x40, 0x00, 0xC0, /* MASK_ID: device id 0 */
};

static struct {
    uint8_t regs[13];
    uint64_t last_access;
    uint64_t int_release_at;
    uint32_t wd_resets;
    uint32_t errors;
} m;

void chgm_reset(void)
{
    memset(&m, 0, sizeof(m));
    memcpy(m.regs, defaults, sizeof(defaults));
    host_pin_release(PIN_PMIC_INT);
}

static bool watchdog_enabled(void)
{
    return (m.regs[0x07] & 0x03) != 0x03;
}

int chgm_i2c(const uint8_t *w, size_t wl, uint8_t *r, size_t rl, uint64_t now)
{
    if (wl == 0) {
        return -1;
    }
    m.last_access = now;
    uint8_t reg = w[0];
    for (size_t i = 1; i < wl; i++, reg++) {
        if (reg >= sizeof(m.regs)) {
            m.errors++;
            continue;
        }
        if (reg <= 0x02 || reg == 0x0C) {
            continue; /* status / flags / id are read-only */
        }
        m.regs[reg] = w[i];
    }
    for (size_t i = 0; i < rl; i++, reg++) {
        if (reg >= sizeof(m.regs)) {
            r[i] = 0;
            m.errors++;
            continue;
        }
        r[i] = m.regs[reg];
        if (reg == 0x02) {
            m.regs[0x02] = 0; /* FLAG0 clears on read */
        }
    }
    return 0;
}

void chgm_set(bool vin, uint8_t state, uint64_t now)
{
    uint8_t stat0 = (uint8_t)(((state & 3) << 5) | (vin ? 1 : 0));
    if (stat0 == m.regs[0x00]) {
        return;
    }
    m.regs[0x00] = stat0;
    m.regs[0x02] |= 0x08; /* charge status flag */
    if (!(m.regs[0x06] & 0x04)) {
        /* /INT is a short active-low pulse (128 us); 1 ms in the model. */
        host_pin_drive(PIN_PMIC_INT, false);
        m.int_release_at = now + 1;
    }
    if (!vin) {
        m.last_access = now;
    }
}

void chgm_advance(uint64_t now)
{
    if (m.int_release_at && now >= m.int_release_at) {
        m.int_release_at = 0;
        host_pin_release(PIN_PMIC_INT);
    }
    if ((m.regs[0x00] & 0x01) && watchdog_enabled() && now - m.last_access >= CHGM_WATCHDOG_MS) {
        uint8_t stat0 = m.regs[0x00];
        memcpy(m.regs, defaults, sizeof(defaults));
        m.regs[0x00] = stat0;
        m.wd_resets++;
        m.last_access = now;
    }
}

uint64_t chgm_next_event(void)
{
    return m.int_release_at;
}

uint16_t chgm_ichg_ma(void)
{
    uint8_t code = m.regs[0x04] & 0x7F;
    return code <= 31 ? (uint16_t)(code + 5) : (uint16_t)(40 + (code - 31) * 10);
}

uint16_t chgm_vreg_mv(void)
{
    return (uint16_t)(3500 + (m.regs[0x03] & 0x7F) * 10);
}

uint32_t chgm_watchdog_resets(void)
{
    return m.wd_resets;
}

uint32_t chgm_errors(void)
{
    return m.errors;
}
