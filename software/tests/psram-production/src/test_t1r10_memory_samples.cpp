// T1r10 HOST TEST: real src bluetooth_memory.h + real tesla_psram.h (lab mode, simulated PSRAM heap).
// Proves publication/locking/accounting/fail-closed logic. It does NOT prove ESP32 placement (hardware readback does).
// Modes run as separate processes because the table is intentionally process-lifetime (never freed):
//   noinit  - sampling/reading before init(): dropped+counted, no heap query, no allocation, reads unavailable
//   fail    - injected allocator failure: fail closed (no internal fallback), sticky failed state, drops counted
//   ready   - init once, accounting (880 B, kind 11), bounds, accumulation, interval, json, no per-call alloc, concurrency
//   race    - samplers/readers running BEFORE and during publication + concurrent init() callers
#define WARP_TESLA_MEMORY_PHASES 1
#define TESLA_PSRAM_PLACEMENT 1
#define TESLA_PSRAM_HOST_TEST 1
#define TESLA_MEMORY_HOST_TEST 1
#include "bluetooth_memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
namespace bm = bluetooth_memory; namespace tp = tesla_psram;
static constexpr unsigned N = static_cast<unsigned>(bm::Stage::Count);
static constexpr uint32_t BIT = 1u << tp::MemorySamples;

static bool consistent(const bm::Sample &s) { // all scalar fields come from one sampler token
    return s.ms == s.free_internal && s.ms == s.min_internal && s.ms == s.largest_internal && s.ms == s.free_dma &&
           s.ms == s.largest_dma && s.ms == s.free_psram && s.ms == s.largest_psram;
}

static void mode_noinit() {
    CHECK(bm::status().state == bm::TableUninitialized && !bm::status().ready && bm::status().bytes == 0);
    const uint32_t q0 = memory_heap_queries.load(), a0 = tp::stats.allocs.load();
    memory_test_token = 7;
    for (unsigned i = 0; i < N; ++i) bm::sample(static_cast<bm::Stage>(i), true, i & 1 ? 1000 : 0);
    CHECK(bm::dropped.load() == N);
    CHECK(memory_heap_queries.load() == q0);     // unavailable table: no heap/stack query cost
    CHECK(tp::stats.allocs.load() == a0);        // samplers never allocate (no lazy alloc on hot path)
    bm::Sample s; CHECK(!bm::read(bm::Stage::Boot, s));
    char buf[512]; CHECK(!bm::json(bm::Stage::Boot, buf, sizeof buf));
    CHECK(bm::table.load() == nullptr);
    CHECK((tp::stats.placed_seen.load() & BIT) == 0);
}

static void mode_fail() {
    tp::inject_failures.store(1);
    CHECK(!bm::init());
    const auto st = bm::status();
    CHECK(st.state == bm::TableFailed && !st.ready && !st.external && st.bytes == 0);
    CHECK(std::strcmp(bm::state_name(st.state), "failed") == 0);
    CHECK(tp::stats.failures.load() == 1 && (tp::stats.failed_mask.load() & BIT));
    CHECK(tp::stats.allocs.load() == 0 && tp::stats.live_bytes.load() == 0); // no internal fallback allocation
    CHECK(!bm::init());                          // sticky: no retry, no second attempt
    CHECK(tp::stats.failures.load() == 1);
    bm::sample(bm::Stage::Boot); bm::sample(bm::Stage::GapCallback, true, 1000);
    CHECK(bm::dropped.load() == 2);
    bm::Sample s; CHECK(!bm::read(bm::Stage::Boot, s));
}

