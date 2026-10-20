#include "r13_psram_policy.h"
#if TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC || TESLA_BLE_MANUAL_WEB_BENCH
#if !defined(TESLA_BLE_BENCH_ONLY) || !TESLA_BLE_BENCH_ONLY || !defined(TESLA_BLE_IRAM_DIAGNOSTICS) || !TESLA_BLE_IRAM_DIAGNOSTICS
#error "PSRAM diagnostic requires explicit bench-only diagnostics"
#endif
#include "bluetooth_lifecycle.h"
#include "bluetooth_master_teardown.h"
#include "r13_psram_resource.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_rom_sys.h>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <cstdio>
namespace {
#if !TESLA_BLE_MANUAL_WEB_BENCH
BluetoothLifecycle diagnostic;
#endif
std::atomic<bool> good{true};
std::atomic<TaskHandle_t> worker{nullptr};
std::atomic<unsigned> watermark{0};
#if !TESLA_BLE_MANUAL_WEB_BENCH
bool ready = false, attempted = false, active_reported = false, stopped_reported = false;
#endif
unsigned current_states = 0, current_request = 0;
unsigned sequence = 0; // Serial snapshots are serialized below, including callbacks.
portMUX_TYPE output_lock = portMUX_INITIALIZER_UNLOCKED;
#if !TESLA_BLE_MANUAL_WEB_BENCH
char line[24]{};
unsigned used = 0;
bool overflow = false;
#endif
}
bool r13_psram_ok() { return good.load() && !bluetooth_master_teardown::forbidden.load(); }
void r13_psram_worker_begin() { worker.store(xTaskGetCurrentTaskHandle()); }
#if TESLA_BLE_MANUAL_WEB_BENCH
void r13_psram_worker_end() { worker.store(nullptr); }
#endif
void r13_psram_snapshot(const char *stage, const char *phase, int rc, bool rc_valid, bool teardown) {
    multi_heap_info_t exec{}, internal{}, byte{};
    heap_caps_get_info(&exec, MALLOC_CAP_EXEC);
    heap_caps_get_info(&internal, MALLOC_CAP_INTERNAL);
    heap_caps_get_info(&byte, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const auto task = worker.load();
    if (task) {
        const auto value = static_cast<unsigned>(uxTaskGetStackHighWaterMark(task));
        const auto old = watermark.load();
        if (!old || value < old) { watermark.store(value); }
    }
    const auto stack = watermark.load();
    const bool floor = r13_psram_resource::admit(byte.total_free_bytes, byte.largest_free_block, byte.minimum_free_bytes, stack, task != nullptr);
    if (!teardown && !floor) { good.store(false); }
    char code[16] = "na";
    if (rc_valid) { snprintf(code, sizeof(code), "%d", rc); }
    // ROM output is bounded and allocation-free; overlapping caps are not summed.
    // This short UART serialization lock is diagnostic-only, never a heap lock.
    portENTER_CRITICAL(&output_lock);
    esp_rom_printf("R13_MEM seq=%u stage=%s phase=%s rc=%s exec_free=%u exec_largest=%u exec_min=%u internal_free=%u internal_largest=%u internal_min=%u internal8_free=%u internal8_largest=%u internal8_min=%u worker_valid=%u worker_min=%u states=%u request=%u floor=%u teardown=%u\n",
      ++sequence, stage, phase, code,
      unsigned(exec.total_free_bytes), unsigned(exec.largest_free_block), unsigned(exec.minimum_free_bytes),
      unsigned(internal.total_free_bytes), unsigned(internal.largest_free_block), unsigned(internal.minimum_free_bytes),
      unsigned(byte.total_free_bytes), unsigned(byte.largest_free_block), unsigned(byte.minimum_free_bytes),
      task != nullptr ? 1u : 0u, stack, current_states, current_request, floor ? 1u : 0u, teardown ? 1u : 0u);
    portEXIT_CRITICAL(&output_lock);
}
void r13_psram_checkpoint(const char *module, const char *hook, bool after, unsigned states, unsigned request) {
    if (strcmp(module, "API") && strcmp(module, "Api") && strcmp(module, "Mqtt")) { return; }
    char stage[64]{};
    snprintf(stage, sizeof(stage), "%s.%s", !strcmp(module,"Mqtt") ? "mqtt" : "api", hook);
    current_states = states; current_request = request;
    r13_psram_snapshot(stage, after ? "after" : "before");
}
void r13_psram_boot(bool complete) {
    r13_psram_snapshot(complete ? "boot.complete" : "boot.start", "sample");
    if (!complete) { return; }
    // Exercise real allocator placement without changing SDK capability requests.
    void *ordinary = malloc(64);
    void *internal = heap_caps_malloc(64, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void *dma = heap_caps_malloc(64, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    const bool placement = ordinary && internal && dma && esp_ptr_external_ram(ordinary) && esp_ptr_internal(internal) && esp_ptr_internal(dma);
    free(ordinary); heap_caps_free(internal); heap_caps_free(dma);
    if (!placement) { good.store(false); }
    esp_rom_printf("R13_ALLOC ordinary_external=%u explicit_internal=%u explicit_dma_internal=%u\n", placement ? 1u : 0u, placement ? 1u : 0u, placement ? 1u : 0u);
    r13_psram_snapshot("allocation.probe", "after");
#if !TESLA_BLE_MANUAL_WEB_BENCH
    ready = true;
    esp_rom_printf("R13_POLICY event=off ready=%u attempted=0 controller=%d host=%d\n", r13_psram_ok() ? 1u : 0u, diagnostic.controller(), diagnostic.host());
#endif
}
#if !TESLA_BLE_MANUAL_WEB_BENCH
void r13_psram_loop() {
    // Clear the observer's handle before supervision can reclaim a parked task.
    // Failed ownership remains latched; this never makes a failure admissible.
    if (diagnostic.stopped() || diagnostic.failed()) { worker.store(nullptr); }
    diagnostic.supervise();
    if (diagnostic.failed() || !r13_psram_ok()) { diagnostic.request_stop(); }
    if (diagnostic.active() && !active_reported) {
        active_reported = true;
        r13_psram_snapshot("lifecycle.active", "sample");
        esp_rom_printf("R13_POLICY event=active ready=%u attempted=1 controller=%d host=%d\n", r13_psram_ok() ? 1u : 0u, diagnostic.controller(), diagnostic.host());
    }
    if (attempted && !stopped_reported && (diagnostic.stopped() || diagnostic.failed())) {
        stopped_reported = true;
        // Worker is parked and supervised before its handle is cleared; retain its measured minimum.
        worker.store(nullptr);
        esp_rom_printf("R13_POLICY event=%s ready=%u attempted=1 controller=%d host=%d\n", diagnostic.stopped() ? "stopped" : "failed", r13_psram_ok() ? 1u : 0u, diagnostic.controller(), diagnostic.host());
    }
    for (unsigned n=0; n<32 && Serial.available(); ++n) {
        const char c = static_cast<char>(Serial.read());
        if (c!='\n') {
            if (c<32 || c>126 || used+1>=sizeof(line)) { overflow=true; }
            else { line[used++]=c; }
            continue;
        }
        line[used]='\0';
        if (!overflow && !strcmp(line,"r13:psram:on") && ready && !attempted && r13_psram_ok()) {
            attempted=true;
            r13_psram_snapshot("lifecycle.launch", "before");
            if (!r13_psram_ok() || !diagnostic.start(nullptr)) { good.store(false); }
        } else if (!overflow && !strcmp(line,"r13:psram:off") && attempted) {
            diagnostic.request_stop();
        } else {
            good.store(false); diagnostic.request_stop();
            esp_rom_printf("R13_POLICY event=invalid ready=0 attempted=%u controller=%d host=%d\n", attempted ? 1u : 0u, diagnostic.controller(), diagnostic.host());
        }
        used=0; overflow=false;
    }
}
#endif
#endif
