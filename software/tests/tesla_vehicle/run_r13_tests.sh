#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
out="${TMPDIR:?}/warp-r13"
export PYTHONDONTWRITEBYTECODE=1
preflight="$root/software/tests/tesla_vehicle/r13_output_preflight.py"
# Complete output/log/cleanup pathset BEFORE the first mkdir or redirection.
python3 -B "$preflight" "$out"
mkdir -p "$out"
mbed="${TESLA_BLE_HOST_MBEDTLS_SOURCE:-$out/mbedtls-source}"
libs="$out/native/mbedtls"
if [[ ! -f "$libs/library/libmbedcrypto.a" ]]; then
    python3 -B "$preflight" "$out"
    cmake -S "$mbed" -B "$libs" -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF > "$out/mbedtls-configure.log" 2>&1
    python3 -B "$preflight" "$out"
    cmake --build "$libs" --target mbedcrypto -j2 > "$out/mbedtls-build.log" 2>&1
fi
flags=(-std=c++17 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -pthread)
if [[ "${1:-}" == sanitize ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -no-pie); fi
if [[ "${1:-}" != baseline ]]; then flags+=(-DR13_API); fi
if [[ "${1:-}" == unsupported ]]; then flags+=(-DR13_UNSUPPORTED); fi
python3 -B "$preflight" "$out"
g++ "${flags[@]}" -DTESLA_BLE_MODULE_HOST_TEST -DTESLA_BLE_HOST_FILESYSTEM \
    -DBLUETOOTH_CONFIG_PATH=\"$out/module_config\" \
    -I"$root/software/tests/tesla_vehicle/host" -I"$root/software/lib/TeslaBLEClean/include" -I"$mbed/include" \
    "$root/software/tests/tesla_vehicle/test_r13_module.cpp" \
    "$root/software/src/modules/tesla_vehicle/tesla_ble_storage.cpp" \
    "$root/software/src/modules/tesla_vehicle/tesla_ble_transport.cpp" \
    "$root/software/src/modules/tesla_vehicle/tesla_ble_gatt.cpp" \
    "$root/software/src/modules/tesla_vehicle/bluetooth_lifecycle.cpp" \
    "$root"/software/lib/TeslaBLEClean/src/*.cpp \
    "$libs/library/libmbedcrypto.a" "$libs/3rdparty/everest/libeverest.a" "$libs/3rdparty/p256-m/libp256m.a" \
    -Wl,--wrap=open,--wrap=write,--wrap=read,--wrap=fsync,--wrap=close,--wrap=rename,--wrap=unlink \
    -o "$out/module"
python3 -B "$preflight" "$out" --cleanup
if [[ "${1:-}" == unsupported ]]; then "$out/module" unsupported; exit; fi
"$out/module" default
if [[ "${1:-}" == baseline ]]; then exit; fi
for fault in open write short read fsync close rename remove rollback; do
    R13_FAULT="$fault" "$out/module" persist-fail
done
"$out/module" save
"$out/module" forget-disabled
"$out/module" boot
