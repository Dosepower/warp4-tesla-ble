#pragma once
// Production (-DTESLA_PSRAM_PLACEMENT=1, origin T1r8-T1r14 lab): explicit PSRAM placement for Tesla-owned objects.
// Scope is the Tesla extension only - no global malloc/new override, no SDK/config change.
//  * alloc(): heap_caps_calloc(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT). NO internal fallback: failure is reported
//    to the caller exactly like the previous nothrow-new failure (nullptr), never silently moved to internal RAM.
//  * release(): zeroizes exactly header+requested bytes and heap_caps_free()s the block. The requested size is kept
//    in a 16 B prefix header. Never wipe heap_caps_get_allocated_size() bytes: with CONFIG_HEAP_POISONING_* it is the
//    rounded TLSF block, so the wipe would destroy the tail canary at data+requested and heap_caps_free asserts (B1).
//  * Owned<T>/create<T>(): unique_ptr with a matching destructor+zeroize+free deleter (placement new).
//  * PSRAM_OPERATORS: class-scoped nothrow new/delete for Tesla's own aggregates (Runtime, Tesla), so existing
//    `new (std::nothrow) Runtime(...)` / unique_ptr<Runtime> sites stay unchanged and stay matched.
//    T1r9: also BluetoothLifecycle::Context (NimBLE central + discovery state); its discovery spinlock is a static
//    internal .bss portMUX under this flag (TeslaBleDiscovery), never inside the PSRAM object.
//    T1r10: also the bluetooth_memory sample table (MemorySamples, allocated once in pre_setup, never freed);
//    its portMUX stays a static internal .bss lock (bluetooth_memory.h).
//    T1r11: also every storage-executor Job (ExecutorJob, transient per I/O; executor uses internal-stack bounce copies).
// Safety preconditions (checked by #error / documented in MAX_TESLA_PSRAM_REPORT):
//  * CONFIG_SPIRAM=y, CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND=y (std::atomic RMW in PSRAM via libcalls,
//    -mdisable-hardware-atomics) and esp_cpu_compare_and_set external-RAM path for FreeRTOS spinlocks.
//  * Objects are touched from task context only (worker, main loop, NimBLE host task); none from an ISR or
//    while the flash cache is disabled. DMA/TCB/ISR/executor-stack/Job memory stays internal (not here).
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
#include <tesla_clean/support.hpp>

#if defined(TESLA_PSRAM_PLACEMENT) && TESLA_PSRAM_PLACEMENT
#ifdef TESLA_PSRAM_HOST_TEST
#include "tesla_psram_host_platform.h" // host malloc + simulated "external" tagging; proves accounting, NOT placement
#else
#include "sdkconfig.h"
#if !CONFIG_SPIRAM
#error "TESLA_PSRAM_PLACEMENT requires CONFIG_SPIRAM"
#endif
#if !CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND
#error "TESLA_PSRAM_PLACEMENT: Tesla objects contain std::atomic; PSRAM placement needs the S32C1I SPIRAM workaround"
#endif
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include <mbedtls/platform_util.h>
namespace tesla_psram { namespace plat {
constexpr uint32_t CAPS = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
inline void *calloc_ext(size_t n) { return heap_caps_calloc(1, n, CAPS); }
inline void free_ext(void *p) { heap_caps_free(p); }
inline bool is_external(const void *p) { return p && esp_ptr_external_ram(p); }
inline void zeroize(void *p, size_t n) { mbedtls_platform_zeroize(p, n); }
}}
#endif

