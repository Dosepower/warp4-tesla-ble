#include "bluetooth_lifecycle.h"
#include "bluetooth_memory.h"
#if TESLA_BLE_USE_NIMBLE
#include "bluetooth_nimble_lifecycle.inc"
#else
#include <new>
#ifndef TESLA_BLE_MODULE_HOST_TEST
#include <Arduino.h>
#if WARP_BLUETOOTH_COMPILED
#include <esp_bt.h>
#include <esp_bt_main.h>
// Arduino releases BLE memory in initArduino unless this weak policy hook says
// it is linked. A strong constant hook needs no BLEDevice constructor/archive.
// It reserves controller memory, NOT a running stack, on both boot choices.
extern "C" bool bleInUse() { return true; }
#endif
#endif
#if TESLA_BLE_R13_MASTER_BENCH || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
#include "tesla_ble_iram_diagnostics.h"
#include "bluetooth_master_teardown.h"
#endif

struct BluetoothLifecycle::Context {
    enum Phase { Starting, Active, Stopping, Stopped, Failed };
    std::atomic<Phase> phase{Starting};
    std::atomic<bool> stop{false}, error{false}, done{false};
    std::atomic<int> registered{0};
    std::atomic<uint32_t> since{0};
    TaskHandle_t task = nullptr;
    Handler handler = nullptr; // immutable before launch; host quiesces before destruction.
    bool controller_owned = false, host_owned = false; // worker-only
#if TESLA_BLE_MANUAL_VEHICLE_UI
    TeslaBleDiscovery discovery;
    bool gap_registered=false; // worker-only; never register/scan at boot.
#endif
#if TESLA_BLE_R13_MASTER_BENCH || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
    std::atomic<uint32_t> watermark{0};
    uint32_t last_sample = 0; // Worker-only, never emit UART diagnostics at the 20 ms hold rate.
    void sample(const char *stage) {
        const auto value = uxTaskGetStackHighWaterMark(nullptr);
        const auto old = watermark.load();
        if (!old || value < old) { watermark.store(value); }
        last_sample = millis();
        tesla_ble_iram_snapshot(stage, "sample");
    }
#endif
};
std::atomic<BluetoothLifecycle::Context *> BluetoothLifecycle::owner{nullptr};
BluetoothLifecycle::BluetoothLifecycle() = default;
BluetoothLifecycle::~BluetoothLifecycle() {
    if (!context) { return; }
    context->stop.store(true); // Close forwarded callback admission immediately.
    // A facade cannot destroy a blocked worker or an SDK-retained callback
    // context. Orphaned contexts/tasks deliberately remain pinned until reset.
    // Both entry and GATT dispatch use Context directly, never this facade.
    if ((context->task && !context->done.load(std::memory_order_acquire)) || owner.load() == context.get()) {
        context.release(); return;
    }
    if (context->task) { vTaskDelete(context->task); context->task = nullptr; }
}

