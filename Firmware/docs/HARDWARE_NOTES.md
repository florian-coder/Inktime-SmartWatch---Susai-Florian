# Hardware notes for firmware developers

Findings from reading the design files directly (`Hardware/Schematic-final-Susai-Florian.sch`
and `Hardware/InkTime-pcb-Susai-Florian.brd`, Eagle XML). They matter because bare-metal
code hard-codes pin numbers.

## 1. The README pin table does not match the design

The firmware uses the pins of the **routed board** (`.brd`), which is what the Gerbers are
made from. They are collected in `src/board.h`:

| Signal | README says | Board (`.brd`) | Firmware |
|---|---|---|---|
| EPD SCK / MOSI | P0.14 / P0.13 | P0.02 / P0.03 | P0.02 / P0.03 |
| EPD CS / DC | P1.01 / P1.02 | P0.05 / P0.15 | P0.05 / P0.15 |
| EPD RST / BUSY | P0.15 / P0.16 | P0.16 / P0.17 | P0.16 / P0.17 |
| I2C SDA / SCL | P0.26 / P0.27 | P0.06 / P0.07 | P0.06 / P0.07 |
| IMU INT1 / INT2 | P0.04 / P0.05 | P0.08 / P1.08 | P0.08 / P1.08 |
| PMIC /INT | P1.09 | P0.11 | P0.11 |
| Fuel gauge /ALRT | P1.01 | P0.10 (NFC2) | P0.10 |
| Haptic EN | P0.25 | P0.12 | P0.12 |
| Buttons UP / ENTER / DOWN | P0.28 / P0.29 / P0.30 | P0.13 / P0.14 / *not connected* | P0.13 / P0.14 / P1.02 |
| EPD power switch (Q2 gate) | – | P1.01 **and** P1.02 | P1.01 (active low) |
| SWO / log TX | P0.09 | P1.00 (TP_SWO) | P1.00 (UART log) |

## 2. Schematic and board are out of sync

The schematic has net labels whose *names* include sheet references (`P0.13/1.5D`,
`P0.13/2.6C`, `3V3/1.7D`, `SDA/1.4C`...). In Eagle a different name is a different net, so
in the `.sch`:

- the three buttons are not connected to the MCU;
- DRV2605 and MAX17048 sit on their own I2C bus (`SDA/1.4C`, `SCL/1.4C`) that does not reach the MCU;
- the MCU `VDD` pins (net `3V3`) only connect to decoupling capacitors.

The `.brd` merged most of these (buttons UP/ENTER and a single I2C bus are routed), but not all.

## 3. Problems that remain on the routed board

1. **SW3 (DOWN) is not routed to the MCU** - net `P1.02` contains only SW3, R12 and C29.
   The firmware is written so that every function is reachable with UP + ENTER only
   (menus wrap around, values wrap); `app_reachable_with_up_and_enter_only` tests this.
2. **P1.01 and P1.02 are shorted** (both on net `P1.01`, which drives the Q2 gate).
   P1.01 is held low to power the panel, so P1.02 always reads low: if the firmware used it
   for the DOWN button, DOWN would look permanently pressed (endless auto-repeat). With the
   default `BOARD_REV=1` the firmware therefore leaves P1.02 unconfigured and has no DOWN
   button. Rework for `BOARD_REV=2`: cut the P1.02 pad from the Q2-gate net and wire SW3
   (net `P1.02`, with R12/C29) to it.
3. **MCU supply is split** - VDD pads A22 and B1 plus **DEC2 (A18)** are on net `S$3`
   together with the Tag-Connect VCC, Q2 source and the button pull-ups, while VDD pads
   AD14/AD23 and VDDH are on `3V3` with the regulator output. `S$3` has no connection to the
   regulator. DEC2 is a decoupling *output* of the internal regulator and must not be tied to
   3.3 V. This needs a fix before ordering boards.
4. **No pull-ups on /INT (BQ25180) and /ALRT (MAX17048)**: both are open drain. The firmware
   enables the internal ~13 kOhm pull-ups, which is fine, but external 10k would be more robust.
5. **P0.10 is an NFC pin**. It only works as GPIO after `UICR.NFCPINS` is cleared; the linker
   script puts that word into the HEX/ELF image, and `hal_init()` fixes it at runtime if the
   chip was flashed from a `.bin`.

## 4. Things the firmware cannot verify without hardware

- E-paper waveform choice (`0xF7` full / `0xFF` partial) follows GxEPD2 for the GDEH0154D67
  panel; ghosting and refresh time need checking on the real glass.
- The DRV2605 overdrive clamp (`0x8C`, about 3 V) for the FIT0774 motor.
- Battery life (estimated 5-10 days in the README). The firmware sleeps in System ON with the
  RTC running and wakes once per second; measure with a PPK2 before trusting any number.
- RF / BLE: not implemented (see the main README for options).
