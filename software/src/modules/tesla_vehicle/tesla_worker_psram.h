#pragma once
// Tesla worker task stack in PSRAM (origin: T1r6-T1r14 lab, ported to production; behavior unchanged).
// Compiled only with -DTESLA_WORKER_PSRAM_STACK=1. Uses the supported IDF v5.5 APIs
// xTaskCreatePinnedToCoreWithCaps()/vTaskDeleteWithCaps() (freertos/idf_additions.h):
//   * TCB: pvPortMalloc() -> internal RAM (idf_additions.c; asserted by xPortCheckValidTCBMem)
//   * stack: heap_caps_malloc(12288, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT) -> PSRAM
// IDF only accepts an external stack when CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y
// (heap_idf.c xPortcheckValidStackMem); that is the single explicit SDK delta (variant eth-ble1-extstk).
// A task whose stack is in PSRAM must never run a flash operation (cache disabled):
// IDF asserts in spi_flash_disable_interrupts_caches_and_other_cpu(); this module additionally
// fails Tesla LittleFS I/O closed *before* touching flash when called on a PSRAM stack.
#if defined(TESLA_WORKER_PSRAM_STACK) && TESLA_WORKER_PSRAM_STACK
#include "sdkconfig.h"
#if !CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
#error "TESLA_WORKER_PSRAM_STACK requires SDK variant eth-ble1-extstk (CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y)"
#endif
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && !CONFIG_ESP_COREDUMP_USE_STACK_SIZE
#error "coredump-to-flash with PSRAM task stacks requires a dedicated DRAM coredump stack"
#endif
#if !defined(CONFIG_ESP32_REV_MIN_FULL) || CONFIG_ESP32_REV_MIN_FULL < 300
#error "PSRAM task stack lab requires ESP32 rev >= 3.0 (no PSRAM cache workaround for ROM/BT controller code)"
#endif
#if !CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE
#error "IDF cache-off stack assert must stay enabled for the PSRAM stack lab"
#endif
#include <atomic>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_cpu.h"

