#pragma once
#include "bluetooth_littlefs_binding.h"
#ifndef TESLA_BLE_MODULE_HOST_TEST
#include <sdkconfig.h>
#include <soc/soc_caps.h>
#if defined(SOC_BT_SUPPORTED) && SOC_BT_SUPPORTED && defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED && defined(CONFIG_BLUEDROID_ENABLED) && CONFIG_BLUEDROID_ENABLED && defined(CONFIG_BT_BLE_ENABLED) && CONFIG_BT_BLE_ENABLED
#define WARP_BLUETOOTH_COMPILED 1
#else
#define WARP_BLUETOOTH_COMPILED 0
#endif
#elif defined(R13_UNSUPPORTED)
#define WARP_BLUETOOTH_COMPILED 0
#else
#define WARP_BLUETOOTH_COMPILED 1
#endif
inline const char *bluetooth_unsupported_reason() {
#if defined(R13_UNSUPPORTED)
    return "sdk_disabled";
#elif defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY
    return "bench_only";
#elif !WARP_BLUETOOTH_COMPILED
#if !defined(SOC_BT_SUPPORTED) || !SOC_BT_SUPPORTED
    return "hardware_unsupported";
#else
    return "sdk_disabled";
#endif
#elif defined(ESP_PLATFORM) && !BLUETOOTH_CONFIG_TARGET_VFS_ADAPTER
    return "storage_backend_unverified";
#else
    return "";
#endif
}
inline bool bluetooth_supported() { return bluetooth_unsupported_reason()[0] == '\0'; }
