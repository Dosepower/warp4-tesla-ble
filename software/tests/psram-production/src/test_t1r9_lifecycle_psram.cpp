// T1r9 host regression: BluetoothLifecycle::Context-shaped aggregate with TESLA_PSRAM_OPERATORS(Lifecycle).
// Mirrors the real start()/~BluetoothLifecycle ownership: unique_ptr<Context>.reset(new(std::nothrow) Context),
// failure -> latched + nothing live, normal destroy -> dtor + zeroize(requested+hdr) + free (tail canary intact, B1),
// owner-still-running path -> context.release() (no free, no double free), later reclaimed exactly once.
// HOST ONLY: proves ownership/accounting/zeroize logic, NOT ESP32 placement.
#define TESLA_PSRAM_PLACEMENT 1
#define TESLA_PSRAM_HOST_TEST 1
#include <atomic>
#include "tesla_psram.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
namespace tp = tesla_psram;
static int dtors = 0;
struct Ctx { // shape of BluetoothLifecycle::Context: atomics + ~968 B NimBLE central state
    TESLA_PSRAM_OPERATORS(Lifecycle)
    enum Phase { Starting, Active, Stopping, Stopped, Failed };
    std::atomic<Phase> phase{Starting}; std::atomic<bool> stop{false}, error{false}, done{false}, host_done{false};
    std::atomic<uint32_t> since{0}; void *task = nullptr, *host_task = nullptr;
    struct Central { uint64_t epoch = 0; unsigned char state[952]{}; } central; bool acquired = false;
    ~Ctx() { ++dtors; }
};
static std::atomic<Ctx *> owner{nullptr};
struct Facade { // reduced BluetoothLifecycle start/stop/destructor ownership logic
    std::unique_ptr<Ctx> context; bool latched = false;
    bool start(bool task_ok) {
        if (context || latched) return false;
        context.reset(new (std::nothrow) Ctx);
        if (!context) { latched = true; return false; }
        Ctx *expected = nullptr; if (!owner.compare_exchange_strong(expected, context.get())) { latched = true; return false; }
        if (!task_ok) { latched = true; context->task = nullptr; owner = nullptr; return false; }
        context->task = reinterpret_cast<void *>(1); return true;
    }
    ~Facade() {
        if (!context) return;
        context->stop = true;
        if ((context->task && !context->done.load()) || owner.load() == context.get()) { context.release(); return; }
    }
};
int main() {
    const size_t n = sizeof(Ctx);
    // 1) normal lifecycle: allocate external, task finishes, owner cleared, facade destroyed -> one matched free
    for (int cycle = 0; cycle < 200; ++cycle) {
        Facade f; assert(f.start(true));
        Ctx *raw = f.context.get();
        assert(tp::plat::is_external(reinterpret_cast<unsigned char *>(raw) - tp::HDR));
        assert(tp::stats.live_bytes.load() == n);
        std::memset(raw->central.state, 0xA5, sizeof(raw->central.state)); // "secret-ish" state must be wiped
        raw->done = true; raw->phase = Ctx::Stopped; owner = nullptr;
    }
    assert(dtors == 200 && tp::stats.allocs.load() == 200 && tp::stats.frees.load() == 200 && tp::stats.live_bytes.load() == 0);
    assert(tp::stats.peak_bytes.load() == n);
    assert((tp::stats.placed_external.load() >> tp::Lifecycle) & 1u);
    assert(tp::stats.obj_bytes[tp::Lifecycle].load() == n);
    assert(tp::plat::canary_failures.load() == 0 && tp::plat::zeroized_nonzero.load() == 0);
    // 2) allocation failure: nullptr, latched, no internal fallback, nothing live, failed mask bit
    { tp::inject_failures = 1; Facade f; assert(!f.start(true)); assert(f.latched && !f.context);
      assert(!f.start(true)); /* latched: no retry */ }
    assert(tp::stats.failures.load() == 1 && ((tp::stats.failed_mask.load() >> tp::Lifecycle) & 1u));
    assert(tp::stats.live_bytes.load() == 0 && tp::stats.allocs.load() == 200);
    // 3) task-create failure after successful allocation: owner reset, context freed exactly once
    { Facade f; assert(!f.start(false)); assert(f.latched && f.context && owner.load() == nullptr); }
    assert(tp::stats.frees.load() == 201 && tp::stats.live_bytes.load() == 0);
    // 4) worker still running at destruction: ownership released to the task (no free), reclaimed once later
    Ctx *leaked = nullptr;
    { Facade f; assert(f.start(true)); leaked = f.context.get(); }
    assert(tp::stats.live_bytes.load() == n && tp::stats.frees.load() == 201 && owner.load() == leaked);
    owner = nullptr; delete leaked; // class-scoped matched delete (as unique_ptr default_delete would)
    assert(tp::stats.frees.load() == 202 && tp::stats.live_bytes.load() == 0 && dtors == 202);
    // 5) second owner refused while first is live (singleton discovery/spinlock invariant)
    { Facade a, b; assert(a.start(true)); assert(!b.start(true) && b.latched);
      a.context->done = true; owner = nullptr; }
    assert(tp::stats.live_bytes.load() == 0 && tp::plat::canary_failures.load() == 0 && tp::plat::zeroized_nonzero.load() == 0);
    std::printf("T1r9 lifecycle PSRAM host test PASS: sizeof=%zu allocs=%u frees=%u failures=%u peak=%u\n", n,
                tp::stats.allocs.load(), tp::stats.frees.load(), tp::stats.failures.load(), tp::stats.peak_bytes.load());
    return 0;
}
