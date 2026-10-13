# InkTime firmware (bare-metal nRF52840)

Bare-metal C firmware for the InkTime e-paper smartwatch, plus everything needed to run and
test it **without the hardware**: a host simulator (window or REPL), behavioural models of
every peripheral, an emulated nRF52840 (Renode) running the real ARM binary, and an
automated test pipeline.

No SDK, no RTOS: register-level code on top of the Nordic MDK headers and CMSIS.

```
            +-----------------------------------------------------------+
            |  app/      screens, menus, alarm, stopwatch (pure logic)   |
            |  services/ buttons, display refresh policy, haptics       |
            |  drivers/  SSD1681  BMA423  MAX17048  BQ25180  DRV2605     |
            |  core/ gfx/  calendar, event queue, log, 1-bpp graphics    |
            +----------------------------+------------------------------+
                                         | hal/hal.h
            +----------------------------+------------------------------+
            | hal/nrf52/  registers:      | hal/host/  virtual MCU       |
            |  RTC1 GPIOTE SPIM2 TWI0     |  + sim/models/ (C models)    |
            |  UARTE0 WDT POWER           |  + SDL window / REPL         |
            +-----------------------------+------------------------------+
               real watch / Renode              simulator + tests
```

## Quick start

```bash
tools/setup_tools.sh     # ARM GCC, Renode, Robot Framework, gcovr, SDL2 into ~/tools (no sudo)
make run                 # simulator window (WSLg / X11), or a text REPL without a display
make ci                  # the whole test pipeline
make firmware            # build/fw/inktime.hex for the watch
```

## The simulator

`make run` opens the watch in a window. Virtual time follows wall-clock time.

| Key | Action |
|---|---|
| Up / W, Enter / Space, Down / S | the three buttons (hold for long press and auto-repeat), or click the side buttons |
| C | charger: off → charging → full |
| B / G | battery −10 % / +10 % (below 5 % the fuel-gauge alert fires) |
| T | walk 50 steps (BMA423 step interrupt) |
| N | incoming notification |
| F | fast forward ×60 |
| P | save `screenshot_N.pbm` (convert with `tools/pbm2png.py`) |

Without a display, `build/host/inktime_sim --repl` gives a command line with the same
language the scenario tests use (`press enter`, `wait 60000`, `steps 120`, `ascii`, `status`...).

### UI

* **Watch face**: date, battery (bolt while charging), time in 7-segment digits, steps and a
  progress bar towards 8000, alarm time when set. Refreshed once a minute (partial refresh;
  a full refresh every 30 partials to clear ghosting).
* **Menu** (ENTER): Steps, Stopwatch, Alarm, Set time, Battery, Settings (vibration, full
  refresh interval, power off), About (hardware self-test result, uptime).
* Long ENTER always goes back to the watch face. Every function is reachable with UP and
  ENTER only, because the DOWN button is not routed on PCB rev 1.
* Overlays: notification, alarm (vibrates every 2 s for 60 s), low battery.
* Power off = nRF52840 System OFF; the e-paper keeps the last image; ENTER wakes the watch.

## Test pipeline

`make ci` (or `tools/ci.sh`) runs every stage and prints a summary. The same script runs in
GitHub Actions (`.github/workflows/firmware.yml`) and uploads the reports and the HEX.

| Stage | What it checks |
|---|---|
| host build | everything above the HAL compiled for the PC with `-Wall -Wextra -Werror` and ASan + UBSan |
| `make test` | 53 unit and integration tests: calendar math, graphics, debouncing, every app screen and flow, every driver against its peripheral model, and whole-firmware runs (boot, 6 simulated hours, alarm, power off / wake...). JUnit report in `build/reports/unit.xml`. |
| `make scenarios` | 10 scripted end-to-end runs (`tests/scenarios/*.scn`) whose screens are compared pixel-exactly with the golden images in `tests/golden/` |
| `make firmware` | ARM build (`arm-none-eabi-gcc -Werror`) |
| `make size` | flash ≤ 256 KiB and RAM ≤ 64 KiB, which leaves room for a BLE stack |
| `make lint` | GCC `-fanalyzer` (host and ARM), cppcheck, clang-format |
| `make renode-test` | the **real ELF** on an emulated nRF52840 with C# models of the peripherals (`tests/renode/`). The panel contents must be **byte-identical** to the host golden images. Covers hardware detection, interrupts, refresh counts, power off, and the watchdog over 10 minutes. |
| `make coverage` | line coverage of `src/` (gate: 85 %, currently about 93 %) |

The peripheral models are strict: the SSD1681 model flags commands sent while BUSY or in deep
sleep, and only drives pixels that differ between its two RAM banks during a partial refresh,
so a wrong "previous frame" shows up as wrong pixels. The BMA423 model only starts its feature
engine if the 6 KiB Bosch configuration was streamed byte for byte with power save disabled.
The BQ25180 model resets its registers if the firmware forgets to disable the I2C watchdog.

After an intended UI change, run `make golden`, look at the images (`tools/contact_sheet.py`
builds an overview PNG) and commit them.

## Renode by hand

```bash
~/tools/renode/renode tests/renode/inktime.resc
(inktime) start
(inktime) sysbus.gpio0.btn_enter Press ; sysbus.gpio0.btn_enter Release
(inktime) sysbus.twi0.bma AddSteps 120
(inktime) sysbus.spi2.epd SavePbm @/tmp/screen.pbm
```

## On the real watch

* Pins come from the routed PCB, not from the main README table: see `src/board.h` and
  [docs/HARDWARE_NOTES.md](docs/HARDWARE_NOTES.md). **Read the notes before powering a
  board**: the MCU supply is split on rev 1.
* `BOARD_REV=1` (default) matches the boards as designed. `make firmware BOARD_REV=2` enables
  the DOWN button on P1.02 once the documented rework is done.
* Flash over SWD (Tag-Connect J2) with any CMSIS-DAP or J-Link probe:
  `pip install pyocd && make flash`.
* Debug log: UART TX at 115200 baud on test pad TP_SWO (P1.00).
* The image includes the UICR word that turns the NFC pins into GPIOs (P0.10 = fuel-gauge alert).

Not implemented yet: Bluetooth (the notification path `watch_notify()` is ready for it; the
realistic options are Nordic's SoftDevice S140 or Apache NimBLE) and USB.

## Layout

```
src/            firmware (board.h, watch.c main loop, app/, services/, drivers/, gfx/, core/, hal/)
sim/            host simulator: virtual board, peripheral models, scenario language, SDL window
tests/unit/     unit + integration tests (tiny self-registering framework, tst.h)
tests/scenarios end-to-end scripts       tests/golden/  approved screen images
tests/renode/   platform (.repl), C# peripheral models, Robot test suite
tools/          ci.sh, setup_tools.sh, lint.sh, image converters, size check
third_party/    Nordic MDK (BSD-3), CMSIS Core (Apache-2.0), Bosch BMA423 config blob (BSD-3)
```
