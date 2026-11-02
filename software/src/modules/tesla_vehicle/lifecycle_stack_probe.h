#pragma once
// LSP1 lifecycle stack probe (MEASUREMENT ONLY, isolated build flag TESLA_LIFECYCLE_STACK_PROBE=1).
// Records uxTaskGetStackHighWaterMark() of the permanent "bluetooth" lifecycle task (bluetooth_lifecycle_stack::BYTES) at fixed stages, the
// NimBLE host task (CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE) at its own return and via its still-valid handle right
// before the existing vTaskDelete, and the lifecycle task via its still-valid handle right before main's existing
// vTaskDelete. ESP-IDF Xtensa: portSTACK_TYPE is uint8_t, so the value is BYTES never touched (0xa5 fill) since
// task creation, i.e. a historical minimum headroom, not the current depth.
// Writer cost: no formatting, no allocation, no lock, no task/timer/callout. One 16 B slot per stage in ONE PSRAM
// block allocated once at boot (pre_setup); single writer per slot; plain 32-bit atomic loads/stores only (ESP32 has
// no S32C1I on external RAM, so no RMW is ever done on the PSRAM block). Allocation failure fails closed: probe off,
// samples dropped and counted in internal .bss, Tesla/Bluetooth behaviour unchanged.
// Reader/publisher: main loop only; one bounded pass, no retry loop; torn/odd slots are skipped until the next loop.
// With TESLA_LIFECYCLE_STACK_PROBE=0 (production default) every hook is an empty inline function.
#include <cstdint>
#include <cstddef>
#include "bluetooth_lifecycle_stack.h"
#ifndef TESLA_LIFECYCLE_STACK_PROBE
#define TESLA_LIFECYCLE_STACK_PROBE 0
#endif
#if TESLA_LIFECYCLE_STACK_PROBE
#include <atomic>
#include <new>
#if defined(TESLA_BLE_MODULE_HOST_TEST) || defined(TESLA_NIMBLE_HOST_TEST) || defined(LSP_UNIT_TEST)
#define LSP_HOST 1
#include <cstdlib>
#else
#define LSP_HOST 0
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#endif
#endif

