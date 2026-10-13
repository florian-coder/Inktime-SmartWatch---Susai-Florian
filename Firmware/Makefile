# InkTime firmware: bare-metal nRF52840 + host simulator + test pipeline.
#
#   make firmware      ARM build (build/fw/inktime.elf/.hex/.bin)
#   make sim           host simulator (build/host/inktime_sim)
#   make test          unit + integration tests on the host (ASan + UBSan)
#   make scenarios     scripted end-to-end runs compared against golden images
#   make renode-test   the real firmware ELF on an emulated nRF52840 (Renode + Robot)
#   make lint          cppcheck + compiler static analysis
#   make coverage      unit-test coverage report (gcovr)
#   make ci            everything above, in pipeline order
#   make run           interactive simulator (SDL window if available, else REPL)
#   make golden        regenerate the golden images after an intended UI change

BUILD      ?= build
TOOLS_DIR  ?= $(HOME)/tools
V          ?= 0
Q          := $(if $(filter 1,$(V)),,@)

ARM_PREFIX ?= $(if $(wildcard $(TOOLS_DIR)/arm-gnu/bin/arm-none-eabi-gcc),$(TOOLS_DIR)/arm-gnu/bin/arm-none-eabi-,arm-none-eabi-)
RENODE_DIR ?= $(if $(wildcard $(TOOLS_DIR)/renode/renode),$(TOOLS_DIR)/renode,)
RENODE_TEST ?= $(if $(RENODE_DIR),$(RENODE_DIR)/renode-test,renode-test)
HOST_CC    ?= gcc
# SDL2 for the simulator window: system package (pkg-config) or a local copy in $(TOOLS_DIR)/sdl2
# (see tools/setup_tools.sh, which extracts the Ubuntu packages without root).
SDL_LOCAL  := $(TOOLS_DIR)/sdl2/usr
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)
ifeq ($(SDL_LIBS),)
ifneq ($(wildcard $(SDL_LOCAL)/include/SDL2/SDL.h),)
SDL_LIBDIR := $(SDL_LOCAL)/lib/x86_64-linux-gnu
SDL_CFLAGS := -I$(SDL_LOCAL)/include/SDL2 -I$(SDL_LOCAL)/include/x86_64-linux-gnu -D_REENTRANT
SDL_LIBS   := -L$(SDL_LIBDIR) -lSDL2 -Wl,--disable-new-dtags -Wl,-rpath,$(SDL_LIBDIR) -Wl,-rpath,$(SDL_LIBDIR)/pulseaudio
endif
endif

# ---------------------------------------------------------------------------
# Sources

