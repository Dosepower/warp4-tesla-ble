#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Builds only clean core sources, never the legacy TeslaBLE implementation.
set -euo pipefail
root="$(cd "$(dirname "$0")/../../../.." && pwd)"
out="${TESLA_CLEAN_TEST_OUT:-${TMPDIR:?}/tesla-clean-native}"
mkdir -p "$out"
mbed="/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/esp-idf/components/mbedtls/mbedtls"
libs="${TESLA_CLEAN_MBEDTLS_BUILD:-$out/mbedtls}"
if [[ ! -f "$libs/library/libmbedcrypto.a" ]]; then
    cmake -S "$mbed" -B "$libs" -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF > "$out/mbedtls-configure.log" 2>&1
    cmake --build "$libs" --target mbedcrypto -j4 > "$out/mbedtls-build.log" 2>&1
fi
name="${1:?test name}"
if [[ "$name" == test_warp_target_text || "$name" == test_warp_target_validation ]]; then
    export TESLA_CLEAN_TARGET_OOM_TEST="$root/software/tests/tesla_vehicle/cleanroom/test_warp_target_text.cpp"
    export TESLA_CLEAN_VALIDATION_TEST="$root/software/tests/tesla_vehicle/cleanroom/test_warp_target_validation.cpp"
    export TESLA_CLEAN_TEST_OUT="$out/$name${2:+-$2}"
    export TESLA_CLEAN_HARNESS_SANITIZE="$([[ "${2:-}" != sanitize ]] || printf 1)"
    if [[ "$name" == test_warp_target_text ]]; then
        python3 "$root/software/tests/tesla_vehicle/host/build_target_oom_harness.py"
    else
        python3 "$root/software/tests/tesla_vehicle/host/build_module_validation_harness.py"
    fi
    exit 0
fi
flags=(-std=c++17 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -I"$root/software/tests/tesla_vehicle/cleanroom/target_ports"
       -I"$root/software/tests/tesla_vehicle/cleanroom")
suffix=""
if [[ "$name" == test_faults || "$name" == test_lifecycle_faults || "$name" == test_vehicle_faults || "$name" == test_astra_faults ]]; then
    flags+=(-Wl,--wrap=calloc,--wrap=free)
fi
if [[ "${2:-}" == sanitize ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -no-pie)
    suffix="-sanitized"
fi
sources=()
if [[ "$name" == test_warp_adapters || "$name" == test_warp_storage_faults || "$name" == test_warp_littlefs ]]; then
    sources+=("$root/software/src/modules/tesla_vehicle/tesla_ble_storage.cpp")
    flags+=("-DTESLA_CLEAN_STORAGE_PREFIX=\"$out/storage\"" -pthread)
fi
if [[ "$name" == test_warp_storage_faults || "$name" == test_warp_littlefs ]]; then
    flags+=(-DTESLA_BLE_CLOSE_QUARANTINE_TEST
            -Wl,--wrap=open,--wrap=fstat,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=stat,--wrap=rename,--wrap=unlink)
fi
if [[ "$name" == test_warp_littlefs ]]; then
    flags+=(-UTESLA_CLEAN_STORAGE_PREFIX)
    TESLA_CLEAN_TEST_OUT="$out" TESLA_CLEAN_HARNESS_SANITIZE="$([[ "${2:-}" != sanitize ]] || printf 1)" \
        python3 "$root/software/tests/tesla_vehicle/host/build_littlefs_close_harness.py"
    sources+=("$out/littlefs-close/backend.o" "$out/littlefs-close/lfs.o" "$out/littlefs-close/lfs_util.o")
fi
if [[ "$name" == test_warp_module ]]; then
    sources+=("$root/software/src/modules/tesla_vehicle/tesla_ble_storage.cpp"
              "$root/software/src/modules/tesla_vehicle/tesla_ble_transport.cpp"
              "$root/software/src/modules/tesla_vehicle/tesla_ble_gatt.cpp")
    flags+=(-DTESLA_BLE_MODULE_HOST_TEST -DTESLA_BLE_HOST_FILESYSTEM -pthread
            -I"$root/software/tests/tesla_vehicle/host")
fi
for file in "$root"/software/lib/TeslaBLEClean/src/*.cpp; do
    [[ ! -f "$file" ]] || sources+=("$file")
done
if ! g++ -H "${flags[@]}" -I"$root/software/lib/TeslaBLEClean/include" -I"$mbed/include" \
    "$root/software/tests/tesla_vehicle/cleanroom/$name.cpp" "${sources[@]}" \
    "$libs/library/libmbedcrypto.a" "$libs/3rdparty/everest/libeverest.a" \
    "$libs/3rdparty/p256-m/libp256m.a" -o "$out/$name$suffix" 2> "$out/$name$suffix.includes"; then
    python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).read_text())' "$out/$name$suffix.includes"
    exit 1
fi
if [[ "$name" == test_warp_module ]]; then
    "$out/$name$suffix" "${3:-pair}"
else
    "$out/$name$suffix"
fi
