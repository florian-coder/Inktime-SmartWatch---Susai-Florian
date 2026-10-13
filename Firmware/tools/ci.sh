#!/usr/bin/env bash
# Full test pipeline, in the same order as the GitHub workflow.
# Every stage runs even if an earlier one failed; the summary shows what broke.
set -u
cd "$(dirname "$0")/.."

TOOLS_DIR=${TOOLS_DIR:-$HOME/tools}
export TOOLS_DIR
JOBS=$(nproc 2>/dev/null || echo 4)

declare -a NAMES RESULTS TIMES
run_stage() {
    local name=$1
    shift
    echo
    echo "================================================================ $name"
    local t0=$SECONDS
    if "$@"; then RESULTS+=("PASS"); else RESULTS+=("FAIL"); fi
    NAMES+=("$name")
    TIMES+=($((SECONDS - t0)))
}

have_renode() { [ -x "$TOOLS_DIR/renode/renode-test" ] || command -v renode-test > /dev/null; }

run_stage "1. host build (-Werror, ASan+UBSan)" make -j"$JOBS" sim build/host/unit_tests
run_stage "2. unit + integration tests" make test
run_stage "3. scenario tests (golden images)" make scenarios
run_stage "4. firmware build (arm-none-eabi)" make -j"$JOBS" firmware
run_stage "5. flash/RAM budget" make size
run_stage "6. static analysis" make lint
if have_renode; then
    run_stage "7. Renode: ARM firmware on emulated nRF52840" make renode-test
else
    echo "Renode not found: run tools/setup_tools.sh (stage 7 skipped)"
fi
run_stage "8. coverage" make coverage

echo
echo "================================================================ summary"
fail=0
for i in "${!NAMES[@]}"; do
    printf "  %-4s %-50s %4ss\n" "${RESULTS[$i]}" "${NAMES[$i]}" "${TIMES[$i]}"
    [ "${RESULTS[$i]}" = "PASS" ] || fail=1
done
echo "  reports: build/reports/  coverage: build/coverage/index.html"
exit $fail
