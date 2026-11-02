// Host regression for tesla_psram.h (T1r8 LAB): placement accounting, zeroize-on-free, failure injection,
// Owned<> deleter destructor order, class-scoped operators, tesla_clean::Allocation hook + CheckedCallback.
#define TESLA_PSRAM_PLACEMENT 1
#define TESLA_PSRAM_HOST_TEST 1
#include <atomic>
#include "tesla_psram.h"
#include <cassert>
#include <cstring> // port: legacy lab header pulled this in transitively
#include <cstdio>
static int dtors = 0;
struct Secret { unsigned char key[64]; explicit Secret(unsigned char v) { std::memset(key, v, sizeof key); } ~Secret() { ++dtors; } };
struct Agg { TESLA_PSRAM_OPERATORS(Runtime) std::atomic<uint32_t> a{1}; char big[3000]{}; ~Agg() { ++dtors; } };
int main() {
    namespace tp = tesla_psram; auto &s = tp::stats;
    { tp::Owned<Secret> k(tp::create<Secret>(tp::Keys, (unsigned char)0xA5));
      assert(k && s.allocs == 1 && (s.placed_external & (1u << tp::Keys)) && s.obj_bytes[tp::Keys] == sizeof(Secret)); }
    assert(dtors == 1 && s.frees == 1 && s.live_bytes == 0 && tp::plat::zeroized_nonzero == 0);
    // class-scoped nothrow new / delete via unique_ptr default_delete (as Runtime/Tesla)
    { std::unique_ptr<Agg> r(new (std::nothrow) Agg); assert(r && (s.placed_external & (1u << tp::Runtime)));
      r->a.fetch_add(1); assert(r->a == 2 && s.live_bytes == sizeof(Agg)); }
    assert(dtors == 2 && s.live_bytes == 0 && s.peak_bytes >= sizeof(Agg));
    // failure injection: nullptr, no internal fallback, counted, nothing live
    tp::inject_failures = 1;
    { std::unique_ptr<Agg> r(new (std::nothrow) Agg); assert(!r && s.failures == 1 && (s.failed_mask & (1u << tp::Runtime))); }
    { tp::Owned<Secret> k(tp::create<Secret>(tp::Keys, (unsigned char)1)); assert(k); } // next succeeds
    // Allocation hook + CheckedCallback (Callback kind) release through zeroizing release
    { tesla_clean::CheckedCallback<int(int)> cb; int captured = 41;
      assert(cb.assign([captured](int x) noexcept { return captured + x; }, tp::allocation(tp::Callback)));
      assert(cb(1) == 42 && (s.placed_external & (1u << tp::Callback))); }
    tp::inject_failures = 1;
    { tesla_clean::CheckedCallback<int()> cb; assert(!cb.assign([]() noexcept { return 1; }, tp::allocation(tp::Callback)) && !cb); }
    // B1 regression: 4 KiB key scratch (as private_key_presence) and a >128 B aggregate must free without wiping the
    // poisoned-heap tail canary (free_ext aborts otherwise); payload must be zero right before free.
    { auto *k = static_cast<unsigned char *>(tp::alloc(4096, tp::KeyScratch)); assert(k && s.obj_bytes[tp::KeyScratch] == 4096);
      std::memset(k, 0x5A, 4096); tp::release(k); }
    { std::unique_ptr<Agg> r(new (std::nothrow) Agg); assert(r); std::memset(r->big, 0x33, sizeof r->big); }
    assert(s.live_bytes == 0 && s.allocs == s.frees && s.non_external == 0 && tp::plat::zeroized_nonzero == 0 && tp::plat::canary_failures == 0);
    std::printf("tesla_psram host: OK allocs=%u frees=%u failures=%u peak=%u\n", (unsigned)s.allocs, (unsigned)s.frees, (unsigned)s.failures, (unsigned)s.peak_bytes);
    return 0;
}
