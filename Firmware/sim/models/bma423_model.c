/*
 * BMA423 model. The feature engine only starts if the driver streamed the exact
 * Bosch config blob through the 0x5B/0x5C/0x5E window, with advanced power save
 * disabled, so the upload algorithm is verified byte for byte.
 */
#include <string.h>

#include "bma423_config.h"
#include "board.h"
#include "hal/host/hal_host.h"
#include "models.h"

#define FEAT_SIZE       70
#define FEAT_START_WORD 0x0400 /* arbitrary non-zero feature area address */

static struct {
    uint8_t regs[128];
    uint8_t config[BMA423_CONFIG_SIZE];
    uint32_t config_bytes;
    bool aps_violation;
    uint16_t asic_word;
    bool engine_ok;
    uint8_t feature[FEAT_SIZE];
    uint32_t steps;
    uint8_t int_status0;
    uint32_t errors;
} m;

static void power_on_reset(void)
{
    uint32_t keep_steps = m.steps;
    memset(&m, 0, sizeof(m));
    m.steps = keep_steps; /* not physical, but lets tests survive an MCU reset */
    m.regs[0x00] = 0x13;  /* CHIP_ID */
    m.regs[0x40] = 0xA8;  /* ACC_CONF */
    m.regs[0x7C] = 0x03;  /* PWR_CONF: APS on */
}

static void update_int1(void)
{
    const uint8_t io = m.regs[0x53];
    if (!(io & 0x08)) {
        host_pin_release(PIN_IMU_INT1);
        return;
    }
    bool active = (m.int_status0 & m.regs[0x56]) != 0;
    bool active_high = (io & 0x02) != 0;
    host_pin_drive(PIN_IMU_INT1, active == active_high);
}

void bmam_reset(void)
{
    m.steps = 0;
    power_on_reset();
    update_int1();
}

static bool step_counter_on(void)
{
    return m.engine_ok && (m.feature[0x3B] & 0x10) && (m.regs[0x7D] & 0x04);
}

static void port_write(uint32_t i, uint8_t v)
{
    if (m.regs[0x7C] & 0x01) {
        m.aps_violation = true;
        m.errors++;
    }
    if (!m.engine_ok) {
        uint32_t off = (uint32_t)m.asic_word * 2 + i;
        if (off < BMA423_CONFIG_SIZE) {
            m.config[off] = v;
            m.config_bytes++;
        } else {
            m.errors++;
        }
    } else if (m.asic_word == FEAT_START_WORD && i < FEAT_SIZE) {
        m.feature[i] = v;
    } else {
        m.errors++;
    }
}

static uint8_t port_read(uint32_t i)
{
    if (m.engine_ok) {
        return (m.asic_word == FEAT_START_WORD && i < FEAT_SIZE) ? m.feature[i] : 0;
    }
    uint32_t off = (uint32_t)m.asic_word * 2 + i;
    return off < BMA423_CONFIG_SIZE ? m.config[off] : 0;
}

static void reg_write(uint8_t reg, uint8_t v)
{
    switch (reg) {
    case 0x5B: m.asic_word = (uint16_t)((m.asic_word & 0xFF0) | (v & 0x0F)); break;
    case 0x5C: m.asic_word = (uint16_t)((m.asic_word & 0x00F) | ((uint16_t)v << 4)); break;
    case 0x59:
        m.regs[0x59] = v;
        if (v == 0x01) {
            bool ok = m.config_bytes >= BMA423_CONFIG_SIZE && !m.aps_violation &&
                      memcmp(m.config, bma423_config_file, BMA423_CONFIG_SIZE) == 0;
            m.engine_ok = ok;
            m.regs[0x2A] = ok ? 0x01 : 0x02; /* init_ok / init_err */
            if (ok) {
                m.asic_word = FEAT_START_WORD;
            }
        }
        break;
    case 0x7E:
        if (v == 0xB6) {
            power_on_reset();
        }
        break;
    default:
        if (reg < sizeof(m.regs) && reg != 0x00 && reg != 0x2A) {
            m.regs[reg] = v;
        }
        break;
    }
}

static uint8_t reg_read(uint8_t reg)
{
    switch (reg) {
    case 0x12: return 0x00; /* acc X = 0 */
    case 0x13: return 0x00;
    case 0x14: return 0x00; /* acc Y = 0 */
    case 0x15: return 0x00;
    case 0x16: return 0x00; /* acc Z = +1 g (1024 << 4) */
    case 0x17: return 0x40;
    case 0x1C: {
        uint8_t v = m.int_status0; /* latched, cleared on read */
        m.int_status0 = 0;
        update_int1();
        return v;
    }
    case 0x1E: return (uint8_t)(m.steps);
    case 0x1F: return (uint8_t)(m.steps >> 8);
    case 0x20: return (uint8_t)(m.steps >> 16);
    case 0x21: return (uint8_t)(m.steps >> 24);
    case 0x5B: return (uint8_t)(m.asic_word & 0x0F);
    case 0x5C: return (uint8_t)(m.asic_word >> 4);
    default: return reg < sizeof(m.regs) ? m.regs[reg] : 0;
    }
}

int bmam_i2c(const uint8_t *w, size_t wl, uint8_t *r, size_t rl)
{
    if (wl == 0) {
        return -1;
    }
    uint8_t reg = w[0];
    for (size_t i = 1; i < wl; i++) {
        if (reg == 0x5E) {
            port_write((uint32_t)(i - 1), w[i]);
        } else {
            reg_write(reg++, w[i]);
        }
    }
    for (size_t i = 0; i < rl; i++) {
        r[i] = reg == 0x5E ? port_read((uint32_t)i) : reg_read(reg++);
    }
    update_int1();
    return 0;
}

void bmam_add_steps(uint32_t n)
{
    if (!step_counter_on()) {
        return;
    }
    uint32_t wm = (uint32_t)m.feature[0x3A] | ((uint32_t)(m.feature[0x3B] & 0x03) << 8);
    uint32_t before = m.steps;
    m.steps += n;
    if (wm != 0 && m.steps / (wm * 20) != before / (wm * 20)) {
        m.int_status0 |= 0x02;
        update_int1();
    }
}

uint32_t bmam_steps(void)
{
    return m.steps;
}

bool bmam_feature_engine_ok(void)
{
    return m.engine_ok;
}

bool bmam_step_counter_enabled(void)
{
    return step_counter_on();
}

uint32_t bmam_errors(void)
{
    return m.errors;
}
