#pragma once
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
#include <atomic>
// Boot-lifetime exclusion: choose unchanged stack-smoke OR discovery + probe.
// Never release this latch, even after failed task creation or clean teardown.
namespace tesla_bench {
inline std::atomic<unsigned> owner{0};
inline bool claim(unsigned action) { unsigned expected = 0; return owner.compare_exchange_strong(expected, action); }
}
#endif
