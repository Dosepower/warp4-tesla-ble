// NEW (psram-production port): production-config negative cases for the REAL tesla_psram.h.
// Built WITHOUT TESLA_WORKER_TEST_HOOKS: every failure here comes from a real production branch, not an injection hook.
// Host shim (stub/psram/tesla_psram_host_platform.h) = malloc + tail canary + zeroize checker; proves logic, NOT placement.
#define TESLA_PSRAM_PLACEMENT 1
#define TESLA_PSRAM_HOST_TEST 1
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "tesla_psram.h"
#if defined(TESLA_WORKER_TEST_HOOKS) && TESLA_WORKER_TEST_HOOKS
#error "negative suite must be built in production hook configuration (TESLA_WORKER_TEST_HOOKS unset)"
#endif
namespace tp = tesla_psram;
static int checks = 0, fails = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
struct Secret { unsigned char k[200]; static inline int dtors = 0; Secret() { std::memset(k, 0xA5, sizeof k); } ~Secret() { ++dtors; } };
int main(int argc, char **argv) {
    if (argc > 1 && std::strcmp(argv[1], "canary_red") == 0) { // RED control: B1-style wipe past the requested bytes
        auto *p = static_cast<unsigned char *>(tp::alloc(200, tp::Keys));
        std::memset(p, 0, 204); // destroys the tail canary at data+requested, as heap_caps_get_allocated_size wipe did
        tp::release(p);         // host shim must abort like heap_caps_free asserts on target
        std::puts("RED_NOT_DETECTED"); return 0;
    }
    // 1. real size-overflow failure branch (n > UINT32_MAX - HDR): nullptr, counted, no internal fallback, nothing live
    const uint32_t f0 = tp::stats.failures, a0 = tp::stats.allocs, l0 = tp::stats.live_bytes;
    void *big = tp::alloc(static_cast<size_t>(UINT32_MAX) - tp::HDR + 1, tp::Vehicle);
    CHECK(big == nullptr); CHECK(tp::stats.failures == f0 + 1); CHECK(tp::stats.allocs == a0);
    CHECK(tp::stats.live_bytes == l0); CHECK(tp::stats.failed_mask & (1u << tp::Vehicle));
    // 2. exact zeroize + canary intact on a >=128 B (rounded size class) block holding key-like bytes
    auto *k = static_cast<unsigned char *>(tp::alloc(200, tp::KeyMaterial));
    CHECK(k != nullptr); CHECK(tp::stats.live_bytes == l0 + 200);
    CHECK(tp::stats.placed_external & (1u << tp::KeyMaterial)); CHECK(tp::stats.obj_bytes[tp::KeyMaterial] == 200);
    for (int i = 0; i < 200; ++i) CHECK(k[i] == 0); // calloc semantics
    std::memset(k, 0x5A, 200);
    tp::release(k);
    CHECK(tp::plat::zeroized_nonzero == 0); CHECK(tp::plat::canary_failures == 0); CHECK(tp::stats.live_bytes == l0);
    // 3. Owned<T> lifecycle: one ctor, one dtor, one free, accounting back to baseline
    const uint32_t fr0 = tp::stats.frees;
    { tp::Owned<Secret> s(tp::create<Secret>(tp::Keys)); CHECK(s != nullptr); CHECK(s->k[199] == 0xA5);
      CHECK(tp::stats.live_bytes == l0 + sizeof(Secret)); }
    CHECK(Secret::dtors == 1); CHECK(tp::stats.frees == fr0 + 1); CHECK(tp::stats.live_bytes == l0);
    CHECK(tp::plat::zeroized_nonzero == 0); CHECK(tp::plat::canary_failures == 0);
    tp::release(nullptr); CHECK(tp::stats.frees == fr0 + 1); // null release is a no-op
    std::printf("psram_production_negative: checks=%d fails=%d\n", checks, fails);
    return fails ? 1 : 0;
}
