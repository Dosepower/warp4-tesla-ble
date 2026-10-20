#include "tesla_ble_bench_smoke.h"
#include "tesla_ble_bench_admission.h"

#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS && (!defined(TESLA_BLE_R13_MASTER_BENCH) || !TESLA_BLE_R13_MASTER_BENCH)
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <cstring>
#include "tesla_ble_iram_diagnostics.h"

TeslaBleBenchSmoke tesla_ble_bench_smoke;

TeslaBleBenchSmoke::Report TeslaBleBenchSmoke::snapshot()
{
    portENTER_CRITICAL(&lock);
    const Report copy = report;
    portEXIT_CRITICAL(&lock);
    return copy;
}

void TeslaBleBenchSmoke::supervise()
{
    portENTER_CRITICAL(&lock);
    // Sample with the protected timestamp. A pre-lock tick can be older than
    // a concurrently published new stage, causing unsigned underflow.
    const TickType_t now = xTaskGetTickCount();
    if (report.worker_active &&
        (static_cast<TickType_t>(now - stage_started) >= pdMS_TO_TICKS(5000) ||
         (!timed_out && static_cast<TickType_t>(now - attempt_started) >= pdMS_TO_TICKS(30000)))) {
        timed_out = true;
        report.status = "restart_required";
        if (report.failure_code == ESP_OK) {
            report.failure_stage = report.stage;
            report.failure_code = ESP_ERR_TIMEOUT;
        }
        if (stage_is_teardown && report.teardown_code == ESP_OK) {
            report.teardown_stage = report.stage;
            report.teardown_code = ESP_ERR_TIMEOUT;
        }
    }
    portEXIT_CRITICAL(&lock);
}

bool TeslaBleBenchSmoke::stage(const char *name, bool teardown)
{
    supervise();
    portENTER_CRITICAL(&lock);
    const TickType_t now = xTaskGetTickCount();
    if (timed_out && !teardown) {
        portEXIT_CRITICAL(&lock);
        return false;
    }
    report.stage = name;
    stage_started = now;
    stage_is_teardown = teardown;
    portEXIT_CRITICAL(&lock);
    return true;
}

bool TeslaBleBenchSmoke::result(const char *name, int rc, bool teardown)
{
    // Also check elapsed time on return: the main task need not have run while
    // an SDK call was in progress, including the final deinit call.
    supervise();
    portENTER_CRITICAL(&lock);
    if (rc != ESP_OK) {
        if (teardown && report.teardown_code == ESP_OK) {
            report.teardown_stage = name;
            report.teardown_code = rc;
        }
        if (report.failure_code == ESP_OK) {
            report.failure_stage = name;
            report.failure_code = rc;
        }
    }
    const bool allowed = teardown || !timed_out;
    portEXIT_CRITICAL(&lock);
    return rc == ESP_OK && allowed;
}

bool TeslaBleBenchSmoke::request()
{
    portENTER_CRITICAL(&lock);
    const TickType_t now = xTaskGetTickCount();
    if (strcmp(report.status, "idle") != 0 || !tesla_bench::claim(1)) {
        portEXIT_CRITICAL(&lock);
        return false;
    }
    report.status = "running";
    report.stage = "task.create";
    report.worker_active = true;
    stage_started = attempt_started = now;
    // Policy latch: even a fully cleaned success needs a reboot for another run.
    // No memory-release API is called; this is not a claim of irreversible release.
    report.restart_required = true;
    portEXIT_CRITICAL(&lock);
    TaskHandle_t task = nullptr;
    const auto task_rc = xTaskCreatePinnedToCore(task_entry, "bench_bt_smoke", 4096, this, 1, &task, 0);
    portENTER_CRITICAL(&lock);
    report.task_create_code = task_rc;
    portEXIT_CRITICAL(&lock);
    if (task_rc != pdPASS) {
        result("task.create", ESP_ERR_NO_MEM);
        portENTER_CRITICAL(&lock);
        report.status = "failed";
        report.worker_active = false;
        report.teardown_complete = true;
        portEXIT_CRITICAL(&lock);
        return false;
    }
    return true;
}

void TeslaBleBenchSmoke::callback(esp_gattc_cb_event_t event, esp_gatt_if_t iface, esp_ble_gattc_cb_param_t *param)
{
    auto &self = tesla_ble_bench_smoke;
    // UNREG has a null parameter in the pinned IDF. Never dereference it.
    portENTER_CRITICAL(&self.lock);
    const bool registration = self.accept_events && event == ESP_GATTC_REG_EVT && param && param->reg.app_id == APP_ID;
    if (registration) {
        self.registration_code = param->reg.status;
        self.interface = param->reg.status == ESP_GATT_OK ? iface : ESP_GATT_IF_NONE;
        self.registration_seen = true;
    } else if (self.accept_events && event == ESP_GATTC_UNREG_EVT && iface == self.interface) {
        self.unregistration_seen = true;
    }
    portEXIT_CRITICAL(&self.lock);
    if (registration) {
        tesla_ble_iram_snapshot("gatt.app_register", "event", param->reg.status, true);
    }
}

