#!/usr/bin/env bash
# Static analysis for the firmware:
#   1. GCC -fanalyzer (interprocedural: leaks, NULL derefs, use-after-free, OOB...)
#      on the portable code (host gcc) and on the bare-metal HAL (arm-none-eabi-gcc)
#   2. cppcheck, if installed
#   3. clang-format check, if installed
set -u
cd "$(dirname "$0")/.."

TOOLS_DIR=${TOOLS_DIR:-$HOME/tools}
ARM_GCC=${ARM_GCC:-$TOOLS_DIR/arm-gnu/bin/arm-none-eabi-gcc}
command -v "$ARM_GCC" > /dev/null || ARM_GCC=arm-none-eabi-gcc

status=0
INC="-Isrc -Ithird_party/bosch"
HOST_SRC=$(find src sim -name '*.c' ! -path 'src/hal/nrf52/*' ! -name sim_gui.c | sort)
NRF_SRC="src/hal/nrf52/hal_nrf52.c src/hal/nrf52/startup.c src/main.c"

echo "== gcc -fanalyzer (portable code + simulator)"
for f in $HOST_SRC; do
    gcc -std=c11 -O1 -fanalyzer -Wall -Wextra -Werror $INC -D_DEFAULT_SOURCE -DBOARD_REV=2 -c "$f" -o /dev/null || status=1
done

echo "== arm-none-eabi-gcc -fanalyzer (bare-metal HAL)"
if command -v "$ARM_GCC" > /dev/null; then
    for f in $NRF_SRC; do
        "$ARM_GCC" -std=c11 -Os -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -fanalyzer \
            -Wall -Wextra -Werror $INC -Ithird_party/nrf_mdk -Ithird_party/cmsis/Include -DNRF52840_XXAA \
            -c "$f" -o /dev/null || status=1
    done
else
    echo "   (skipped: no arm-none-eabi-gcc)"
fi

echo "== cppcheck"
CPPCHECK=${CPPCHECK:-$(command -v cppcheck || echo "$TOOLS_DIR/cppcheck/usr/bin/cppcheck")}
if [ -x "$CPPCHECK" ]; then
    "$CPPCHECK" --quiet --std=c11 --enable=warning,style,performance,portability --inline-suppr \
        --error-exitcode=1 --suppress=missingIncludeSystem --suppress=unusedFunction \
        $INC -DNRF52840_XXAA src sim tests/unit || status=1
else
    echo "   (skipped: install with 'sudo apt install cppcheck')"
fi

echo "== clang-format"
# Prefer the pinned pip version from the tools venv: formatting differs between releases.
CLANG_FORMAT=${CLANG_FORMAT:-$([ -x "$TOOLS_DIR/venv/bin/clang-format" ] && echo "$TOOLS_DIR/venv/bin/clang-format" || command -v clang-format)}
if [ -x "$CLANG_FORMAT" ]; then
    find src sim tests/unit -name '*.[ch]' -print0 | xargs -0 "$CLANG_FORMAT" --dry-run --Werror || status=1
else
    echo "   (skipped: pip install clang-format)"
fi

if [ $status -eq 0 ]; then echo "lint: OK"; else echo "lint: FAILED"; fi
exit $status