namespace tesla_psram {
// Object kinds whose placement is read back (bit index in placed_* masks). Never pointers in readback.
enum Obj : uint8_t { TeslaRoot = 0, Runtime = 1, Keys = 2, Vehicle = 3, RxQueue = 4, EventQueue = 5, KeyMaterial = 6,
                     KeyScratch = 7, Callback = 8, LabKeys = 9, Lifecycle = 10, MemorySamples = 11, ExecutorJob = 12, Count = 13 };
inline const char *obj_name(unsigned o) {
    static const char *const n[] = {"tesla_root", "runtime", "keys", "vehicle", "rx_queue", "event_queue", "key_material",
                                    "key_scratch", "callback", "lab_keys", "lifecycle", "memory_samples", "executor_job"};
    return o < Count ? n[o] : "invalid"; }
struct Stats {
    std::atomic<uint32_t> allocs{0}, frees{0}, failures{0}, live_bytes{0}, peak_bytes{0}, non_external{0};
    std::atomic<uint32_t> placed_seen{0}, placed_external{0}, failed_mask{0};
    std::atomic<uint32_t> obj_bytes[Count]{};
};
inline Stats stats; // small scalar table in internal .bss (read by main/fixture)
// Host/fixture-only failure injection; NOT compiled into production (TESLA_WORKER_TEST_HOOKS unset).
#if defined(TESLA_WORKER_TEST_HOOKS) && TESLA_WORKER_TEST_HOOKS
inline std::atomic<uint32_t> inject_failures{0}; // test hook: fail next N allocations
#endif

inline void note(Obj o, const void *p, size_t bytes) {
    const uint32_t bit = 1u << o;
    stats.placed_seen.fetch_or(bit);
    if (plat::is_external(p)) { stats.placed_external.fetch_or(bit); } else { stats.placed_external.fetch_and(~bit); stats.non_external.fetch_add(1); }
    stats.obj_bytes[o].store(static_cast<uint32_t>(bytes));
}
// 16 B prefix: holds the requested size; keeps the heap's own alignment (4 B on ESP32, 16 B on host malloc).
constexpr size_t HDR = 16;
struct Hdr { uint32_t requested; };
static_assert(sizeof(Hdr) <= HDR, "header");
inline void *alloc(size_t n, Obj o) {
#if defined(TESLA_WORKER_TEST_HOOKS) && TESLA_WORKER_TEST_HOOKS
    uint32_t pending = inject_failures.load();
    while (pending != 0 && !inject_failures.compare_exchange_weak(pending, pending - 1)) {}
#else
    constexpr uint32_t pending = 0;
#endif
    void *base = (pending != 0 || n > UINT32_MAX - HDR) ? nullptr : plat::calloc_ext(n + HDR);
    if (!base) { stats.failures.fetch_add(1); stats.failed_mask.fetch_or(1u << o); return nullptr; }
    static_cast<Hdr *>(base)->requested = static_cast<uint32_t>(n);
    void *p = static_cast<unsigned char *>(base) + HDR;
    const uint32_t size = static_cast<uint32_t>(n); // live/peak = requested object bytes (excl. header + heap rounding)
    stats.allocs.fetch_add(1);
    const uint32_t live = stats.live_bytes.fetch_add(size) + size;
    uint32_t peak = stats.peak_bytes.load();
    while (live > peak && !stats.peak_bytes.compare_exchange_weak(peak, live)) {}
    note(o, base, n); // placement of the block we own (host sim tags the block base)
    return p;
}
inline void release(void *p) {
    if (!p) { return; }
    void *base = static_cast<unsigned char *>(p) - HDR;
    const uint32_t n = static_cast<Hdr *>(base)->requested;
    plat::zeroize(base, HDR + n); // exactly the bytes we own: object (may have held key material) + header
    plat::free_ext(base);
    stats.frees.fetch_add(1); stats.live_bytes.fetch_sub(n);
}
template <typename T> struct Deleter {
    void operator()(T *p) const noexcept { if (p) { p->~T(); release(p); } }
};
template <typename T> using Owned = std::unique_ptr<T, Deleter<T>>;
template <typename T, typename... A> T *create(Obj o, A &&...a) {
    static_assert(alignof(T) <= alignof(std::max_align_t), "PSRAM heap alignment");
    void *m = alloc(sizeof(T), o);
    return m ? new (m) T(std::forward<A>(a)...) : nullptr; // Tesla/tesla_clean ctors used here are noexcept-in-practice (no exceptions build)
}
// tesla_clean::Allocation hook (IdentityStore key Material, CheckedCallback nodes): context carries the Obj kind.
inline void *allocation_get(void *context, size_t n) { return alloc(n, static_cast<Obj>(reinterpret_cast<uintptr_t>(context))); }
inline void allocation_put(void *, void *p) { release(p); }
inline tesla_clean::Allocation allocation(Obj o) {
    tesla_clean::Allocation a; a.allocate = allocation_get; a.release = allocation_put;
    a.context = reinterpret_cast<void *>(static_cast<uintptr_t>(o)); return a; }
} // namespace tesla_psram

// Class-scoped operators: every new/delete of the class (incl. unique_ptr<Class> default_delete) is matched here.
#define TESLA_PSRAM_OPERATORS(KIND) \
    static void *operator new(size_t n, const std::nothrow_t &) noexcept { return tesla_psram::alloc(n, tesla_psram::KIND); } \
    static void operator delete(void *p) noexcept { tesla_psram::release(p); } \
    static void operator delete(void *p, const std::nothrow_t &) noexcept { tesla_psram::release(p); }
#else
namespace tesla_psram {
enum Obj : uint8_t { TeslaRoot, Runtime, Keys, Vehicle, RxQueue, EventQueue, KeyMaterial, KeyScratch, Callback, LabKeys, Lifecycle, MemorySamples, ExecutorJob, Count };
template <typename T> using Owned = std::unique_ptr<T>;
template <typename T, typename... A> T *create(Obj, A &&...a) { return new (std::nothrow) T(std::forward<A>(a)...); }
inline tesla_clean::Allocation allocation(Obj) { return {}; }
}
#define TESLA_PSRAM_OPERATORS(KIND)
#endif