namespace tesla_worker_psram {
constexpr configSTACK_DEPTH_TYPE STACK_BYTES = 12288; // unchanged size; no shrink
constexpr UBaseType_t STACK_CAPS = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

struct Proof {
    std::atomic<uint32_t> creates{0}, create_failures{0}, deletes{0}, flash_denials{0};
    std::atomic<uint32_t> stack_addr{0}, tcb_addr{0}, worker_sp{0};
    std::atomic<bool> buffers_valid{false}, stack_external{false}, tcb_internal{false}, sp_external{false};
};
inline Proof proof;
// Host/fixture-only failure injection; NOT compiled into production (TESLA_WORKER_TEST_HOOKS unset).
#if defined(TESLA_WORKER_TEST_HOOKS) && TESLA_WORKER_TEST_HOOKS
inline std::atomic<uint32_t> inject_create_failures{0};
#endif

// esp_cpu_get_sp() returns void * in IDF v5.5 (esp_cpu.h); keep it a pointer for the check.
inline bool on_psram_stack() { return esp_ptr_external_ram(static_cast<const void *>(esp_cpu_get_sp())); }

// Returns true when the caller must NOT touch flash (current stack lives in PSRAM).
inline bool deny_flash_from_psram_stack()
{
    if (!on_psram_stack()) { return false; }
    proof.flash_denials.fetch_add(1, std::memory_order_relaxed);
    return true;
}

inline BaseType_t create(TaskFunction_t entry, void *context, TaskHandle_t *handle)
{
#if defined(TESLA_WORKER_TEST_HOOKS) && TESLA_WORKER_TEST_HOOKS
    uint32_t pending = inject_create_failures.load();
    while (pending != 0 && !inject_create_failures.compare_exchange_weak(pending, pending - 1)) {}
    if (pending != 0) { proof.create_failures.fetch_add(1); *handle = nullptr; return pdFAIL; }
#endif
    // Same name/priority/core as production; only the stack memory capability differs.
    const BaseType_t rc = xTaskCreatePinnedToCoreWithCaps(entry, "tesla_ble", STACK_BYTES, context, 1, handle, 0, STACK_CAPS);
    if (rc != pdPASS) { proof.create_failures.fetch_add(1); *handle = nullptr; return rc; }
    StackType_t *stack = nullptr; StaticTask_t *tcb = nullptr;
    const bool valid = xTaskGetStaticBuffers(*handle, &stack, &tcb) == pdTRUE;
    proof.stack_addr.store(reinterpret_cast<uintptr_t>(stack));
    proof.tcb_addr.store(reinterpret_cast<uintptr_t>(tcb));
    proof.stack_external.store(valid && esp_ptr_external_ram(stack));
    proof.tcb_internal.store(valid && esp_ptr_internal(tcb) && !esp_ptr_external_ram(tcb));
    proof.buffers_valid.store(valid);
    proof.creates.fetch_add(1);
    return rc;
}

// Called by main only after the worker published worker_stopped and parked itself
// (production ACK barrier unchanged). vTaskDeleteWithCaps suspends, waits until the task is
// not current on any core, deletes it (TLSP deletion callbacks run) and frees the PSRAM
// stack with heap_caps_free and the internal TCB with vPortFree.
inline void destroy(TaskHandle_t handle)
{
    vTaskDeleteWithCaps(handle);
    proof.deletes.fetch_add(1);
}

#if defined(TESLA_PSRAM_PLACEMENT) && TESLA_PSRAM_PLACEMENT
// T1r11: the transient "tesla_teardown" task (4096 B, created by main only during stop) gets a PSRAM stack.
// Audited call set of TeslaVehicle::teardown_entry/release_bluetooth: atomics, millis(), vTaskDelay, strlcpy into
// Runtime, BluetoothLifecycle::request_stop/stopped/failed (atomic loads/stores only; controller/host APIs run on the
// separate internal-stack lifecycle task), bluetooth_memory::sample (heap_caps queries + HWM, same as the worker),
// TeslaBleTeardown::complete (atomics), then park. No flash/NVS/LittleFS/partition/ROM-flash call, no ISR use.
// It never deletes itself: main deletes it only after the completion ACK (existing reaper), with vTaskDeleteWithCaps.
// On failure/timeout the parked task and its stack are retained (ownership retained, reboot required) as before.
constexpr configSTACK_DEPTH_TYPE TEARDOWN_STACK_BYTES = 4096; // unchanged size
inline Proof teardown_proof;
#if defined(TESLA_WORKER_TEST_HOOKS) && TESLA_WORKER_TEST_HOOKS
inline std::atomic<uint32_t> inject_teardown_create_failures{0};
#endif
inline BaseType_t create_teardown(TaskFunction_t entry, void *context, TaskHandle_t *handle)
{
#if defined(TESLA_WORKER_TEST_HOOKS) && TESLA_WORKER_TEST_HOOKS
    uint32_t pending = inject_teardown_create_failures.load();
    while (pending != 0 && !inject_teardown_create_failures.compare_exchange_weak(pending, pending - 1)) {}
    if (pending != 0) { teardown_proof.create_failures.fetch_add(1); *handle = nullptr; return pdFAIL; }
#endif
    const BaseType_t rc = xTaskCreatePinnedToCoreWithCaps(entry, "tesla_teardown", TEARDOWN_STACK_BYTES, context, 1, handle, 0, STACK_CAPS);
    if (rc != pdPASS) { teardown_proof.create_failures.fetch_add(1); *handle = nullptr; return rc; }
    StackType_t *stack = nullptr; StaticTask_t *tcb = nullptr;
    const bool valid = xTaskGetStaticBuffers(*handle, &stack, &tcb) == pdTRUE;
    teardown_proof.stack_addr.store(reinterpret_cast<uintptr_t>(stack));
    teardown_proof.tcb_addr.store(reinterpret_cast<uintptr_t>(tcb));
    teardown_proof.stack_external.store(valid && esp_ptr_external_ram(stack));
    teardown_proof.tcb_internal.store(valid && esp_ptr_internal(tcb) && !esp_ptr_external_ram(tcb));
    teardown_proof.buffers_valid.store(valid);
    teardown_proof.creates.fetch_add(1);
    return rc;
}
inline void teardown_begin()
{
    void *const sp = esp_cpu_get_sp();
    teardown_proof.worker_sp.store(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sp)));
    teardown_proof.sp_external.store(esp_ptr_external_ram(sp));
}
// main only, after TeslaBleTeardown reported completion (task parked or about to park; WithCaps waits until not running).
inline void destroy_teardown(TaskHandle_t handle)
{
    vTaskDeleteWithCaps(handle);
    teardown_proof.deletes.fetch_add(1);
}
#endif

inline void worker_begin()
{
    void *const sp = esp_cpu_get_sp();
    proof.worker_sp.store(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sp)));
    proof.sp_external.store(esp_ptr_external_ram(sp));
}
} // namespace tesla_worker_psram
#endif
