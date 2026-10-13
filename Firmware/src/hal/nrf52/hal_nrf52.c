/*
 * Bare-metal nRF52840 HAL. Register names come from the Nordic MDK (nrf52840.h /
 * nrf52840_bitfields.h); no SDK or driver library is used.
 *
 * Peripheral allocation:
 *   RTC1    32.768 kHz timebase + wake-up compare (CC[0]), LFXO clock
 *   GPIOTE  PORT event with per-pin SENSE (low power, ~0 uA) for all watched inputs
 *   SPIM2   e-paper (write only, EasyDMA)
 *   TWI0    shared I2C bus (legacy byte-by-byte TWI, works on silicon and in Renode)
 *   UARTE0  debug log (TX only)
 *   WDT     120 s, keeps running while the CPU sleeps
 */
#include <string.h>

#include "board.h"
#include "hal/hal.h"
#include "nrf.h"

#define RTC_HZ       32768u
#define RTC_MASK     0x00FFFFFFu
#define MAX_SLEEP_MS 256000u /* well below the 512 s RTC wrap, see hal_millis() */
#define MAX_WATCH    8
#define TWI_TIMEOUT  200000u
#define SPI_TIMEOUT  2000000u
#define UART_TIMEOUT 2000000u
#define LFXO_TIMEOUT 4000000u
#define WDT_RELOAD   0x6E524635u

static uint64_t s_ticks_hi;
static uint32_t s_last_counter;
static volatile bool s_wake_pending;
static volatile uint32_t s_changes;
static uint8_t s_watch_pin[MAX_WATCH];
static uint8_t s_watch_count;
static uint32_t s_reset_reason;
static bool s_log_ready;
static uint8_t s_log_buf[128]; /* UARTE EasyDMA source must be in RAM */

static NRF_GPIO_Type *port_of(uint8_t pin)
{
    return PIN_PORT(pin) ? NRF_P1 : NRF_P0;
}

/* ---------------------------------------------------------------- clocks / time */

static void start_lfclk(void)
{
    NRF_CLOCK->LFCLKSRC = CLOCK_LFCLKSRC_SRC_Xtal << CLOCK_LFCLKSRC_SRC_Pos;
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_LFCLKSTART = 1;
    for (uint32_t i = 0; i < LFXO_TIMEOUT && !NRF_CLOCK->EVENTS_LFCLKSTARTED; i++) {
    }
    if (!NRF_CLOCK->EVENTS_LFCLKSTARTED) {
        /* Crystal did not start: fall back to the internal RC oscillator. */
        NRF_CLOCK->TASKS_LFCLKSTOP = 1;
        NRF_CLOCK->LFCLKSRC = CLOCK_LFCLKSRC_SRC_RC << CLOCK_LFCLKSRC_SRC_Pos;
        NRF_CLOCK->TASKS_LFCLKSTART = 1;
        for (uint32_t i = 0; i < LFXO_TIMEOUT && !NRF_CLOCK->EVENTS_LFCLKSTARTED; i++) {
        }
    }
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
}

static void start_rtc(void)
{
    NRF_RTC1->TASKS_STOP = 1;
    NRF_RTC1->PRESCALER = 0; /* 30.5 us per tick */
    NRF_RTC1->TASKS_CLEAR = 1;
    NRF_RTC1->EVENTS_COMPARE[0] = 0;
    NRF_RTC1->INTENCLR = 0xFFFFFFFFu;
    NVIC_SetPriority(RTC1_IRQn, 3);
    NVIC_ClearPendingIRQ(RTC1_IRQn);
    NVIC_EnableIRQ(RTC1_IRQn);
    NRF_RTC1->TASKS_START = 1;
    s_ticks_hi = 0;
    s_last_counter = 0;
}

void RTC1_IRQHandler(void);
void RTC1_IRQHandler(void)
{
    if (NRF_RTC1->EVENTS_COMPARE[0]) {
        NRF_RTC1->EVENTS_COMPARE[0] = 0;
        (void)NRF_RTC1->EVENTS_COMPARE[0];
        NRF_RTC1->INTENCLR = RTC_INTENCLR_COMPARE0_Msk;
        s_wake_pending = true;
    }
}

/* The RTC counter is 24 bits (wraps every 512 s). It is extended in software,
 * which works as long as hal_millis() runs at least once per wrap; the main loop
 * never sleeps longer than MAX_SLEEP_MS. */
static uint64_t rtc_ticks(void)
{
    __disable_irq();
    uint32_t c = NRF_RTC1->COUNTER & RTC_MASK;
    if (c < s_last_counter) {
        s_ticks_hi += RTC_MASK + 1u;
    }
    s_last_counter = c;
    uint64_t t = s_ticks_hi + c;
    __enable_irq();
    return t;
}

