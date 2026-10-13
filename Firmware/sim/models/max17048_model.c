/* MAX17048 model: big-endian 16-bit registers mapped as a byte array; ALRT is open drain. */
#include <string.h>

#include "board.h"
#include "hal/host/hal_host.h"
#include "models.h"

static struct {
    uint8_t mem[256];
    uint32_t errors;
} m;

static uint16_t get16(uint8_t reg)
{
    return (uint16_t)(m.mem[reg] << 8 | m.mem[reg + 1]);
}

static void set16(uint8_t reg, uint16_t v)
{
    m.mem[reg] = (uint8_t)(v >> 8);
    m.mem[reg + 1] = (uint8_t)v;
}

static void update_pin(void)
{
    if (m.mem[0x0D] & 0x20) {
        host_pin_drive(PIN_FG_ALERT, false);
    } else {
        host_pin_release(PIN_FG_ALERT);
    }
}

void fgm_reset(void)
{
    memset(&m, 0, sizeof(m));
    set16(0x08, 0x0012); /* VERSION */
    set16(0x0A, 0x8030); /* HIBRT */
    set16(0x0C, 0x971C); /* CONFIG: RCOMP 0x97, ATHD 4% */
    set16(0x14, 0x00FF); /* VALRT */
    set16(0x18, 0x9600); /* VRESET */
    set16(0x1A, 0x0100); /* STATUS: RI (reset indicator) */
    fgm_set(87, 3950);
    update_pin();
}

uint8_t fgm_alert_threshold(void)
{
    return (uint8_t)(32 - (m.mem[0x0D] & 0x1F));
}

void fgm_set(uint8_t pct, uint16_t mv)
{
    uint8_t old = m.mem[0x04];
    set16(0x02, (uint16_t)((uint32_t)mv * 64 / 5));
    set16(0x04, (uint16_t)(pct << 8));
    const uint8_t thr = fgm_alert_threshold();
    if (pct < thr && old >= thr) {
        m.mem[0x1A] |= 0x10; /* STATUS.HD */
        m.mem[0x0D] |= 0x20; /* CONFIG.ALRT */
    }
    update_pin();
}

bool fgm_alert_active(void)
{
    return (m.mem[0x0D] & 0x20) != 0;
}

int fgm_i2c(const uint8_t *w, size_t wl, uint8_t *r, size_t rl)
{
    if (wl == 0) {
        return -1;
    }
    uint8_t reg = w[0];
    if ((wl > 1 || rl > 0) && (reg & 1)) {
        m.errors++; /* registers must be accessed on 16-bit boundaries */
    }
    for (size_t i = 1; i < wl; i++, reg++) {
        if (reg == 0x08 || reg == 0x09 || reg <= 0x05) {
            continue; /* read-only */
        }
        m.mem[reg] = w[i];
    }
    if (wl > 1 && w[0] == 0xFE && get16(0xFE) == 0x5400) {
        fgm_reset();
        return 0;
    }
    for (size_t i = 0; i < rl; i++) {
        r[i] = m.mem[reg++];
    }
    update_pin();
    return 0;
}

uint32_t fgm_errors(void)
{
    return m.errors;
}
