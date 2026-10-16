#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/../../../.." && pwd)"
build_dir="${TMPDIR:-$repo_root/.tmp}/warp-tesla-host-tests"
mbedtls_source="${TESLA_BLE_HOST_MBEDTLS_SOURCE:-/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/esp-idf/components/mbedtls/mbedtls}"
mbedtls_build="$build_dir/mbedtls"
name="${1:-test_tesla_ble_crypto_parser}"
[[ "$name" =~ ^test_[a-zA-Z0-9_]+$ ]] || { printf 'Unsafe test name\n' >&2; exit 2; }
python3 "$repo_root/software/tools/bench_confinement.py" outputs "$build_dir"
mkdir -p "$build_dir"
nanopb="$repo_root/software/.pio/libdeps/warp4_tesla_ble/Nanopb"
cmake -S "$mbedtls_source" -B "$mbedtls_build" -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF > "$build_dir/mbedtls-configure.log" 2>&1
cmake --build "$mbedtls_build" --target mbedcrypto -j4 > "$build_dir/mbedtls-build.log" 2>&1
lib="$repo_root/software/lib/TeslaBLE"
shift || true
extra_flags=(-fno-exceptions)
if [[ "$name" == test_tesla_ble_review7 ]]; then
    extra_flags+=(-Wl,--wrap=_ZNSt6chrono3_V212steady_clock3nowEv)
fi
if [[ "$name" == test_tesla_ble_mbedtls_faults ]]; then
    extra_flags+=(-Wl,--wrap=calloc,--wrap=free,--wrap=mbedtls_entropy_func)
fi
if [[ "$name" == test_tesla_ble_sync_initialization || "$name" == test_tesla_ble_runtime_sync ]]; then
    extra_flags+=(-D_GTHREAD_USE_MUTEX_INIT_FUNC=1 -Wl,--wrap=pthread_mutex_init,--wrap=pthread_mutex_destroy,--wrap=pthread_mutex_lock,--wrap=pthread_mutex_trylock,--wrap=pthread_mutex_unlock)
fi
if [[ "$name" == test_tesla_ble_bounded_storage ]]; then
    extra_flags+=(-Wl,--wrap=open,--wrap=fstat,--wrap=read,--wrap=close)
fi
if [[ "$name" == test_tesla_ble_checked_storage || "$name" == test_tesla_ble_strong_transaction || "$name" == test_tesla_ble_littlefs_close ]]; then
    extra_flags+=(-Wl,--wrap=open,--wrap=fstat,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=stat,--wrap=rename,--wrap=unlink)
fi
if [[ "$name" == test_tesla_ble_module_text || "$name" == test_tesla_ble_startup_allocations || "$name" == test_tesla_ble_module_composition || "$name" == test_tesla_ble_module_acceptance ]]; then
    extra_flags+=(-Wl,--wrap=calloc,--wrap=free,--wrap=mbedtls_entropy_func)
    for context in pk ecdh ctr_drbg entropy; do
        extra_flags+=(-Wl,--wrap=mbedtls_${context}_init,--wrap=mbedtls_${context}_free)
    done
fi

backend_objects=()
if [[ "$name" == test_tesla_ble_littlefs_close ]]; then
    python3 "$repo_root/software/tests/tesla_vehicle/host/build_littlefs_close_harness.py"
    backend_objects+=("$build_dir/littlefs-close/backend.o" "$build_dir/littlefs-close/lfs.o" "$build_dir/littlefs-close/lfs_util.o")
fi
g++ -std=c++20 -pthread -Wall -Wextra -Werror -Wno-unused-parameter "${extra_flags[@]}" \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I"$repo_root/software/tests/tesla_vehicle/host" \
    -I"$lib/include" -I"$lib/generated/include" -I"$nanopb" -I"$mbedtls_source/include" \
    "$repo_root/software/tests/tesla_vehicle/$name.cpp" \
    "$lib/src/client.cpp" "$lib/src/peer.cpp" "$lib/src/crypto_context.cpp" \
    "$lib/src/message_builders.cpp" "$lib/src/tb_utils.cpp" "$lib/src/tb_logging.cpp" \
    "$lib/src/errors.cpp" "$lib/src/vin_utils.cpp" "$lib/src/logging_bridge.cpp" \
    "$lib/src/vehicle.cpp" "$lib/src/message_processor.cpp" \
    "$lib"/generated/src/*.c "$lib"/generated/src/google/protobuf/*.c \
    "$nanopb/pb_common.c" "$nanopb/pb_encode.c" "$nanopb/pb_decode.c" \
    "$mbedtls_build/library/libmbedcrypto.a" \
    "$mbedtls_build/3rdparty/everest/libeverest.a" "$mbedtls_build/3rdparty/p256-m/libp256m.a" \
    "${backend_objects[@]}" -o "$build_dir/$name"
"$build_dir/$name" "$@"