bool TeslaBleBenchSmoke::wait_event(bool registration)
{
    const TickType_t start = xTaskGetTickCount();
    do {
        portENTER_CRITICAL(&lock);
        const bool seen = registration ? registration_seen : unregistration_seen;
        const int rc = registration ? registration_code : ESP_OK;
        const bool expired = timed_out;
        portEXIT_CRITICAL(&lock);
        if (expired && registration) { return false; }
        if (seen) { return result(registration ? "gatt.register_event" : "gatt.unregister_event", rc, !registration); }
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (static_cast<TickType_t>(xTaskGetTickCount() - start) < pdMS_TO_TICKS(2000));
    result(registration ? "gatt.register_event" : "gatt.unregister_event", ESP_ERR_TIMEOUT, !registration);
    portENTER_CRITICAL(&lock);
    timed_out = true;
    portEXIT_CRITICAL(&lock);
    return false;
}

void TeslaBleBenchSmoke::task_entry(void *context)
{
    static_cast<TeslaBleBenchSmoke *>(context)->run();
    vTaskDelete(nullptr);
}

void TeslaBleBenchSmoke::run()
{
    bool controller_initialized = false, controller_enabled = false;
    bool host_initialized = false, host_enabled = false, initialized = false;
    // Record successful ownership even if the supervisor quarantined an SDK
    // call while it was blocked. If it returns, only unwind; never advance init.
    const auto initialize = [this](const char *name, auto submit, bool &owned) {
        if (!stage(name)) { return false; }
        const int rc = submit();
        owned = rc == ESP_OK;
        return result(name, rc);
    };
    if (!stage("ownership.check")) {
        // Task scheduling itself exceeded the budget: do not touch Bluetooth.
    } else if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE ||
        esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        result("ownership.check", ESP_ERR_INVALID_STATE);
    } else {
        // Real pinned SDK defaults, BLE-only. No Classic/host/controller memory
        // release: both release APIs are one-way within a boot.
        esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        do {
            if (!initialize("controller.init", [&cfg]() { return esp_bt_controller_init(&cfg); }, controller_initialized)) { break; }
            if (!initialize("controller.enable", []() { return esp_bt_controller_enable(ESP_BT_MODE_BLE); }, controller_enabled)) { break; }
            if (!initialize("host.init", []() { return esp_bluedroid_init(); }, host_initialized)) { break; }
            if (!initialize("host.enable", []() { return esp_bluedroid_enable(); }, host_enabled)) { break; }
            portENTER_CRITICAL(&lock);
            accept_events = true;
            portEXIT_CRITICAL(&lock);
            if (!stage("gatt.callback_register")) { break; }
            if (!result("gatt.callback_register", esp_ble_gattc_register_callback(callback))) { break; }
            if (!stage("gatt.app_register")) { break; }
            if (!result("gatt.app_register", esp_ble_gattc_app_register(APP_ID))) { break; }
            if (!stage("gatt.register_event")) { break; }
            initialized = wait_event(true);
        } while (false);
    }

    portENTER_CRITICAL(&lock);
    const esp_gatt_if_t registered_interface = interface;
    report.initialization_ok = initialized;
    portEXIT_CRITICAL(&lock);
    if (registered_interface != ESP_GATT_IF_NONE) {
        stage("gatt.app_unregister", true);
        if (result("gatt.app_unregister", esp_ble_gattc_app_unregister(registered_interface), true)) {
            stage("gatt.unregister_event", true);
            wait_event(false);
        }
    }
    // Do not deinit a still-enabled layer or disable its dependencies if a
    // teardown call fails. Ownership remains quarantined until restart.
    bool clean = true;
    if (host_enabled) {
        stage("host.disable", true);
        clean = result("host.disable", esp_bluedroid_disable(), true);
    }
    if (clean && host_initialized) {
        stage("host.deinit", true);
        clean = result("host.deinit", esp_bluedroid_deinit(), true);
    }
    if (clean && controller_enabled) {
        stage("controller.disable", true);
        clean = result("controller.disable", esp_bt_controller_disable(), true);
    }
    if (clean && controller_initialized) {
        stage("controller.deinit", true);
        clean = result("controller.deinit", esp_bt_controller_deinit(), true);
    }
    portENTER_CRITICAL(&lock);
    accept_events = false;
    report.teardown_complete = clean && report.teardown_code == ESP_OK;
    report.worker_active = false;
    report.status = timed_out || !report.teardown_complete ? "restart_required" : (initialized ? "succeeded" : "failed");
    report.stage = "complete";
    portEXIT_CRITICAL(&lock);
}
#endif