bool BluetoothLifecycle::start(Handler handler) {
#if WARP_BLUETOOTH_COMPILED
    if (!bluetooth_supported() || context || latched) { return false; }
    bluetooth_memory::sample(bluetooth_memory::Stage::ContextBefore);
    context.reset(new (std::nothrow) Context);
    bluetooth_memory::sample(bluetooth_memory::Stage::ContextAfter);
    if (!context) { latched = true; return false; }
    Context *expected = nullptr;
    if (!owner.compare_exchange_strong(expected, context.get())) { latched = true; return false; }
    context->handler = handler; context->since.store(millis());
    if (xTaskCreatePinnedToCore(entry, "bluetooth", 4096, context.get(), 1, &context->task, 0) != pdPASS) {
        context->task = nullptr; latched = true;
        owner.store(nullptr); return false;
    }
    return true;
#else
    (void)handler; return false;
#endif
}
void BluetoothLifecycle::request_stop() {
    if (context && !context->stop.exchange(true)) { context->since.store(millis()); }
}
bool BluetoothLifecycle::failed() const {
#if TESLA_BLE_R13_MASTER_BENCH || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
    if (bluetooth_master_teardown::forbidden.load()) { return true; }
#endif
    return latched || (context && context->error.load());
}
bool BluetoothLifecycle::gatt_ready() const { return context && context->registered.load() == 1; }
bool BluetoothLifecycle::active() const {
    return context && !failed() && !context->stop.load() && context->phase.load() == Context::Active;
}
bool BluetoothLifecycle::stopped() const {
    return context && !failed() && context->done.load(std::memory_order_acquire) && context->phase.load() == Context::Stopped;
}
const char *BluetoothLifecycle::phase() const {
    if (failed()) { return "error"; }
    if (!context) { return "disabled"; }
    switch (context->phase.load()) {
    case Context::Starting: return "starting";
    case Context::Active: return context->stop.load() ? "stopping" : "active";
    case Context::Stopping: return "stopping";
    case Context::Stopped: return "disabled";
    default: return "error";
    }
}
void BluetoothLifecycle::supervise() {
    if (!context) { return; }
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if(context->discovery.overdue(millis())) {
        latched=true;context->discovery.cancel("timeout");request_stop();
    }
#endif
#if TESLA_BLE_R13_MASTER_BENCH || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
    if (bluetooth_master_teardown::forbidden.load()) { context->error.store(true); request_stop(); }
#endif
    const auto phase = context->phase.load();
    if (!context->done.load(std::memory_order_acquire) &&
        (phase == Context::Starting || phase == Context::Stopping || context->stop.load()) && millis() - context->since.load() > 15000) {
        latched = true; context->stop.store(true); // Never reclaim a possibly blocked SDK worker.
    }
    if (context->done.load(std::memory_order_acquire) && context->task) {
#if TESLA_BLE_MANUAL_WEB_BENCH
        r13_psram_worker_end(); // Observer must never sample a reclaimed worker.
#endif
        vTaskDelete(context->task); context->task = nullptr; // ACK, no SDK/runtime access afterwards.
    }
}
void BluetoothLifecycle::entry(void *context) { run(*static_cast<Context *>(context)); }
#if TESLA_BLE_MANUAL_VEHICLE_UI
bool BluetoothLifecycle::request_discovery() {
    return active() && context->discovery.request(millis());
}
bool BluetoothLifecycle::request_discovery(const TeslaBleDiscovery::Scope &scope) {
    return active() && context->discovery.request(millis(),scope);
}
bool BluetoothLifecycle::discovery_matches(const TeslaBleDiscovery::Scope &scope,uint32_t revision,const uint8_t *address,uint8_t type) {
    return active() && context->discovery.matches(millis(),scope,revision,address,type);
}
bool BluetoothLifecycle::discovery_busy() const { return context && context->discovery.busy(); }
bool BluetoothLifecycle::discovery_snapshot(TeslaBleDiscovery::Snapshot &out) {
    if(!context) { out={};return true; }
    return context->discovery.snapshot(out);
}
#if WARP_BLUETOOTH_COMPILED
void BluetoothLifecycle::gap(esp_gap_ble_cb_event_t event,esp_ble_gap_cb_param_t *p) {
    auto *c=owner.load();if(!c || !p) { return; }
    using D=TeslaBleDiscovery;
    if(event==ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT) { c->discovery.event(D::Event::Params,p->scan_param_cmpl.status==ESP_BT_STATUS_SUCCESS); }
    else if(event==ESP_GAP_BLE_SCAN_START_COMPLETE_EVT) { c->discovery.event(D::Event::Start,p->scan_start_cmpl.status==ESP_BT_STATUS_SUCCESS); }
    else if(event==ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT) { c->discovery.event(D::Event::Stop,p->scan_stop_cmpl.status==ESP_BT_STATUS_SUCCESS); }
    else if(event==ESP_GAP_BLE_SCAN_RESULT_EVT) {
        if(p->scan_rst.search_evt==ESP_GAP_SEARCH_INQ_CMPL_EVT) { c->discovery.event(D::Event::Done,true); }
        else if(p->scan_rst.search_evt==ESP_GAP_SEARCH_INQ_RES_EVT) {
            c->discovery.observe(p->scan_rst.ble_adv,p->scan_rst.adv_data_len,p->scan_rst.scan_rsp_len,
                p->scan_rst.bda,p->scan_rst.ble_addr_type,p->scan_rst.rssi,p->scan_rst.ble_evt_type);
        }
    }
    // No GATT open, security negotiation, key/protocol or follow-up action.
    bluetooth_memory::sample(bluetooth_memory::Stage::GapCallback,true,1000);
}
bool BluetoothLifecycle::discovery_tick(Context &c) {
    using D=TeslaBleDiscovery;
    const auto action=c.discovery.step(millis());
    if(action==D::Action::Retire) { c.error.store(true);return false; }
    if(action==D::Action::Params) {
        if(!c.gap_registered) {
            c.gap_registered=esp_ble_gap_register_callback(gap)==ESP_OK;
            if(!c.gap_registered) { c.discovery.submitted(action,false);return true; }
        }
        esp_ble_scan_params_t params{};
        params.scan_type=BLE_SCAN_TYPE_ACTIVE; // Standard scan requests only, never a connection.
        params.own_addr_type=BLE_ADDR_TYPE_PUBLIC;params.scan_filter_policy=BLE_SCAN_FILTER_ALLOW_ALL;
        params.scan_interval=0x50;params.scan_window=0x30;params.scan_duplicate=BLE_SCAN_DUPLICATE_ENABLE;
        c.discovery.submitted(action,esp_ble_gap_set_scan_params(&params)==ESP_OK);
    } else if(action==D::Action::Start) {
        c.discovery.submitted(action,esp_ble_gap_start_scanning(D::SCAN_SECONDS)==ESP_OK);
    } else if(action==D::Action::Stop) {
        c.discovery.submitted(action,esp_ble_gap_stop_scanning()==ESP_OK);
    }
    return true;
}
#endif
#endif
#if WARP_BLUETOOTH_COMPILED
void BluetoothLifecycle::gatt(esp_gattc_cb_event_t event, esp_gatt_if_t interface, esp_ble_gattc_cb_param_t *param) {
    auto *c = owner.load();
    if (!c || !param) { return; }
    if (event == ESP_GATTC_REG_EVT && param->reg.app_id == 0x5231) {
        c->registered.store(param->reg.status == ESP_GATT_OK ? 1 : -1);
#if TESLA_BLE_R13_MASTER_BENCH || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
        tesla_ble_iram_snapshot("master.gatt_register", "event", param->reg.status, true);
#endif
    } else if (!c->stop.load() && c->handler) { c->handler(event, interface, param); }
    // Self-sample after callback work; no c/task pointer used or retained here.
    bluetooth_memory::sample(bluetooth_memory::Stage::GattCallback,true,1000);
}
#endif
bool BluetoothLifecycle::teardown(Context &c) {
#if WARP_BLUETOOTH_COMPILED
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if(c.discovery.busy()) {
        c.discovery.cancel();c.error.store(true);
        while(discovery_tick(c)) { vTaskDelay(pdMS_TO_TICKS(10)); }
    }
#endif
    // Only tear down stages acquired by this worker. Any inconsistent status or
    // SDK error retains owner+context, closes admission, and requires reboot.
    if (c.host_owned) {
        auto status = esp_bluedroid_get_status();
        if (status == ESP_BLUEDROID_STATUS_ENABLED) {
            if (esp_bluedroid_disable() != ESP_OK) { return false; }
            status = esp_bluedroid_get_status();
        }
        if (status != ESP_BLUEDROID_STATUS_INITIALIZED || esp_bluedroid_deinit() != ESP_OK ||
            esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED) { return false; }
        c.host_owned = false;
    }
    if (c.controller_owned) {
        auto status = esp_bt_controller_get_status();
        if (status == ESP_BT_CONTROLLER_STATUS_ENABLED) {
            if (esp_bt_controller_disable() != ESP_OK) { return false; }
            status = esp_bt_controller_get_status();
        }
        if (status != ESP_BT_CONTROLLER_STATUS_INITED || esp_bt_controller_deinit() != ESP_OK ||
            esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) { return false; }
        c.controller_owned = false;
    }
    c.registered.store(0);
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED ||
        esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) { return false; }
    owner.store(nullptr); // Bluedroid deinit is the callback quiescence barrier.