static void mode_ready() {
    const uint32_t q_before = memory_heap_queries.load();
    CHECK(bm::init());
    CHECK(memory_heap_queries.load() == q_before);
    auto st = bm::status();
    CHECK(st.state == bm::TableReady && st.ready && st.bytes == 880 && st.dropped == 0);
    // Host sim tags only the allocator's block base (tesla_psram_host_platform.h), so status().external (which tests the
    // table address itself, like esp_ptr_external_ram on target) is not provable on host. Check the owning block instead;
    // the actual placement proof is the target readback (/bluetooth/memory table.external + fixture mask bit 11).
    CHECK(tp::plat::is_external(reinterpret_cast<unsigned char *>(bm::table.load()) - tp::HDR));
    CHECK(tp::stats.allocs.load() == 1 && tp::stats.live_bytes.load() == 880 && tp::stats.obj_bytes[tp::MemorySamples].load() == 880);
    CHECK((tp::stats.placed_seen.load() & BIT) && (tp::stats.placed_external.load() & BIT) && tp::stats.non_external.load() == 0);
    CHECK(std::strcmp(tp::obj_name(tp::MemorySamples), "memory_samples") == 0 && std::strcmp(tp::obj_name(tp::Count), "invalid") == 0);
    CHECK(bm::init() && tp::stats.allocs.load() == 1); // idempotent, no second block
    bm::Sample s;
    for (unsigned i = 0; i < N; ++i) { CHECK(bm::read(static_cast<bm::Stage>(i), s) && s.samples == 0 && s.ms == 0); }
    // bounds
    CHECK(!bm::read(bm::Stage::Count, s) && !bm::read(static_cast<bm::Stage>(200), s));
    bm::sample(bm::Stage::Count); bm::sample(static_cast<bm::Stage>(255), true, 5);
    for (unsigned i = 0; i < N; ++i) { CHECK(bm::read(static_cast<bm::Stage>(i), s) && s.samples == 0); }
    char buf[512]; CHECK(!bm::json(bm::Stage::Count, buf, sizeof buf));
    CHECK(std::strcmp(bm::name(static_cast<bm::Stage>(99)), "invalid") == 0);
    // accumulation + stack minimum
    memory_test_token = 100; memory_test_stack = 900; bm::sample(bm::Stage::WorkerActive, true);
    memory_test_token = 200; memory_test_stack = 500; bm::sample(bm::Stage::WorkerActive, true);
    memory_test_token = 300; memory_test_stack = 800; bm::sample(bm::Stage::WorkerActive, true);
    CHECK(bm::read(bm::Stage::WorkerActive, s) && s.samples == 3 && s.ms == 300 && s.stack_min == 500 && consistent(s));
    memory_test_token = 400; bm::sample(bm::Stage::WorkerActive, false);
    CHECK(bm::read(bm::Stage::WorkerActive, s) && s.samples == 4 && s.stack_min == 0); // non-stack sample, as T1r9
    // interval throttling
    memory_test_token = 1000; bm::sample(bm::Stage::GapCallback, true, 1000);
    memory_test_token = 1500; bm::sample(bm::Stage::GapCallback, true, 1000);
    CHECK(bm::read(bm::Stage::GapCallback, s) && s.samples == 1 && s.ms == 1000);
    memory_test_token = 2000; bm::sample(bm::Stage::GapCallback, true, 1000);
    CHECK(bm::read(bm::Stage::GapCallback, s) && s.samples == 2 && s.ms == 2000);
    // json + truncation never reported as success
    CHECK(bm::json(bm::Stage::WorkerActive, buf, sizeof buf));
    CHECK(std::string(buf).find("\"stage\":\"worker_active\",\"samples\":4,\"ms\":400") != std::string::npos);
    char tiny[32]; CHECK(!bm::json(bm::Stage::WorkerActive, tiny, sizeof tiny));
    // no allocation/free per sample or read
    const uint32_t a0 = tp::stats.allocs.load(), f0 = tp::stats.frees.load();
    for (int k = 0; k < 10000; ++k) { memory_test_token = 5000 + k; bm::sample(static_cast<bm::Stage>(k % N), k & 1); bm::read(static_cast<bm::Stage>(k % N), s); }
    CHECK(tp::stats.allocs.load() == a0 && tp::stats.frees.load() == f0 && tp::stats.live_bytes.load() == 880);
    CHECK(bm::dropped.load() == 0);
    // concurrency: samplers + readers, no torn sample, no lost increments
    uint32_t base[N]; for (unsigned i = 0; i < N; ++i) { bm::read(static_cast<bm::Stage>(i), s); base[i] = s.samples; }
    constexpr int T = 6, K = 20000; std::atomic<bool> stop{false}; std::atomic<unsigned> torn{0}, regress{0};
    std::vector<std::thread> th;
    for (int r = 0; r < 2; ++r) th.emplace_back([&] { uint32_t last[N] = {}; bm::Sample x;
        while (!stop.load()) for (unsigned i = 0; i < N; ++i) if (bm::read(static_cast<bm::Stage>(i), x)) {
            if (x.samples && !consistent(x)) { torn++; } if (x.samples < last[i]) { regress++; } last[i] = x.samples; } });
    std::vector<std::thread> w;
    for (int t = 0; t < T; ++t) w.emplace_back([t] { for (int k = 0; k < K; ++k) { memory_test_token = 1000000u * (t + 1) + k;
        memory_test_stack = 100 + t; bm::sample(static_cast<bm::Stage>((k + t) % N), true); } });
    for (auto &x : w) { x.join(); } stop = true; for (auto &x : th) { x.join(); }
    uint64_t total = 0; for (unsigned i = 0; i < N; ++i) { CHECK(bm::read(static_cast<bm::Stage>(i), s) && consistent(s)); total += s.samples - base[i]; }
    CHECK(total == uint64_t(T) * K); CHECK(torn.load() == 0); CHECK(regress.load() == 0);
    CHECK(tp::stats.allocs.load() == 1 && tp::stats.frees.load() == 0);
    st = bm::status(); CHECK(st.ready && st.dropped == 0);
}

