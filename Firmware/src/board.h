/*
 * InkTime v1 board description.
 *
 * Pin numbers come from the routed PCB (Hardware/InkTime-pcb-Susai-Florian.brd),
 * NOT from the README table, which does not match the design. Pins are encoded
 * as (port << 5) | pin, the same encoding the nRF52840 PSEL registers use.
 *
 * Known PCB rev v1 issues that affect firmware (see docs/HARDWARE_NOTES.md):
 *  - SW3 (BTN_DOWN) is not routed to the MCU. The schematic intent is P1.02.
 *  - P1.01 and P1.02 are shorted together on the EPD power-switch net, so P1.02
 *    reads low whenever the panel is powered.
 *  - P0.10 (fuel gauge ALERT) is an NFC pin; UICR.NFCPINS must be cleared.
 *
 * BOARD_REV 1 = boards as designed: no DOWN button (every screen works with UP + ENTER).
 * BOARD_REV 2 = with the rework from the notes: SW3 on P1.02 and the short removed.
 * The firmware defaults to 1; the simulator and host tests model rev 2.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#ifndef BOARD_REV
#define BOARD_REV 1
#endif

#define PIN(port, pin) ((uint8_t)(((port) << 5) | (pin)))
#define PIN_NC         0xFFu /* not connected */
#define PIN_PORT(p)    ((uint8_t)((p) >> 5))
#define PIN_NUM(p)     ((uint8_t)((p) & 0x1F))

/* E-paper, Waveshare 1.54" V2 (SSD1681), on SPIM2 */
#define PIN_EPD_SCK   PIN(0, 2)
#define PIN_EPD_MOSI  PIN(0, 3)
#define PIN_EPD_CS    PIN(0, 5)
#define PIN_EPD_DC    PIN(0, 15)
#define PIN_EPD_RST   PIN(0, 16)
#define PIN_EPD_BUSY  PIN(0, 17)
#define PIN_EPD_PWR_N PIN(1, 1) /* Q2 P-MOSFET gate: low = panel powered */

/* Shared I2C bus on TWI0 */
#define PIN_I2C_SDA PIN(0, 6)
#define PIN_I2C_SCL PIN(0, 7)

/* Interrupt / control lines */
#define PIN_IMU_INT1  PIN(0, 8)  /* BMA423 INT1, push-pull active high */
#define PIN_IMU_INT2  PIN(1, 8)  /* BMA423 INT2, unused */
#define PIN_FG_ALERT  PIN(0, 10) /* MAX17048 /ALRT, open drain active low */
#define PIN_PMIC_INT  PIN(0, 11) /* BQ25180 /INT, open drain active low */
#define PIN_HAPTIC_EN PIN(0, 12) /* DRV2605 EN */

/* Buttons: active low, external 10k pull-up + 1uF debounce cap */
#define PIN_BTN_UP    PIN(0, 13)
#define PIN_BTN_ENTER PIN(0, 14)
#if BOARD_REV >= 2
#define PIN_BTN_DOWN PIN(1, 2)
#else
#define PIN_BTN_DOWN PIN_NC
#endif

/* Debug log: UARTE0 TX on test pad TP_SWO */
#define PIN_LOG_TX PIN(1, 0)

/* I2C 7-bit addresses */
#define I2C_ADDR_BMA423   0x18 /* SDO tied to GND through R3 */
#define I2C_ADDR_MAX17048 0x36
#define I2C_ADDR_DRV2605  0x5A
#define I2C_ADDR_BQ25180  0x6A
#define I2C_ADDR_RT6160   0x75 /* left at its power-on defaults */

/* Battery: Akyga LP502030, 250 mAh */
#define BATTERY_CAPACITY_MAH 250
#define BATTERY_CHARGE_MA    120 /* ~0.5C */
#define BATTERY_VREG_MV      4200

#endif
