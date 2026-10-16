#!/usr/bin/env bash
# Software builds only. No hardware, upload, signing, OTA or release operations.
set -u -o pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT" || exit 1
E="$ROOT/software/.pio/bench-evidence/astra-fixes"
# Validate ALL exports, SDK cleanup roots, evidence/logs and ancestors before
# the first mutation, rather than validating one leaf immediately before use.
python3 software/tools/bench_confinement.py driver-init "$ROOT/software" || exit $?
run_logged() {
    local log="$1"; shift
    python3 software/tools/bench_confinement.py driver-preflight "$ROOT/software" || return $?
    printf 'COMMAND:' > "$log"
    printf ' %q' "$@" >> "$log"
    printf '\n' >> "$log"
    "$@" >> "$log" 2>&1
    local rc=$?
    printf '\nSHELL_EXIT_CODE=%s\n' "$rc" >> "$log"
    printf '%s: SHELL_EXIT_CODE=%s\n' "$log" "$rc"
    return "$rc"
}
export IDF_COMPONENT_MANAGER=0
export IDF_PATH="$ROOT/software/.pio/bench-idf"
. "$IDF_PATH/export.sh" > "$E/idf-export.log" 2>&1 || exit $?
for variant in diag minimal; do
    d="$ROOT/software/.pio/warp4-bench-$variant-sdk"
    # Only these isolated build outputs are removed. Retain all builder inputs.
    run_logged "$E/$variant-sdk-clean.log" python3 software/tools/bench_confinement.py sdk-clean "$ROOT/software" "$d" || exit $?
    run_logged "$E/$variant-sdk-configure.log" idf.py -C "$d" -B "$d/build" -D "EXTRA_COMPONENT_DIRS=$d/managed_components/espressif__mdns;$d/managed_components/joltwallet__littlefs;$d/managed_components/espressif__libsodium" reconfigure || exit $?
    run_logged "$E/$variant-sdk-begin.log" python3 software/lib-builder/prepare_warp4_bench.py --destination "$d" --begin-build || exit $?
    run_logged "$E/$variant-sdk-build.log" cmake --build "$d/build" --target idf-libs copy-bootloader -- -j4 || exit $?
    run_logged "$E/$variant-sdk-finalize.log" python3 software/lib-builder/prepare_warp4_bench.py --destination "$d" --finalize --strip /home/hermes/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-strip || exit $?
done
export TESLA_BLE_BENCH_BUILDER="$ROOT/software/.pio/warp4-bench-diag-sdk"
export TESLA_BLE_BENCH_MINIMAL_BUILDER="$ROOT/software/.pio/warp4-bench-minimal-sdk"
export TESLA_BLE_BENCH_FRAMEWORK_LIBS="$TESLA_BLE_BENCH_BUILDER/out/tools/esp32-arduino-libs"
export TESLA_BLE_BENCH_MINIMAL_FRAMEWORK_LIBS="$TESLA_BLE_BENCH_MINIMAL_BUILDER/out/tools/esp32-arduino-libs"
for variant in ordinary minimal; do
    environment=warp4_tesla_ble_iram_bench
    if [ "$variant" = minimal ]; then environment=${environment}_minimal; fi
    run_logged "$E/$variant-firmware-clean.log" uv run --offline --directory software pio run -e "$environment" -t clean || exit $?
    run_logged "$E/$variant-firmware-build.log" uv run --offline --directory software pio run -e "$environment" -j 4 || exit $?
done