static void mode_race() {
    constexpr int T = 4, K = 30000; std::atomic<bool> go{false}; std::atomic<unsigned> torn{0}, reads_ok{0};
    std::vector<std::thread> w, r, in;
    for (int t = 0; t < T; ++t) w.emplace_back([&, t] { while (!go.load()) {} for (int k = 0; k < K; ++k) {
        memory_test_token = 1000000u * (t + 1) + k; bm::sample(static_cast<bm::Stage>(k % N), k & 1); } });
    for (int t = 0; t < 2; ++t) r.emplace_back([&] { bm::Sample x; while (!go.load()) {} for (int k = 0; k < K; ++k)
        if (bm::read(static_cast<bm::Stage>(k % N), x)) { reads_ok++; if (x.samples && !consistent(x)) torn++; } });
    for (int t = 0; t < 3; ++t) in.emplace_back([&] { while (!go.load()) {} std::this_thread::yield(); bm::init(); });
    go = true;
    for (auto &x : in) { x.join(); } for (auto &x : w) { x.join(); } for (auto &x : r) { x.join(); }
    CHECK(tp::stats.allocs.load() == 1 && tp::stats.failures.load() == 0); // exactly one block despite 3 initializers
    CHECK(bm::status().state == bm::TableReady && bm::status().bytes == 880);
    bm::Sample s; uint64_t stored = 0;
    for (unsigned i = 0; i < N; ++i) { CHECK(bm::read(static_cast<bm::Stage>(i), s)); if (s.samples) CHECK(consistent(s)); stored += s.samples; }
    CHECK(stored + bm::dropped.load() == uint64_t(T) * K); // every call either stored or counted as dropped
    CHECK(torn.load() == 0);
    std::printf("race: stored=%llu dropped=%u reads_ok=%u\n", (unsigned long long)stored, bm::dropped.load(), reads_ok.load());
}

int main(int argc, char **argv) {
    const std::string m = argc > 1 ? argv[1] : "";
    if (m == "noinit") mode_noinit(); else if (m == "fail") mode_fail(); else if (m == "ready") mode_ready();
    else if (m == "race") mode_race(); else { std::fprintf(stderr, "usage: noinit|fail|ready|race\n"); return 2; }
    CHECK(tp::plat::canary_failures.load() == 0);
    std::printf("%s %s failures=%d\n", failures ? "FAIL" : "PASS", m.c_str(), failures);
    return failures ? 1 : 0;
}