uint64_t hal_millis(void)
{
    return rtc_ticks() * 1000u / RTC_HZ;
}

void hal_set_wakeup(uint64_t at_ms)
{
    NRF_RTC1->INTENCLR = RTC_INTENCLR_COMPARE0_Msk;
    NRF_RTC1->EVENTS_COMPARE[0] = 0;
    s_wake_pending = false;

    const uint64_t now = hal_millis();
    uint64_t delta = (at_ms == 0) ? MAX_SLEEP_MS : (at_ms > now ? at_ms - now : 0);
    if (at_ms != 0 && delta == 0) {
        s_wake_pending = true;
        return;
    }
    if (delta > MAX_SLEEP_MS) {
        delta = MAX_SLEEP_MS;
    }
    uint32_t ticks = (uint32_t)((delta * RTC_HZ + 999u) / 1000u);
    if (ticks < 2) {
        ticks = 2; /* CC must be at least COUNTER + 2 to be guaranteed to trigger */
    }
    NRF_RTC1->CC[0] = (NRF_RTC1->COUNTER + ticks) & RTC_MASK;
    NRF_RTC1->INTENSET = RTC_INTENSET_COMPARE0_Msk;
}

void hal_sleep(void)
{
    /* WFI with PRIMASK set still wakes on a pending interrupt, so checking the
     * flags and sleeping cannot race with an ISR. */
    __disable_irq();
    if (!s_wake_pending && s_changes == 0) {
        __DSB();
        __WFI();
    }
    __enable_irq();
}

void hal_delay_ms(uint32_t ms)
{
    const uint64_t end = rtc_ticks() + ((uint64_t)ms * RTC_HZ + 999u) / 1000u + 1u;
    while (rtc_ticks() < end) {
    }
}

/* ---------------------------------------------------------------- GPIO */

static uint32_t pull_bits(hal_pull_t pull)
{
    switch (pull) {
    case HAL_PULL_UP: return GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos;
    case HAL_PULL_DOWN: return GPIO_PIN_CNF_PULL_Pulldown << GPIO_PIN_CNF_PULL_Pos;
    default: return GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos;
    }
}

void hal_gpio_output(uint8_t pin, bool level)
{
    NRF_GPIO_Type *p = port_of(pin);
    const uint32_t bit = 1u << PIN_NUM(pin);
    if (level) {
        p->OUTSET = bit;
    } else {
        p->OUTCLR = bit;
    }
    p->PIN_CNF[PIN_NUM(pin)] =
        (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) | (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos);
}

void hal_gpio_input(uint8_t pin, hal_pull_t pull)
{
    NRF_GPIO_Type *p = port_of(pin);
    const uint32_t sense = p->PIN_CNF[PIN_NUM(pin)] & GPIO_PIN_CNF_SENSE_Msk;
    p->PIN_CNF[PIN_NUM(pin)] = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
                               (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) | pull_bits(pull) | sense;
}

void hal_gpio_write(uint8_t pin, bool level)
{
    NRF_GPIO_Type *p = port_of(pin);
    if (level) {
        p->OUTSET = 1u << PIN_NUM(pin);
    } else {
        p->OUTCLR = 1u << PIN_NUM(pin);
    }
}

bool hal_gpio_read(uint8_t pin)
{
    return (port_of(pin)->IN >> PIN_NUM(pin)) & 1u;
}

static void set_sense(uint8_t pin, uint32_t sense)
{
    NRF_GPIO_Type *p = port_of(pin);
    uint32_t cfg = p->PIN_CNF[PIN_NUM(pin)] & ~GPIO_PIN_CNF_SENSE_Msk;
    p->PIN_CNF[PIN_NUM(pin)] = cfg | (sense << GPIO_PIN_CNF_SENSE_Pos);
}

/* Arm SENSE for the opposite of the current level, so the next change raises DETECT. */
static void arm_sense(uint8_t pin)
{
    set_sense(pin, hal_gpio_read(pin) ? GPIO_PIN_CNF_SENSE_Low : GPIO_PIN_CNF_SENSE_High);
}

int hal_gpio_watch(uint8_t pin)
{
    if (s_watch_count >= MAX_WATCH) {
        return -1;
    }
    const int idx = s_watch_count++;
    s_watch_pin[idx] = pin;
    arm_sense(pin);
    NRF_GPIOTE->EVENTS_PORT = 0;
    NRF_GPIOTE->INTENSET = GPIOTE_INTENSET_PORT_Msk;
    NVIC_SetPriority(GPIOTE_IRQn, 3);
    NVIC_EnableIRQ(GPIOTE_IRQn);
    return idx;
}

