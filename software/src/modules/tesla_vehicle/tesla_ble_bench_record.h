#pragma once
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
#include <array>
#include <memory>
#include <new>
#include <mbedtls/platform_util.h>
// Bounded ordinary heap workspace (target allocator prefers PSRAM). Always
// checked before use and zeroized on every return, including partial failures.
// Never retain 544-byte plaintext records below filesystem/crypto call frames.
struct BenchPairRecord final {
    std::array<uint8_t,544> bytes{};
    ~BenchPairRecord() { mbedtls_platform_zeroize(bytes.data(),bytes.size()); }
};
#endif