namespace lifecycle_stack_probe {
enum class Stage : uint8_t {
    TaskEntry, PortInit, HostCreated, SyncWait, ActiveTick, ScanParams, ScanStart, ScanStop, ScanRetire, LoopExit,
    Quiesce, Drain, HostStop, HostJoined, Deinit, HostPreDelete, HostRunReturn, Done, EarlyStop, MainPreDelete, Count
};
constexpr unsigned STAGES = static_cast<unsigned>(Stage::Count);
constexpr uint32_t LIFECYCLE_STACK_BYTES = bluetooth_lifecycle_stack::BYTES; // LST1: same constant the lifecycle .inc passes to xTaskCreatePinnedToCore()
inline const char *name(Stage s) {
    static constexpr const char *names[] = {"task_entry", "port_init", "host_created", "sync_wait", "active_tick",
        "scan_params", "scan_start", "scan_stop", "scan_retire", "loop_exit", "quiesce", "drain", "host_stop",
        "host_joined", "deinit", "host_pre_delete", "host_run_return", "done", "early_stop", "main_pre_delete"};
    static_assert(sizeof(names) / sizeof(*names) == STAGES, "stage names");
    return static_cast<unsigned>(s) < STAGES ? names[static_cast<unsigned>(s)] : "invalid";
}
inline bool host_stage(Stage s) { return s == Stage::HostPreDelete || s == Stage::HostRunReturn; }

#if TESLA_LIFECYCLE_STACK_PROBE
#if LSP_HOST
inline unsigned (*host_hwm)(const void *task) = nullptr; // test-installed; nullptr task = executing task
inline void *(*host_alloc)(size_t) = nullptr;           // test-installed (failure injection); default calloc
#ifndef CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE
#define CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE 5120
#endif
#endif
constexpr uint32_t HOST_STACK_BYTES = CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE;
struct Slot { std::atomic<uint32_t> seq{0}, count{0}, last{0}, min{0}; uint32_t writer_ms{0}; }; // writer_ms: writer-only
struct Published { uint32_t seq = 0, min = UINT32_MAX; };                                        // main-only
struct Table {
    Slot slots[STAGES];
    Published published[STAGES];
    uint32_t lines = 0;
    bool status_logged = false, budget_logged = false, summary_done = false, summary_pre_delete = false;
};
constexpr uint32_t LINE_BUDGET = 96; // bounded per boot (event log + serial)
enum State : uint8_t { Uninitialized = 0, Ready = 1, Failed = 2 };
inline std::atomic<Table *> table{nullptr};   // internal .bss: published once (release)
inline std::atomic<uint8_t> state{Uninitialized};
inline std::atomic<uint32_t> dropped{0};      // internal .bss RMW only
inline bool external = false;                 // written once in init() before publication

// Main/boot only (pre_setup), before the lifecycle/host tasks exist. Idempotent.
inline bool init() {
    if (state.load(std::memory_order_acquire) != Uninitialized) { return state.load() == Ready; }
#if LSP_HOST
    void *m = host_alloc ? host_alloc(sizeof(Table)) : std::calloc(1, sizeof(Table));
    const bool ext = m != nullptr;
#else
    void *m = heap_caps_calloc(1, sizeof(Table), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const bool ext = m && esp_ptr_external_ram(m);
    if (m && !ext) { heap_caps_free(m); m = nullptr; } // fail closed: never consume internal RAM for the probe
#endif
    if (!m) { state.store(Failed, std::memory_order_release); return false; }
    external = ext;
    table.store(new (m) Table(), std::memory_order_release);
    state.store(Ready, std::memory_order_release);
    return true;
}
// Executing-task or still-valid-handle read. Callers guarantee the handle is not yet deleted (see call sites).
inline uint32_t read_hwm(const void *task) {
#if LSP_HOST
    return host_hwm ? host_hwm(task) : 0;
#else
    return static_cast<uint32_t>(uxTaskGetStackHighWaterMark(static_cast<TaskHandle_t>(const_cast<void *>(task))));
#endif
}
// Single writer per slot (see Stage comments in the lifecycle .inc). Odd seq = write in progress.
__attribute__((noinline)) inline void store(Stage stage, uint32_t value, uint32_t now_ms = 0, uint32_t interval_ms = 0) {
    const unsigned i = static_cast<unsigned>(stage);
    Table *t = table.load(std::memory_order_acquire);
    if (i >= STAGES || !t) { dropped.fetch_add(1, std::memory_order_relaxed); return; }
    Slot &s = t->slots[i];
    const uint32_t n = s.count.load(std::memory_order_relaxed);
    if (interval_ms && n && now_ms - s.writer_ms < interval_ms) { return; }
    s.writer_ms = now_ms;
    const uint32_t q = s.seq.load(std::memory_order_relaxed);
    const uint32_t old_min = s.min.load(std::memory_order_relaxed);
    s.seq.store(q + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    s.last.store(value, std::memory_order_relaxed);
    s.min.store(n == 0 || value < old_min ? value : old_min, std::memory_order_relaxed);
    s.count.store(n == UINT32_MAX ? n : n + 1, std::memory_order_relaxed);
    s.seq.store(q + 2, std::memory_order_release);
}
__attribute__((noinline)) inline void self(Stage stage, uint32_t now_ms = 0, uint32_t interval_ms = 0) {
    if (!table.load(std::memory_order_acquire)) { dropped.fetch_add(1, std::memory_order_relaxed); return; }
    store(stage, read_hwm(nullptr), now_ms, interval_ms);
}
__attribute__((noinline)) inline void handle(Stage stage, const void *task) {
    if (!task || !table.load(std::memory_order_acquire)) { dropped.fetch_add(1, std::memory_order_relaxed); return; }
    store(stage, read_hwm(task));
}
struct View { uint32_t seq, count, last, min; };
// Main only. One pass, no retry: false if unavailable, never written, or a write is in progress/raced.
inline bool read(Stage stage, View &v) {
    const unsigned i = static_cast<unsigned>(stage);
    Table *t = table.load(std::memory_order_acquire);
    if (i >= STAGES || !t) { return false; }
    const Slot &s = t->slots[i];
    const uint32_t a = s.seq.load(std::memory_order_acquire);
    v.count = s.count.load(std::memory_order_relaxed);
    v.last = s.last.load(std::memory_order_relaxed);
    v.min = s.min.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    const uint32_t b = s.seq.load(std::memory_order_relaxed);
    v.seq = a;
    return a != 0 && !(a & 1u) && a == b;
}
// Sink(kind, stage_name, a, b, c, d): kind 0=status(state,external,bytes,dropped) 1=stage(count,last,min,stack_bytes)
// 2=summary(lifecycle_min,host_min,main_pre_delete_or_0,dropped) 3=budget(lines,dropped,0,0). Main loop only.
template <typename Sink> inline void publish(Sink &&sink) {
    Table *t = table.load(std::memory_order_acquire);
    if (!t) {
        static bool failed_logged = false; // main-only
        if (state.load() == Failed && !failed_logged) { failed_logged = true; sink(0, "failed", 2u, 0u, 0u, dropped.load()); }
        return;
    }
    if (!t->status_logged) { t->status_logged = true; sink(0, "ready", 1u, external ? 1u : 0u, uint32_t(sizeof(Table)), dropped.load()); }
    View done{}, pre{};
    const bool have_done = read(Stage::Done, done), have_pre = read(Stage::MainPreDelete, pre);
    if ((have_done && !t->summary_done) || (have_pre && !t->summary_pre_delete)) {
        uint32_t lc = UINT32_MAX, host = UINT32_MAX;
        for (unsigned i = 0; i < STAGES; ++i) {
            View v{}; if (!read(static_cast<Stage>(i), v)) { continue; }
            uint32_t &m = host_stage(static_cast<Stage>(i)) ? host : lc; if (v.min < m) { m = v.min; }
        }
        t->summary_done = t->summary_done || have_done; t->summary_pre_delete = t->summary_pre_delete || have_pre;
        sink(2, have_pre ? "main_pre_delete" : "done", lc == UINT32_MAX ? 0u : lc, host == UINT32_MAX ? 0u : host,
             have_pre ? pre.min : 0u, dropped.load());
    }
    for (unsigned i = 0; i < STAGES; ++i) {
        const Stage s = static_cast<Stage>(i);
        View v{}; if (!read(s, v) || v.seq == t->published[i].seq) { continue; }
        if (s == Stage::ActiveTick && t->published[i].seq && v.min >= t->published[i].min) { t->published[i].seq = v.seq; continue; }
        if (t->lines >= LINE_BUDGET) {
            if (!t->budget_logged) { t->budget_logged = true; sink(3, "budget", t->lines, dropped.load(), 0u, 0u); }
            return;
        }
        t->published[i].seq = v.seq; t->published[i].min = v.min; ++t->lines;
        sink(1, name(s), v.count, v.last, v.min, host_stage(s) ? HOST_STACK_BYTES : LIFECYCLE_STACK_BYTES);
    }
}
#else
inline bool init() { return false; }
inline void self(Stage, uint32_t = 0, uint32_t = 0) {}
inline void handle(Stage, const void *) {}
template <typename Sink> inline void publish(Sink &&) {}
#endif
} // namespace lifecycle_stack_probe
