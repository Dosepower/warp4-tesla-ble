#!/usr/bin/env bash
# Source-only reproduction notes executed only with caller-provided local inputs.
# No fetch/upload/flash/signing/OTA/release commands. Use a disposable copy of the
# allowlisted source and packages; normal upstream hooks can export build/latest.
set -euo pipefail
: "${R7_SOURCE_BUILDER:?pinned patched local lib-builder inputs}"
: "${R7_IDF:?pinned patched IDF 2553c5ad432927e09ccbbad3b954e1cbc4bd7655}"
: "${R7_BASE_CONFIG:?retained full Bluetooth normal sdkconfig}"
: "${R7_DEST:?fresh absolute software-only SDK work directory}"
: "${R7_STRIP:?absolute Xtensa strip executable}"
: "${R7_DISPOSABLE_COPY:?must equal yes in a disposable source/package workspace}"
[[ "$R7_DISPOSABLE_COPY" == yes ]] || exit 2
export IDF_COMPONENT_MANAGER=0 IDF_PATH="$R7_IDF" UV_OFFLINE=1
. "$IDF_PATH/export.sh"
python3 software/lib-builder/prepare_warp4_bench.py --source "$R7_SOURCE_BUILDER" --idf "$R7_IDF" --base-config "$R7_BASE_CONFIG" --destination "$R7_DEST"
idf.py -C "$R7_DEST" -B "$R7_DEST/build" -D "EXTRA_COMPONENT_DIRS=$R7_DEST/managed_components/espressif__mdns;$R7_DEST/managed_components/joltwallet__littlefs;$R7_DEST/managed_components/espressif__libsodium" reconfigure
python3 software/lib-builder/prepare_warp4_bench.py --destination "$R7_DEST" --begin-build
cmake --build "$R7_DEST/build" --target idf-libs copy-bootloader -- -j4
python3 software/lib-builder/prepare_warp4_bench.py --destination "$R7_DEST" --finalize --strip "$R7_STRIP"
export TESLA_BLE_BENCH_BUILDER="$R7_DEST"
export TESLA_BLE_BENCH_FRAMEWORK_LIBS="$R7_DEST/out/tools/esp32-arduino-libs"
uv run --offline --directory software pio run -e warp4_tesla_ble_iram_bench -t clean
uv run --offline --directory software pio run -e warp4_tesla_ble_iram_bench -j4
# This generates diagnostic, NON-INSTALLABLE review bytes, not a Tesla-runtime
# production candidate. Preserve the generated identity proof and report.