void GPIOTE_IRQHandler(void);
void GPIOTE_IRQHandler(void)
{
    if (!NRF_GPIOTE->EVENTS_PORT) {
        return;
    }
    NRF_GPIOTE->EVENTS_PORT = 0;
    (void)NRF_GPIOTE->EVENTS_PORT;

    /* DETECT is the OR of all sensing pins and PORT fires on its rising edge, so
     * every pin that matches its SENSE level must be re-armed or DETECT stays high
     * and no further event is generated. Re-check a few times for fast toggles. */
    for (int pass = 0; pass < 4; pass++) {
        bool rearmed = false;
        for (uint8_t i = 0; i < s_watch_count; i++) {
            const uint8_t pin = s_watch_pin[i];
            const uint32_t sense =
                (port_of(pin)->PIN_CNF[PIN_NUM(pin)] & GPIO_PIN_CNF_SENSE_Msk) >> GPIO_PIN_CNF_SENSE_Pos;
            const bool level = hal_gpio_read(pin);
            if ((sense == GPIO_PIN_CNF_SENSE_High && level) || (sense == GPIO_PIN_CNF_SENSE_Low && !level)) {
                set_sense(pin, level ? GPIO_PIN_CNF_SENSE_Low : GPIO_PIN_CNF_SENSE_High);
                s_changes |= 1u << i;
                rearmed = true;
            }
        }
        if (!rearmed) {
            break;
        }
    }
}

uint32_t hal_gpio_take_changes(void)
{
    __disable_irq();
    uint32_t c = s_changes;
    s_changes = 0;
    __enable_irq();
    return c;
}

/* ---------------------------------------------------------------- SPIM2 */

void hal_spi_init(uint8_t sck, uint8_t mosi)
{
    hal_gpio_output(sck, false);
    hal_gpio_output(mosi, false);
    NRF_SPIM2->ENABLE = SPIM_ENABLE_ENABLE_Disabled << SPIM_ENABLE_ENABLE_Pos;
    NRF_SPIM2->PSEL.SCK = sck;
    NRF_SPIM2->PSEL.MOSI = mosi;
    NRF_SPIM2->PSEL.MISO = SPIM_PSEL_MISO_CONNECT_Msk; /* disconnected */
    NRF_SPIM2->FREQUENCY = SPIM_FREQUENCY_FREQUENCY_M4;
    NRF_SPIM2->CONFIG = (SPIM_CONFIG_ORDER_MsbFirst << SPIM_CONFIG_ORDER_Pos) |
                        (SPIM_CONFIG_CPHA_Leading << SPIM_CONFIG_CPHA_Pos) |
                        (SPIM_CONFIG_CPOL_ActiveHigh << SPIM_CONFIG_CPOL_Pos); /* mode 0 */
    NRF_SPIM2->ORC = 0xFF;
    NRF_SPIM2->RXD.MAXCNT = 0;
    NRF_SPIM2->ENABLE = SPIM_ENABLE_ENABLE_Enabled << SPIM_ENABLE_ENABLE_Pos;
}

void hal_spi_write(const uint8_t *buf, size_t len)
{
    while (len > 0) {
        const uint32_t n = len > 0xFFFFu ? 0xFFFFu : (uint32_t)len;
        NRF_SPIM2->TXD.PTR = (uint32_t)buf;
        NRF_SPIM2->TXD.MAXCNT = n;
        NRF_SPIM2->RXD.MAXCNT = 0;
        NRF_SPIM2->EVENTS_END = 0;
        NRF_SPIM2->TASKS_START = 1;
        for (uint32_t i = 0; i < SPI_TIMEOUT && !NRF_SPIM2->EVENTS_END; i++) {
        }
        NRF_SPIM2->EVENTS_END = 0;
        buf += n;
        len -= n;
    }
}

/* ---------------------------------------------------------------- TWI0 (legacy) */

