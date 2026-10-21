#pragma once
// Installer-only compile-time proof against the actually consumed ESP32 SDK.
#include <stdbool.h>
#include "sdkconfig.h"
#include "esp32/rom/secure_boot.h"
#if !CONFIG_SECURE_BOOT || !CONFIG_SECURE_BOOT_V2_ENABLED || !CONFIG_SPIRAM
#error Installer requires WARP4 Secure Boot v2 and PSRAM configuration
#endif
#if CONFIG_PARTITION_TABLE_OFFSET != 0xe000 || !CONFIG_ESPTOOLPY_FLASHSIZE_16MB
#error Installer requires the original 16MB secure partition layout
#endif
#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE || CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
#error Installer must preserve boot rollback without eFuse antirollback
#endif
ESP_STATIC_ASSERT(sizeof(ets_secure_boot_sig_block_t) == 1216, "unqualified signature block");
ESP_STATIC_ASSERT(sizeof(ets_secure_boot_signature_t) == 4096, "unqualified signature sector");
