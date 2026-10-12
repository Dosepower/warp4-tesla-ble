#include "tesla_ble_iram_diagnostics.h"

#if defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gattc_api.h>
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <cstdio>

void tesla_ble_iram_snapshot(const char *stage, const char *phase, int rc, bool rc_valid)
{
    multi_heap_info_t exec{}, internal{};
    // Independent capability views can overlap; never add these totals.
    // Minima are SDK heap low-water marks, not reset per diagnostic stage.
    heap_caps_get_info(&exec, MALLOC_CAP_EXEC);
    heap_caps_get_info(&internal, MALLOC_CAP_INTERNAL);
    // ROM UART output avoids the event-log/Arduino String allocation path.
    // Serial output perturbs timing; these two heap views are not atomic.
    char code[16] = "na";
    if (rc_valid) { snprintf(code, sizeof(code), "%d", rc); }
    esp_rom_printf("TESLA_IRAM stage=%s phase=%s rc=%s exec_free=%u exec_largest=%u exec_min=%u internal_free=%u internal_largest=%u internal_min=%u\n",
                   stage, phase, code,
                   static_cast<unsigned>(exec.total_free_bytes), static_cast<unsigned>(exec.largest_free_block),
                   static_cast<unsigned>(exec.minimum_free_bytes), static_cast<unsigned>(internal.total_free_bytes),
                   static_cast<unsigned>(internal.largest_free_block), static_cast<unsigned>(internal.minimum_free_bytes));
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
    return diagnose_call("gatt.open", [interface, address, type, direct]() {
        return __real_esp_ble_gattc_open(interface, address, type, direct);
    });
}
}
#endif
