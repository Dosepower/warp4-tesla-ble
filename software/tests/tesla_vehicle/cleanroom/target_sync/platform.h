#pragma once
// Select only the real ESP32 CheckedMutex semaphore branch, not the SDK/ABI.
// The semaphore host seam asserts every marked main pass requests zero ticks.
#define ESP_PLATFORM 1
#include "tesla_ble_clean_platform.h"
#undef ESP_PLATFORM
#undef pdTRUE
#define TESLA_BLE_TARGET_SYNC_TEST 1
