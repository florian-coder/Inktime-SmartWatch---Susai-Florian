/* DRV2605 model: I2C only answers with EN high; GO plays WAVESEQ1 for a per-effect duration. */
#include <string.h>

#include "board.h"
#include "hal/host/hal_host.h"
#include "models.h"

static struct {
    uint8_t regs[0x23];
    uint64_t go_until;
    uint64_t reset_until;
    uint32_t plays;
    uint8_t last_effect;
    uint32_t errors;
} m;

static void defaults(void)
{
    memset(m.regs, 0, sizeof(m.regs));
    m.regs[0x00] = 0x60; /* STATUS: DEVICE_ID = 3 (DRV2605) */
    m.regs[0x01] = 0x40; /* MODE: standby */
    m.regs[0x03] = 0x01; /* LIBRARY_SEL */
    m.regs[0x04] = 0x01; /* WAVESEQ1 */
    m.regs[0x16] = 0x3E;
    m.regs[0x17] = 0x8C;
    m.regs[0x1A] = 0x36; /* FEEDBACK: ERM */
    m.regs[0x1D] = 0xA0; /* CONTROL3: ERM open loop */
}

void hapm_reset(void)
{
    memset(&m, 0, sizeof(m));
    defaults();
}

static uint32_t effect_ms(uint8_t effect)
{
    switch (effect) {
    case 1: return 60;
    case 10: return 150;
    case 14: return 500;
    default: return 100;
    }
}

static void write_reg(uint8_t reg, uint8_t v, uint64_t now)
{
    if (reg == 0x00 || reg >= sizeof(m.regs)) {
        return;
    }
    if (reg == 0x01 && (v & 0x80)) {
        defaults();
        m.regs[0x01] = 0x80 | 0x40;
        m.reset_until = now + 1;
        return;
    }
    m.regs[reg] = v;
    if (reg == 0x0C && (v & 1)) {
        if (m.regs[0x01] & 0x40) {
            m.errors++; /* GO while in standby does nothing on the real part */
            m.regs[0x0C] = 0;
            return;
        }
        uint8_t effect = m.regs[0x04];
        m.plays++;
        m.last_effect = effect;
        m.go_until = now + effect_ms(effect);
    }
}

int hapm_i2c(const uint8_t *w, size_t wl, uint8_t *r, size_t rl, uint64_t now)
{
    if (!host_pin_level(PIN_HAPTIC_EN)) {
        return -1; /* in shutdown, the I2C interface is off */
    }
    if (wl == 0) {
        return -1;
    }
    hapm_advance(now);
    uint8_t reg = w[0];
    for (size_t i = 1; i < wl; i++) {
        write_reg(reg++, w[i], now);
    }
    for (size_t i = 0; i < rl; i++, reg++) {
        r[i] = reg < sizeof(m.regs) ? m.regs[reg] : 0;
    }
    return 0;
}

void hapm_advance(uint64_t now)
{
    if (m.go_until && now >= m.go_until) {
        m.go_until = 0;
        m.regs[0x0C] = 0;
    }
    if (m.reset_until && now >= m.reset_until) {
        m.reset_until = 0;
        m.regs[0x01] &= (uint8_t)~0x80;
    }
}

uint64_t hapm_next_event(void)
{
    uint64_t a = m.go_until, b = m.reset_until;
    if (!a) return b;
    if (!b) return a;
    return a < b ? a : b;
}

uint32_t hapm_play_count(void)
{
    return m.plays;
}

uint8_t hapm_last_effect(void)
{
    return m.last_effect;
}

bool hapm_configured_erm_open_loop(void)
{
    return !(m.regs[0x1A] & 0x80) && (m.regs[0x1D] & 0x20) && m.regs[0x03] == 0x01;
}

bool hapm_in_standby(void)
{
    return (m.regs[0x01] & 0x40) != 0;
}

uint32_t hapm_errors(void)
{
    return m.errors;
}
