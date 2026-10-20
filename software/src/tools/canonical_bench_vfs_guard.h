// The pinned Arduino begin(false,...) disables formatting, but sets grow=true.
// In this distinct canonical bench disable implicit filesystem growth as well.
#pragma once
#include <esp_littlefs.h>
#if !TESLA_BLE_CANONICAL_LAYOUT_BENCH || (!(defined(TESLA_BLE_MANUAL_WEB_BENCH) && TESLA_BLE_MANUAL_WEB_BENCH) && !(defined(TESLA_BLE_MANUAL_VEHICLE_UI) && TESLA_BLE_MANUAL_VEHICLE_UI))
#error "Canonical VFS guard requires an explicitly guarded app-only profile"
#endif
extern "C" esp_err_t __real_esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *conf);
extern "C" esp_err_t __wrap_esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *conf);
extern "C" esp_err_t __wrap_esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *conf)
{
    if (conf == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    auto safe = *conf;
    safe.format_if_mount_failed = false;
    safe.grow_on_mount = false;
    return __real_esp_vfs_littlefs_register(&safe);
}