void hal_i2c_init(uint8_t sda, uint8_t scl)
{
    /* Open drain (standard 0, disconnect 1); the board has 3k3 pull-ups. */
    const uint32_t cfg =
        (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) | (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
        (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) | (GPIO_PIN_CNF_DRIVE_S0D1 << GPIO_PIN_CNF_DRIVE_Pos);
    port_of(sda)->PIN_CNF[PIN_NUM(sda)] = cfg;
    port_of(scl)->PIN_CNF[PIN_NUM(scl)] = cfg;

    NRF_TWI0->ENABLE = TWI_ENABLE_ENABLE_Disabled << TWI_ENABLE_ENABLE_Pos;
    NRF_TWI0->PSEL.SCL = scl;
    NRF_TWI0->PSEL.SDA = sda;
    NRF_TWI0->FREQUENCY = TWI_FREQUENCY_FREQUENCY_K400;
    NRF_TWI0->SHORTS = 0;
    NRF_TWI0->ENABLE = TWI_ENABLE_ENABLE_Enabled << TWI_ENABLE_ENABLE_Pos;
}

static int twi_abort(void)
{
    NRF_TWI0->EVENTS_ERROR = 0;
    const uint32_t errors = NRF_TWI0->ERRORSRC;
    NRF_TWI0->ERRORSRC = errors; /* write 1 to clear */
    NRF_TWI0->SHORTS = 0;
    NRF_TWI0->EVENTS_STOPPED = 0;
    NRF_TWI0->TASKS_STOP = 1;
    for (uint32_t i = 0; i < TWI_TIMEOUT && !NRF_TWI0->EVENTS_STOPPED; i++) {
    }
    NRF_TWI0->EVENTS_STOPPED = 0;
    return -1;
}

static int twi_wait(volatile uint32_t *event)
{
    for (uint32_t i = 0; i < TWI_TIMEOUT; i++) {
        if (*event) {
            *event = 0;
            return 0;
        }
        if (NRF_TWI0->EVENTS_ERROR) {
            return twi_abort();
        }
    }
    twi_abort();
    return -2;
}

int hal_i2c_write_read(uint8_t addr, const uint8_t *wbuf, size_t wlen, uint8_t *rbuf, size_t rlen)
{
    NRF_TWI0->ADDRESS = addr;
    NRF_TWI0->SHORTS = 0;
    NRF_TWI0->EVENTS_TXDSENT = 0;
    NRF_TWI0->EVENTS_RXDREADY = 0;
    NRF_TWI0->EVENTS_STOPPED = 0;
    NRF_TWI0->EVENTS_ERROR = 0;

    if (wlen > 0) {
        /* STARTTX first: the master stretches SCL until each TXD byte is written. */
        NRF_TWI0->TASKS_STARTTX = 1;
        for (size_t i = 0; i < wlen; i++) {
            NRF_TWI0->TXD = wbuf[i];
            if (twi_wait(&NRF_TWI0->EVENTS_TXDSENT) != 0) {
                return -1;
            }
        }
    }

    if (rlen > 0) {
        /* Repeated start. BB_SUSPEND holds the bus after each byte until RXD is
         * read; BB_STOP is armed once only the last byte remains (as nrfx does). */
        NRF_TWI0->SHORTS = rlen == 1 ? TWI_SHORTS_BB_STOP_Msk : TWI_SHORTS_BB_SUSPEND_Msk;
        NRF_TWI0->TASKS_STARTRX = 1;
        for (size_t i = 0; i < rlen; i++) {
            if (twi_wait(&NRF_TWI0->EVENTS_RXDREADY) != 0) {
                return -1;
            }
            rbuf[i] = (uint8_t)NRF_TWI0->RXD;
            if (i + 1 == rlen - 1) {
                NRF_TWI0->SHORTS = TWI_SHORTS_BB_STOP_Msk;
            }
            if (i + 1 < rlen) {
                NRF_TWI0->TASKS_RESUME = 1;
            }
        }
    } else {
        NRF_TWI0->TASKS_STOP = 1;
    }

    const int rc = twi_wait(&NRF_TWI0->EVENTS_STOPPED);
    NRF_TWI0->SHORTS = 0;
    return rc;
}

int hal_i2c_write(uint8_t addr, const uint8_t *buf, size_t len)
{
    return hal_i2c_write_read(addr, buf, len, NULL, 0);
}

/* ---------------------------------------------------------------- UARTE0 log */

static void log_init(void)
{
    hal_gpio_output(PIN_LOG_TX, true);
    NRF_UARTE0->ENABLE = UARTE_ENABLE_ENABLE_Disabled << UARTE_ENABLE_ENABLE_Pos;
    NRF_UARTE0->PSEL.TXD = PIN_LOG_TX;
    NRF_UARTE0->PSEL.RXD = UARTE_PSEL_RXD_CONNECT_Msk; /* disconnected */
    NRF_UARTE0->PSEL.RTS = UARTE_PSEL_RTS_CONNECT_Msk;
    NRF_UARTE0->PSEL.CTS = UARTE_PSEL_CTS_CONNECT_Msk;
    NRF_UARTE0->BAUDRATE = UARTE_BAUDRATE_BAUDRATE_Baud115200;
    NRF_UARTE0->CONFIG = 0;
    NRF_UARTE0->ENABLE = UARTE_ENABLE_ENABLE_Enabled << UARTE_ENABLE_ENABLE_Pos;
    s_log_ready = true;
}

void hal_log_write(const char *s, size_t len)
{
    if (!s_log_ready) {
        return;
    }
    while (len > 0) {
        const size_t n = len < sizeof(s_log_buf) ? len : sizeof(s_log_buf);
        memcpy(s_log_buf, s, n);
        NRF_UARTE0->TXD.PTR = (uint32_t)s_log_buf;
        NRF_UARTE0->TXD.MAXCNT = n;
        NRF_UARTE0->EVENTS_ENDTX = 0;
        NRF_UARTE0->TASKS_STARTTX = 1;
        for (uint32_t i = 0; i < UART_TIMEOUT && !NRF_UARTE0->EVENTS_ENDTX; i++) {
        }
        NRF_UARTE0->EVENTS_ENDTX = 0;
        s += n;
        len -= n;
    }
    /* Stop the transmitter so it releases the high-frequency clock. */
    NRF_UARTE0->TASKS_STOPTX = 1;
}

/* ---------------------------------------------------------------- system */

/* P0.09/P0.10 default to NFC; the fuel-gauge ALERT line is on P0.10. The UICR word
 * is part of the programmed image (see the linker script)... */
__attribute__((section(".uicr_nfcpins"), used)) const uint32_t uicr_nfcpins = 0xFFFFFFFEu;

static void nfc_pins_as_gpio(void)
{
    /* ...and if the chip was flashed from a .bin without UICR, fix it at runtime:
     * a one-time flash write followed by a reset. */
    if ((NRF_UICR->NFCPINS & UICR_NFCPINS_PROTECT_Msk) == (UICR_NFCPINS_PROTECT_NFC << UICR_NFCPINS_PROTECT_Pos)) {
        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
        for (uint32_t i = 0; i < 100000u && NRF_NVMC->READY == NVMC_READY_READY_Busy; i++) {
        }
        NRF_UICR->NFCPINS &= ~UICR_NFCPINS_PROTECT_Msk;
        for (uint32_t i = 0; i < 100000u && NRF_NVMC->READY == NVMC_READY_READY_Busy; i++) {
        }
        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
        NVIC_SystemReset();
    }
}

static void watchdog_start(void)
{
    if (NRF_WDT->RUNSTATUS) {
        return; /* survived a soft reset: CRV can no longer be changed */
    }
    NRF_WDT->CONFIG = (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos) | (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos);
    NRF_WDT->CRV = HAL_WATCHDOG_S * RTC_HZ;
    NRF_WDT->RREN = WDT_RREN_RR0_Msk;
    NRF_WDT->TASKS_START = 1;
}

void hal_watchdog_feed(void)
{
    NRF_WDT->RR[0] = WDT_RELOAD;
}

void hal_init(void)
{
    s_reset_reason = NRF_POWER->RESETREAS;
    NRF_POWER->RESETREAS = 0xFFFFFFFFu; /* bits are cleared by writing 1 */

    nfc_pins_as_gpio();
    /* The board has the inductors for the REG1 DC/DC converter. */
    NRF_POWER->DCDCEN = POWER_DCDCEN_DCDCEN_Enabled << POWER_DCDCEN_DCDCEN_Pos;

    start_lfclk();
    start_rtc();
    watchdog_start();
    log_init();
}

uint32_t hal_reset_reason(void)
{
    /* HAL_RESET_* bit positions are chosen to match POWER->RESETREAS. */
    return s_reset_reason &
           (HAL_RESET_PIN | HAL_RESET_WATCHDOG | HAL_RESET_SOFT | HAL_RESET_LOCKUP | HAL_RESET_WAKE_OFF);
}

void hal_system_off(uint8_t wake_pin)
{
    __disable_irq();
    /* Only the wake button may raise DETECT in System OFF. */
    for (uint8_t i = 0; i < s_watch_count; i++) {
        set_sense(s_watch_pin[i], GPIO_PIN_CNF_SENSE_Disabled);
    }
    NRF_UARTE0->ENABLE = 0;
    port_of(wake_pin)->PIN_CNF[PIN_NUM(wake_pin)] =
        (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) | (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
        (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos) | (GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos);
    NRF_POWER->SYSTEMOFF = POWER_SYSTEMOFF_SYSTEMOFF_Enter;
    __DSB();
    for (;;) {
        /* With a debugger attached System OFF is emulated and execution continues here. */
        __WFE();
    }
}
