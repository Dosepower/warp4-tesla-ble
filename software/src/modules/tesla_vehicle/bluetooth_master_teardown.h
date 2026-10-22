#pragma once
#if (defined(TESLA_BLE_R13_MASTER_BENCH) && TESLA_BLE_R13_MASTER_BENCH) || (defined(TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC) && TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC)
#if !defined(TESLA_BLE_BENCH_ONLY) || !TESLA_BLE_BENCH_ONLY || !defined(TESLA_BLE_IRAM_DIAGNOSTICS) || !TESLA_BLE_IRAM_DIAGNOSTICS
#error "Master teardown observation is bench diagnostics only"
#endif
#include <atomic>
#include <cstdint>
// Passive observation window: wrappers return the SDK result unchanged. Nothing
// calls or retries an SDK operation here. Missing, duplicate or failed calls
// cannot authorize a reboot, even if Arduino's void deinit hides their status.
namespace bluetooth_master_teardown {
inline std::atomic<bool> armed{false}, bad{false};
inline std::atomic<bool> forbidden{false}; // latched until whole-chip reboot
inline std::atomic<uint32_t> seen{0};
inline void begin() { seen.store(0); bad.store(false); armed.store(true); }
inline int observe(uint32_t bit, int rc) {
    if (armed.load()) {
        if ((seen.fetch_or(bit) & bit) || rc != 0) { bad.store(true); }
    }
    return rc;
}
inline bool finish(uint32_t expected) {
    armed.store(false);
    return !bad.load() && seen.load() == expected;
}
}
#endif