#if TESLA_BLE_MANUAL_VEHICLE_UI
    c.discovery.quiesced();
#endif
    return true;
#else
    (void)c;
    return false;
#endif
}
void BluetoothLifecycle::run(Context &c) {
#if WARP_BLUETOOTH_COMPILED
#if TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC || TESLA_BLE_MANUAL_WEB_BENCH
    r13_psram_worker_begin();
#endif
#if TESLA_BLE_R13_MASTER_BENCH || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
    c.sample("master.start.before");
#endif
    const auto initialize = [&]() {
        if (c.stop.load()) { return true; } // Clean cancellation, not SDK failure.
        if (!r13_psram_ok() || esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE ||
            esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED) { return false; }
        esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        bool ok = esp_bt_controller_init(&config) == ESP_OK;
        bluetooth_memory::sample(bluetooth_memory::Stage::ControllerInit,true);
        c.controller_owned = ok;
        ok = ok && r13_psram_ok() && esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED &&
             esp_bt_controller_enable(ESP_BT_MODE_BLE) == ESP_OK &&
             r13_psram_ok() && esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
        bluetooth_memory::sample(bluetooth_memory::Stage::ControllerEnabled,true);
        if (!ok || c.stop.load()) { return ok; }
        ok = esp_bluedroid_init() == ESP_OK;
        bluetooth_memory::sample(bluetooth_memory::Stage::HostInit,true);
        c.host_owned = ok;
        ok = ok && r13_psram_ok() && esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED &&
             esp_bluedroid_enable() == ESP_OK && r13_psram_ok() && esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED;
        bluetooth_memory::sample(bluetooth_memory::Stage::HostEnabled,true);
        if (!ok || c.stop.load()) { return ok; }
        ok = esp_ble_gattc_register_callback(gatt) == ESP_OK && r13_psram_ok() && esp_ble_gattc_app_register(0x5231) == ESP_OK && r13_psram_ok();
        const auto begin = millis();
        while (ok && !c.stop.load() && c.registered.load() == 0 && millis() - begin < 5000) { vTaskDelay(pdMS_TO_TICKS(5)); }
        bluetooth_memory::sample(bluetooth_memory::Stage::GattReady,true);
        return ok && r13_psram_ok() && (c.stop.load() || c.registered.load() == 1);
    };
    bool ok = initialize();
    if (ok && !c.stop.load()) {
        bluetooth_memory::sample(bluetooth_memory::Stage::BtActive,true);
        c.phase.store(Context::Active);
#if TESLA_BLE_R13_MASTER_BENCH || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
        c.sample("master.active");
#endif
        while (!c.stop.load()) {
            bluetooth_memory::sample(bluetooth_memory::Stage::BtActive,true,1000);
            if (!r13_psram_ok() || esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED ||
                esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) { ok = false; break; }
#if TESLA_BLE_MANUAL_VEHICLE_UI
            if(!discovery_tick(c)) { ok=false;break; }
#endif
#if TESLA_BLE_R13_MASTER_BENCH || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
            if (millis() - c.last_sample >= 1000) { c.sample("master.hold"); }
#endif
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    c.stop.store(true); // No forwarded callbacks once initialization/hold ends, including failure.
    c.since.store(millis()); c.phase.store(Context::Stopping);
#if TESLA_BLE_R13_MASTER_BENCH || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
    c.sample("master.teardown.before");
#endif
    // A foreign pre-existing stack is never touched, and never presented as ours.
    bluetooth_memory::sample(bluetooth_memory::Stage::BtStopBefore,true);
    const bool clean = teardown(c);
    bluetooth_memory::sample(bluetooth_memory::Stage::BtStopAfter,true);
#if TESLA_BLE_R13_MASTER_BENCH || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
    c.sample("master.teardown.after");
#endif
    if (!ok || !clean || c.error.load()) { c.error.store(true); c.phase.store(Context::Failed); }
    else { c.phase.store(Context::Stopped); }
    c.done.store(true, std::memory_order_release);
    for (;;) { vTaskSuspend(nullptr); }
#else
    (void)c;
#endif
}
#if TESLA_BLE_R13_MASTER_BENCH || TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
uint32_t BluetoothLifecycle::stack_min() const { return context ? context->watermark.load() : 0; }
int BluetoothLifecycle::controller() const { return esp_bt_controller_get_status(); }
int BluetoothLifecycle::host() const { return esp_bluedroid_get_status(); }
#endif
#endif // !TESLA_BLE_USE_NIMBLE
