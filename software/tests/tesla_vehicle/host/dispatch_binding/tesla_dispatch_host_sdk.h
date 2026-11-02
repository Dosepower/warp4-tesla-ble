#pragma once
// TEST-ONLY host SDK boundary for compiling the UNCHANGED production binding tesla_nimble_dispatch.cpp (and its
// callers) against the existing host NimBLE fixture. Not part of any firmware build.
#ifndef TESLA_NIMBLE_DISPATCH_HOST_BINDING
#error "dispatch_binding stubs are host-test-only"
#endif
#include <chrono>
#include <cstdint>
#include "nimble_platform.h"
using esp_err_t = int;
constexpr esp_err_t ESP_ERR_INVALID_STATE = 0x103;
// Pinned NimBLE error values used by the binding (host/ble_hs.h).
constexpr int BLE_HS_EINVAL = 3, BLE_HS_EMSGSIZE = 4, BLE_HS_ETIMEOUT = 13, BLE_HS_EBUSY = 15, BLE_HS_EREJECT = 16,
              BLE_HS_ENOTSYNCED = 22;
constexpr int BLE_UUID_TYPE_32 = 32;
struct ble_uuid16_t { ble_uuid_t u{}; uint16_t value = 0; };
struct ble_uuid32_t { ble_uuid_t u{}; uint32_t value = 0; };
struct ble_gap_conn_params { uint16_t scan_itvl = 0, scan_window = 0, itvl_min = 0, itvl_max = 0, latency = 0,
                             supervision_timeout = 0, min_ce_len = 0, max_ce_len = 0; };
using ble_gap_event_fn = int(ble_gap_event *, void *);
using ble_gatt_attr_fn = int(uint16_t, const ble_gatt_error *, ble_gatt_attr *, void *);
using ble_gatt_disc_svc_fn = int(uint16_t, const ble_gatt_error *, const ble_gatt_svc *, void *);
using ble_gatt_chr_fn = int(uint16_t, const ble_gatt_error *, const ble_gatt_chr *, void *);
using ble_gatt_dsc_fn = int(uint16_t, const ble_gatt_error *, uint16_t, const ble_gatt_dsc *, void *);
// Stack classification: the test marks the calling thread as running on a PSRAM (external) stack; every other
// host address is classified internal. Models esp_cpu_get_sp()/esp_ptr_* only.
inline thread_local bool tesla_dispatch_host_external_stack = false;
inline bool esp_ptr_external_ram(const void *p) { const auto a = reinterpret_cast<uintptr_t>(p); return a >= 0x3F800000u && a < 0x3FC00000u; }
inline bool esp_ptr_internal(const void *p) { return p != nullptr && !esp_ptr_external_ram(p); }
inline void *esp_cpu_get_sp() { static int internal_probe; return tesla_dispatch_host_external_stack ? reinterpret_cast<void *>(uintptr_t{0x3F800100u}) : static_cast<void *>(&internal_probe); }
inline int64_t esp_timer_get_time() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
