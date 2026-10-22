#pragma once

#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
#include <esp_gattc_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// No Tesla runtime, Arduino BLE singleton, identity or dynamically owned callback.
// This object and its callback deliberately live for the entire boot.
class TeslaBleBenchSmoke final
{
public:
    struct Report {
        const char *status = "idle";
        const char *stage = "idle";
        const char *failure_stage = "none";
        int failure_code = 0;
        int task_create_code = 0; // Exact FreeRTOS return; failure_code maps failure to ESP_ERR_NO_MEM.
        const char *teardown_stage = "none";
        int teardown_code = 0;
        bool initialization_ok = false;
        bool teardown_complete = false;
        bool worker_active = false;
        bool restart_required = false;
    };

    bool request(); // One attempt per boot, including task-creation failure.
    Report snapshot();
    void supervise(); // Main-loop watchdog; never forcibly deletes an SDK caller.

private:
    static constexpr uint16_t APP_ID = 0x424e; // Local GATTC registration ID, NOT a vehicle identity.
    static void task_entry(void *);
    static void callback(esp_gattc_cb_event_t, esp_gatt_if_t, esp_ble_gattc_cb_param_t *);
    void run();
    bool stage(const char *name, bool teardown = false);
    bool result(const char *name, int rc, bool teardown = false);
    bool wait_event(bool registration);

    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    Report report;
    bool registration_seen = false;
    int registration_code = 0;
    esp_gatt_if_t interface = ESP_GATT_IF_NONE;
    bool unregistration_seen = false;
    bool accept_events = false;
    bool timed_out = false;
    bool stage_is_teardown = false;
    TickType_t stage_started = 0;
    TickType_t attempt_started = 0;
};

extern TeslaBleBenchSmoke tesla_ble_bench_smoke;
void tesla_ble_bench_smoke_pre_setup();
void tesla_ble_bench_smoke_register_urls();
void tesla_ble_bench_smoke_loop();
#endif
