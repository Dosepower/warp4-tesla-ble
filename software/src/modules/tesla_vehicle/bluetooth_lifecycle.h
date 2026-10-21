#pragma once
#include "bluetooth_support.h"
#include <atomic>
#include <memory>
#if TESLA_BLE_USE_NIMBLE
#include "tesla_nimble_central.h"
#endif
#if TESLA_BLE_MANUAL_VEHICLE_UI
#include "tesla_ble_discovery.h"
#endif
#ifdef TESLA_BLE_MODULE_HOST_TEST
#include "module_platform.h"
#else
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#if WARP_BLUETOOTH_COMPILED && !TESLA_BLE_USE_NIMBLE
#include <esp_gattc_api.h>
#if TESLA_BLE_MANUAL_VEHICLE_UI
#include <esp_gap_ble_api.h>
#endif
#endif
#endif

// Sole controller/host owner. OFF is one null pointer: no SDK calls, worker,
// queues, peer, storage, protocol, diagnostics or Tesla state construction.
class BluetoothLifecycle {
public:
#if TESLA_BLE_USE_NIMBLE
    using Handler = TeslaNimbleCentral::Handler;
    TeslaNimbleCentral *central();
#elif WARP_BLUETOOTH_COMPILED
    using Handler = void (*)(esp_gattc_cb_event_t, esp_gatt_if_t, esp_ble_gattc_cb_param_t *);
#else
    using Handler = void (*)();
#endif
    BluetoothLifecycle();
    ~BluetoothLifecycle();
    bool start(Handler handler = nullptr);
    void request_stop();
    void supervise();
    bool active() const;
    bool failed() const;
    bool stopped() const;
    bool gatt_ready() const;
    const char *phase() const;
#if TESLA_BLE_MANUAL_VEHICLE_UI
    bool request_discovery();
    bool request_discovery(const TeslaBleDiscovery::Scope &);
    bool discovery_matches(const TeslaBleDiscovery::Scope &, uint32_t revision, const uint8_t *address, uint8_t type);
    bool discovery_busy() const;
    bool discovery_snapshot(TeslaBleDiscovery::Snapshot &);
#endif
#if TESLA_BLE_R13_MASTER_BENCH || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
    uint32_t stack_min() const;
    int controller() const;
    int host() const;
#endif
private:
    struct Context;
    std::unique_ptr<Context> context;
    std::atomic<bool> latched{false}; // Main supervision and Tesla teardown read this latch.
    static std::atomic<Context *> owner;
    static void entry(void *);
#if TESLA_BLE_USE_NIMBLE
    static void gatt(const TeslaNimbleEvent &);
    static void host_entry(void *);
#elif WARP_BLUETOOTH_COMPILED
    static void gatt(esp_gattc_cb_event_t, esp_gatt_if_t, esp_ble_gattc_cb_param_t *);
#endif
    static void run(Context &);
    static bool teardown(Context &);
#if TESLA_BLE_MANUAL_VEHICLE_UI && WARP_BLUETOOTH_COMPILED && !TESLA_BLE_USE_NIMBLE
    static void gap(esp_gap_ble_cb_event_t, esp_ble_gap_cb_param_t *);
    static bool discovery_tick(Context &);
#endif
};
