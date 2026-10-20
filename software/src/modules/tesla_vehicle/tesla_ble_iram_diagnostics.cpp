#include "tesla_ble_iram_diagnostics.h"
#include "r13_psram_policy.h"

#if defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gattc_api.h>
#if (defined(TESLA_BLE_R13_MASTER_BENCH) && TESLA_BLE_R13_MASTER_BENCH) || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
#include <esp_gap_ble_api.h>
#endif
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <cstdio>
#include <cstring>
#include "bluetooth_master_teardown.h"

void tesla_ble_iram_snapshot(const char *stage, const char *phase, int rc, bool rc_valid)
{
#if TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC || TESLA_BLE_MANUAL_WEB_BENCH
    const bool teardown = strstr(stage, "teardown") || strstr(stage, ".disable") || strstr(stage, ".deinit");
    r13_psram_snapshot(stage, phase, rc, rc_valid, teardown);
    return;
#endif
    multi_heap_info_t exec{}, internal{};
    // Independent capability views can overlap; never add these totals.
    // Minima are SDK heap low-water marks, not reset per diagnostic stage.
    heap_caps_get_info(&exec, MALLOC_CAP_EXEC);
    heap_caps_get_info(&internal, MALLOC_CAP_INTERNAL);
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY
    // INTERNAL alone also includes word-only IRAM. FreeRTOS cannot use that
    // for its byte-addressed task stacks, TCBs, queues and semaphores.
    multi_heap_info_t internal8{};
    heap_caps_get_info(&internal8, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
    // ROM UART output avoids the event-log/Arduino String allocation path.
    // Serial output perturbs timing; the capability views are not atomic.
    char code[16] = "na";
    if (rc_valid) { snprintf(code, sizeof(code), "%d", rc); }
    esp_rom_printf("TESLA_IRAM stage=%s phase=%s rc=%s exec_free=%u exec_largest=%u exec_min=%u internal_free=%u internal_largest=%u internal_min=%u"
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY
                   " internal8_free=%u internal8_largest=%u internal8_min=%u"
#endif
                   "\n",
                   stage, phase, code,
                   static_cast<unsigned>(exec.total_free_bytes), static_cast<unsigned>(exec.largest_free_block),
                   static_cast<unsigned>(exec.minimum_free_bytes), static_cast<unsigned>(internal.total_free_bytes),
                   static_cast<unsigned>(internal.largest_free_block), static_cast<unsigned>(internal.minimum_free_bytes)
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY
                   , static_cast<unsigned>(internal8.total_free_bytes), static_cast<unsigned>(internal8.largest_free_block),
                   static_cast<unsigned>(internal8.minimum_free_bytes)
#endif
                   );
}

template <typename Submit>
static esp_err_t diagnose_call(const char *stage, Submit submit)
{
    tesla_ble_iram_snapshot(stage, "before");
    const esp_err_t rc = submit();
    tesla_ble_iram_snapshot(stage, "after", rc, true);
    return rc;
}

// Linker wrappers observe the pinned Arduino/IDF initialization path without
// replacing it, changing ownership or inventing success from a bool result.
extern "C" {
#if (defined(TESLA_BLE_R13_MASTER_BENCH) && TESLA_BLE_R13_MASTER_BENCH) || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
esp_err_t __wrap_esp_ble_gap_start_scanning(uint32_t) {
    bluetooth_master_teardown::forbidden.store(true);
    esp_rom_printf("TESLA_MASTER_FORBIDDEN scan\n");
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t __wrap_esp_ble_gap_start_advertising(esp_ble_adv_params_t *) {
    bluetooth_master_teardown::forbidden.store(true);
    esp_rom_printf("TESLA_MASTER_FORBIDDEN advertising\n");
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t __real_esp_bluedroid_disable();
esp_err_t __real_esp_bluedroid_deinit();
esp_err_t __real_esp_bt_controller_disable();
esp_err_t __real_esp_bt_controller_deinit();
esp_err_t __wrap_esp_bluedroid_disable() {
    return bluetooth_master_teardown::observe(1, diagnose_call("host.disable", [] { return __real_esp_bluedroid_disable(); }));
}
esp_err_t __wrap_esp_bluedroid_deinit() {
    return bluetooth_master_teardown::observe(2, diagnose_call("host.deinit", [] { return __real_esp_bluedroid_deinit(); }));
}
esp_err_t __wrap_esp_bt_controller_disable() {
    return bluetooth_master_teardown::observe(4, diagnose_call("controller.disable", [] { return __real_esp_bt_controller_disable(); }));
}
esp_err_t __wrap_esp_bt_controller_deinit() {
    return bluetooth_master_teardown::observe(8, diagnose_call("controller.deinit", [] { return __real_esp_bt_controller_deinit(); }));
}
#endif
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY
void *__real_pvPortMalloc(size_t size);
void *__wrap_pvPortMalloc(size_t size)
{
    // No global allocation-failure callback replacement (Debug owns it).
    // Observe the real RTOS allocator once, never retry or change its result.
    void *memory = __real_pvPortMalloc(size);
    if (!memory && size != 0) {
        esp_rom_printf("TESLA_BENCH_ALLOC function=pvPortMalloc bytes=%u caps=0x%08x\n",
                       static_cast<unsigned>(size), static_cast<unsigned>(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    return memory;
}
#endif
esp_err_t __real_esp_bt_controller_init(esp_bt_controller_config_t *config);
esp_err_t __real_esp_bt_controller_enable(esp_bt_mode_t mode);
esp_err_t __real_esp_bluedroid_init();
esp_err_t __real_esp_bluedroid_enable();
esp_err_t __real_esp_ble_gattc_register_callback(esp_gattc_cb_t callback);
esp_err_t __real_esp_ble_gattc_app_register(uint16_t app_id);
esp_err_t __real_esp_ble_gattc_open(esp_gatt_if_t interface, uint8_t *address, esp_ble_addr_type_t type, bool direct);

esp_err_t __wrap_esp_bt_controller_init(esp_bt_controller_config_t *config)
{
    return diagnose_call("controller.init", [config]() { return __real_esp_bt_controller_init(config); });
}
esp_err_t __wrap_esp_bt_controller_enable(esp_bt_mode_t mode)
{
    return diagnose_call("controller.enable", [mode]() { return __real_esp_bt_controller_enable(mode); });
}
esp_err_t __wrap_esp_bluedroid_init()
{
    return diagnose_call("host.init", []() { return __real_esp_bluedroid_init(); });
}
esp_err_t __wrap_esp_bluedroid_enable()
{
    return diagnose_call("host.enable", []() { return __real_esp_bluedroid_enable(); });
}
esp_err_t __wrap_esp_ble_gattc_register_callback(esp_gattc_cb_t callback)
{
    return diagnose_call("gatt.callback_register", [callback]() { return __real_esp_ble_gattc_register_callback(callback); });
}
esp_err_t __wrap_esp_ble_gattc_app_register(uint16_t app_id)
{
    return diagnose_call("gatt.app_register", [app_id]() { return __real_esp_ble_gattc_app_register(app_id); });
}
esp_err_t __wrap_esp_ble_gattc_open(esp_gatt_if_t interface, uint8_t *address, esp_ble_addr_type_t type, bool direct)
{
#if (defined(TESLA_BLE_R13_MASTER_BENCH) && TESLA_BLE_R13_MASTER_BENCH) || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
    (void)interface; (void)address; (void)type; (void)direct;
    bluetooth_master_teardown::forbidden.store(true);
    esp_rom_printf("TESLA_MASTER_FORBIDDEN gatt.open\n");
    return ESP_ERR_NOT_SUPPORTED;
#else
    return diagnose_call("gatt.open", [interface, address, type, direct]() {
        return __real_esp_ble_gattc_open(interface, address, type, direct);
    });
#endif
}
}
#endif