COMMON_SRC := \
	src/watch.c \
	src/core/datetime.c src/core/evq.c src/core/log.c \
	src/gfx/gfx.c src/gfx/font5x7.c \
	$(wildcard src/drivers/*.c) \
	$(wildcard src/services/*.c) \
	src/app/app.c src/app/app_ui.c \
	third_party/bosch/bma423_config.c

HOST_SRC := $(COMMON_SRC) src/hal/host/hal_host.c sim/sim.c $(wildcard sim/models/*.c)
UNIT_SRC := $(wildcard tests/unit/*.c)
SIM_SRC  := sim/sim_main.c sim/scenario.c sim/pbm.c $(if $(SDL_LIBS),sim/sim_gui.c,)

FW_SRC := $(COMMON_SRC) \
	src/main.c \
	src/hal/nrf52/startup.c src/hal/nrf52/hal_nrf52.c \
	third_party/nrf_mdk/system_nrf52840.c

INCLUDES := -Isrc -Ithird_party/bosch

# ---------------------------------------------------------------------------
# Host build (simulator + tests)

WARN := -Wall -Wextra -Werror -Wshadow -Wpointer-arith -Wstrict-prototypes \
        -Wmissing-prototypes -Wformat=2 -Wundef -Wno-missing-field-initializers
HOST_CFLAGS := -std=c11 -g -O1 $(WARN) $(INCLUDES) -D_DEFAULT_SOURCE -DBOARD_REV=2 -MMD -MP \
               -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
HOST_LDFLAGS := -fsanitize=address,undefined
# Test files use TEST() constructors, which have no prototypes on purpose.
UNIT_CFLAGS := $(HOST_CFLAGS) -Wno-missing-prototypes

HOST_OBJ := $(patsubst %.c,$(BUILD)/host/%.o,$(HOST_SRC))
UNIT_OBJ := $(patsubst %.c,$(BUILD)/host/%.o,$(UNIT_SRC))
SIM_OBJ  := $(patsubst %.c,$(BUILD)/host/%.o,$(SIM_SRC))

UNIT_BIN := $(BUILD)/host/unit_tests
SIM_BIN  := $(BUILD)/host/inktime_sim

$(BUILD)/host/tests/unit/%.o: tests/unit/%.c
	@mkdir -p $(dir $@)
	@echo "  CC(host) $<"
	$(Q)$(HOST_CC) $(UNIT_CFLAGS) -c $< -o $@

$(BUILD)/host/sim/sim_gui.o: sim/sim_gui.c
	@mkdir -p $(dir $@)
	@echo "  CC(host) $<"
	$(Q)$(HOST_CC) $(HOST_CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(BUILD)/host/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC(host) $<"
	$(Q)$(HOST_CC) $(HOST_CFLAGS) $(if $(SDL_LIBS),-DHAVE_SDL,) -c $< -o $@

# HAVE_SDL is decided in this Makefile, so the simulator front-end depends on it.
$(BUILD)/host/sim/sim_main.o: Makefile

$(UNIT_BIN): $(HOST_OBJ) $(UNIT_OBJ)
	@echo "  LD(host) $@"
	$(Q)$(HOST_CC) $(HOST_LDFLAGS) $^ -o $@

$(SIM_BIN): $(HOST_OBJ) $(SIM_OBJ)
	@echo "  LD(host) $@"
	$(Q)$(HOST_CC) $(HOST_LDFLAGS) $^ -o $@ $(SDL_LIBS)

# ---------------------------------------------------------------------------
# Firmware (bare-metal nRF52840)

FW_CC      := $(ARM_PREFIX)gcc
FW_OBJCOPY := $(ARM_PREFIX)objcopy
FW_SIZE    := $(ARM_PREFIX)size
FW_ARCH    := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
BOARD_REV  ?= 1
FW_CFLAGS  := -std=c11 -Os -g3 $(FW_ARCH) $(WARN) $(INCLUDES) -DBOARD_REV=$(BOARD_REV) \
              -Ithird_party/nrf_mdk -Ithird_party/cmsis/Include \
              -DNRF52840_XXAA -ffunction-sections -fdata-sections -fno-common \
              -ffreestanding -MMD -MP
FW_LDFLAGS := $(FW_ARCH) -Tsrc/hal/nrf52/nrf52840.ld -Wl,--gc-sections \
              -Wl,-Map=$(BUILD)/fw/inktime.map -Wl,--print-memory-usage \
              --specs=nano.specs --specs=nosys.specs -nostartfiles
FW_OBJ := $(patsubst %.c,$(BUILD)/fw/%.o,$(FW_SRC))
FW_ELF := $(BUILD)/fw/inktime.elf

# Vendor code is compiled with its own (relaxed) warnings.
$(BUILD)/fw/third_party/nrf_mdk/%.o: third_party/nrf_mdk/%.c
	@mkdir -p $(dir $@)
	@echo "  CC(arm)  $<"
	$(Q)$(FW_CC) $(filter-out -Werror -Wmissing-prototypes -Wundef -Wstrict-prototypes,$(FW_CFLAGS)) -c $< -o $@

$(BUILD)/fw/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC(arm)  $<"
	$(Q)$(FW_CC) $(FW_CFLAGS) -c $< -o $@

$(FW_ELF): $(FW_OBJ) src/hal/nrf52/nrf52840.ld
	@echo "  LD(arm)  $@"
	$(Q)$(FW_CC) $(FW_LDFLAGS) $(FW_OBJ) -o $@
	$(Q)$(FW_OBJCOPY) -O ihex $@ $(BUILD)/fw/inktime.hex
	$(Q)$(FW_OBJCOPY) -O binary -R .uicr_nfcpins $@ $(BUILD)/fw/inktime.bin
	$(Q)$(FW_SIZE) $@

# ---------------------------------------------------------------------------
# Pipeline targets

.PHONY: all firmware sim test scenarios golden renode-test lint format coverage size ci run flash clean

all: firmware sim $(UNIT_BIN)

firmware: $(FW_ELF)

sim: $(SIM_BIN)

test: $(UNIT_BIN)
	@mkdir -p $(BUILD)/reports
	$(UNIT_BIN) --junit $(BUILD)/reports/unit.xml

scenarios: $(SIM_BIN)
	@mkdir -p $(BUILD)/reports/scenarios
	$(Q)python3 -I tools/run_scenarios.py $(SIM_BIN) tests/scenarios tests/golden $(BUILD)/reports/scenarios

golden: $(SIM_BIN)
	@mkdir -p $(BUILD)/reports/scenarios
	$(Q)python3 -I tools/run_scenarios.py $(SIM_BIN) tests/scenarios tests/golden $(BUILD)/reports/scenarios --update-golden

# renode-test writes its reports into the current directory, so run it from the report dir.
renode-test: $(FW_ELF)
	@rm -rf $(BUILD)/reports/renode && mkdir -p $(BUILD)/reports/renode
	cd $(BUILD)/reports/renode && PATH="$(TOOLS_DIR)/venv/bin:$$PATH" $(RENODE_TEST) \
		$(abspath tests/renode/inktime.robot) --variable FW_ELF:$(abspath $(FW_ELF))

size: $(FW_ELF)
	$(Q)python3 -I tools/check_size.py $(FW_SIZE) $(FW_ELF)

lint:
	$(Q)bash tools/lint.sh

CLANG_FORMAT ?= $(firstword $(wildcard $(TOOLS_DIR)/venv/bin/clang-format) clang-format)
format:
	$(Q)find src sim tests/unit -name '*.[ch]' | xargs $(CLANG_FORMAT) -i

# Flash over SWD (Tag-Connect J2) with any CMSIS-DAP / J-Link probe: pip install pyocd
flash: $(FW_ELF)
	pyocd load --target nrf52840 $(BUILD)/fw/inktime.hex

COV_DIR := $(BUILD)/coverage
coverage:
	$(Q)rm -rf $(COV_DIR) && mkdir -p $(COV_DIR)
	$(Q)$(HOST_CC) -std=c11 -O0 -g --coverage $(INCLUDES) -D_DEFAULT_SOURCE -DBOARD_REV=2 \
		$(HOST_SRC) $(UNIT_SRC) -o $(COV_DIR)/unit_tests_cov
	$(Q)cd $(COV_DIR) && ./unit_tests_cov > /dev/null
	$(Q)GCOVR=$$(command -v gcovr || echo $(TOOLS_DIR)/venv/bin/gcovr); \
	if [ -x "$$GCOVR" ]; then \
		$$GCOVR -r . --object-directory $(COV_DIR) --filter src/ --exclude src/hal/nrf52/ \
			--txt-summary --html-details $(COV_DIR)/index.html --fail-under-line 85; \
	else echo "gcovr not installed: raw .gcda files are in $(COV_DIR)"; fi

ci:
	$(Q)bash tools/ci.sh

run: $(SIM_BIN)
	$(SIM_BIN)

clean:
	rm -rf $(BUILD)

-include $(HOST_OBJ:.o=.d) $(UNIT_OBJ:.o=.d) $(SIM_OBJ:.o=.d) $(FW_OBJ:.o=.d)
