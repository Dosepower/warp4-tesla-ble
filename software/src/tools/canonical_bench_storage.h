// Isolated canonical test-hardware variant only. Never migrate, format, or probe coredump.
#pragma once
#include <sdkconfig.h>
#include <esp_partition.h>
#include <LittleFS.h>

#if !TESLA_BLE_CANONICAL_LAYOUT_BENCH || (!(defined(TESLA_BLE_MANUAL_WEB_BENCH) && TESLA_BLE_MANUAL_WEB_BENCH) && !(defined(TESLA_BLE_MANUAL_VEHICLE_UI) && TESLA_BLE_MANUAL_VEHICLE_UI))
#error "Canonical storage requires an explicitly guarded app-only profile"
#endif
static_assert(CONFIG_PARTITION_TABLE_OFFSET == 0x8000, "Canonical SDK required");
static_assert(ESP_PARTITION_SUBTYPE_DATA_SPIFFS == 0x82, "Stock subtype required");

// An explicit label allows existing LittleFS contents in the historical SPIFFS
// partition. A SPIFFS-formatted or corrupt volume MUST fail, never be converted.
// All ordinary WARP file APIs remain LittleFS; do not pretend they support SPIFFS.
static bool mount_canonical_bench_data()
{
    struct Expected { const char *name; uint8_t type; uint8_t subtype; uint32_t address; uint32_t size; };
    static constexpr Expected layout[] = {
        {"nvs", 1, 2, 0x9000, 0x5000},
        {"otadata", 1, 0, 0xe000, 0x2000},
        {"app0", 0, 0x10, 0x10000, 0x640000},
        {"app1", 0, 0x11, 0x650000, 0x640000},
        {"spiffs", 1, 0x82, 0xc90000, 0x360000},
        {"coredump", 1, 3, 0xff0000, 0x10000},
    };
    for (const auto &expected : layout) {
        const auto *p = esp_partition_find_first(static_cast<esp_partition_type_t>(expected.type),
                                                 static_cast<esp_partition_subtype_t>(expected.subtype), expected.name);
        if (p == nullptr || p->address != expected.address || p->size != expected.size || p->encrypted) {
            return false;
        }
    }
    return LittleFS.begin(false, "/data", 10, "spiffs");
}
