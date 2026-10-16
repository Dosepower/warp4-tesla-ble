#include "tesla_ble_bench_discovery.h"
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <esp_random.h>
#include <cstring>
#include "tesla_ble_bench_admission.h"
#include "tesla_ble_iram_diagnostics.h"
#include "tesla_ble_uuid.h"

TeslaBleBenchDiscovery tesla_ble_bench_discovery;
namespace {
uint32_t pair_now() { return xTaskGetTickCount()*portTICK_PERIOD_MS; }
tesla_clean::Random bench_random() { return {[](void *,unsigned char *p,size_t n) {
    // Pinned esp_random.h: true entropy requires enabled Wi-Fi/Bluetooth.
    // Never enable the bootloader SAR source: it conflicts with ADC users.
    if (esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_ENABLED) { return -1; }
    esp_fill_random(p,n); return 0;
},nullptr}; }
}

TeslaBleBenchDiscovery::Report TeslaBleBenchDiscovery::snapshot()
{
    portENTER_CRITICAL(&lock);
    Report copy = report;
    if (pairing) {
        copy.pair_state=pairing->state(); copy.local_key_present=pairing->key_present();
        copy.request_may_have_reached_tesla=pairing->exposed(); copy.tesla_removal_user_claim=pairing->revoked_by_user();
        copy.pair_wait_expired=pairing->wait_expired(); copy.pair_information=pairing->information();
        if (!report.worker_active) { memcpy(copy.public_key_hex,pairing->public_key_hex(),sizeof(copy.public_key_hex)); }
    }
    portEXIT_CRITICAL(&lock);
    return copy;
}

int TeslaBleBenchDiscovery::request_scan()
{
    portENTER_CRITICAL(&lock);
    if (report.scan_attempted || !tesla_bench::claim(2)) { portEXIT_CRITICAL(&lock); return 409; }
    report.scan_attempted = true;
    report.action = "scan";
    portEXIT_CRITICAL(&lock);
    return launch();
}

int TeslaBleBenchDiscovery::request_probe(unsigned selected)
{
    portENTER_CRITICAL(&lock);
    const TickType_t now = xTaskGetTickCount();
    if (pairing || !report.scan_attempted || report.probe_attempted || report.worker_active ||
        strcmp(report.status, "succeeded") || !report.teardown_complete || report.restart_required ||
        selected >= report.target_count || !report.targets[selected].connectable ||
        static_cast<TickType_t>(now - scan_finished) >= pdMS_TO_TICKS(120000)) {
        portEXIT_CRITICAL(&lock); return 409;
    }
    report.probe_attempted = true;
    report.action = "probe";
    report.selected_target = selected;
    report.restart_required = true; // Terminal probe consumes this boot.
    portEXIT_CRITICAL(&lock);
    return launch();
}

int TeslaBleBenchDiscovery::request_pair_prepare(unsigned selected,const char *vin)
{
    const auto s=snapshot(); char expected[19];
    if (selected>=s.target_count || !BenchPairing::vehicle_name(vin,expected) || strcmp(s.targets[selected].local_name,expected)) { return 409; }
    std::unique_ptr<BenchPairing> candidate(new(std::nothrow) BenchPairing(pair_disk,bench_random()));
    std::unique_ptr<PairRx> rx(new(std::nothrow) PairRx);
    if (!candidate || !rx) { return 503; }
    const auto &target=s.targets[selected];
    if (!candidate->bind(vin,target.local_name,target.address,target.address_type,target.connectable)) { return 409; }
    portENTER_CRITICAL(&lock);
    if (pairing || report.worker_active || report.restart_required || strcmp(report.status,"succeeded") ||
        !report.teardown_complete || report.probe_attempted || static_cast<TickType_t>(xTaskGetTickCount()-scan_finished)>=pdMS_TO_TICKS(120000)) {
        portEXIT_CRITICAL(&lock); return 409;
    }
    pairing=std::move(candidate); pair_rx=std::move(rx); report.selected_target=selected; report.vin_bound=true; report.action="pair_prepare"; report.worker_active=true;
    portEXIT_CRITICAL(&lock); return launch();
}
int TeslaBleBenchDiscovery::request_pair_start()
{
    portENTER_CRITICAL(&lock);
    if (!pairing || report.worker_active || report.restart_required || !report.teardown_complete || report.pair_start_attempted ||
        strcmp(pairing->state(),"key_ready") || static_cast<TickType_t>(xTaskGetTickCount()-scan_finished)>=pdMS_TO_TICKS(120000)) { portEXIT_CRITICAL(&lock); return 409; }
    report.pair_start_attempted=true; report.action="pair_start"; report.worker_active=true;
    portEXIT_CRITICAL(&lock); return launch();
}
int TeslaBleBenchDiscovery::request_pair_verify()
{
    portENTER_CRITICAL(&lock);
    if (!pairing || report.worker_active || report.restart_required || !report.teardown_complete || report.pair_verify_attempted ||
        !pairing->key_present() || static_cast<TickType_t>(xTaskGetTickCount()-scan_finished)>=pdMS_TO_TICKS(120000)) { portEXIT_CRITICAL(&lock); return 409; }
    report.pair_verify_attempted=true; report.action="pair_verify"; report.worker_active=true;
    portEXIT_CRITICAL(&lock); return launch();
}
int TeslaBleBenchDiscovery::request_pair_rehearsal()
{
    portENTER_CRITICAL(&lock);
    if (!pairing || !pair_rx || report.worker_active || report.restart_required || !report.teardown_complete || report.pair_rehearsal_attempted ||
        strcmp(pairing->state(),"key_ready") || pairing->exposed()) { portEXIT_CRITICAL(&lock); return 409; }
    report.pair_rehearsal_attempted=true; report.action="pair_rehearsal"; report.worker_active=true;
    portEXIT_CRITICAL(&lock); return launch();
}
bool TeslaBleBenchDiscovery::resource_sample(unsigned index,ResourceSample &out)
{
    portENTER_CRITICAL(&lock);
    const bool found=pair_rx && index<pair_rx->sample_count;
    if (found) { out=pair_rx->samples[index]; }
    portEXIT_CRITICAL(&lock); return found;
}
int TeslaBleBenchDiscovery::request_pair_delete()
{
    portENTER_CRITICAL(&lock);
    if (!pairing || report.worker_active || !report.teardown_complete || report.pair_delete_attempted || timed_out) { portEXIT_CRITICAL(&lock); return 409; }
    report.pair_delete_attempted=true; report.action="pair_delete"; report.worker_active=true;
    if (!report.resource_gate_failed) {
        report.failure_code=report.teardown_code=0; report.failure_stage=report.teardown_stage="none"; report.restart_required=false;
    } // Explicit cleanup may run after a resource breach; it cannot pardon it.
    portEXIT_CRITICAL(&lock); return launch();
}
int TeslaBleBenchDiscovery::confirm_pair_revoke()
{
    portENTER_CRITICAL(&lock);
    if (!pairing || report.worker_active) { portEXIT_CRITICAL(&lock); return 409; }
    pairing->confirm_tesla_removal(); portEXIT_CRITICAL(&lock); return 200;
}
int TeslaBleBenchDiscovery::launch()
{
    portENTER_CRITICAL(&lock);
    if (report.resource_gate_failed && strcmp(report.action,"pair_delete")) { portEXIT_CRITICAL(&lock); return 409; }
    report.status = "running";
    report.transport_ready=false;
    report.stage = "task.create";
    report.worker_active = true;
    report.teardown_complete = false;
    report.measurements_valid = false;
    report.stack_min_bytes = 0;
    report.exec_free = report.exec_largest = report.exec_min = 0;
    report.internal8_free = report.internal8_largest = report.internal8_min = 0;
    report.psram_free = report.psram_largest = report.psram_min = 0;
    stage_started = attempt_started = xTaskGetTickCount();
    for (auto &e : events) { e = {}; }
    report.resource_samples_dropped=0;
    if (pair_rx) { pair_rx->sample_count=0; }
    portEXIT_CRITICAL(&lock);
    TaskHandle_t task = nullptr;
    const bool large=pairing && strncmp(report.action,"pair_",5)==0;
    const unsigned stack_bytes=!strcmp(report.action,"pair_rehearsal")?REHEARSAL_WORKER_BYTES:(large?PAIR_WORKER_BYTES:DISCOVERY_WORKER_BYTES);
    portENTER_CRITICAL(&lock); report.worker_stack_bytes=stack_bytes; portEXIT_CRITICAL(&lock);
    const int rc = xTaskCreatePinnedToCore(task_entry, "bench_bt_discover", stack_bytes, this, 1, &task, 0);
    portENTER_CRITICAL(&lock);
    report.task_create_code = rc;
    if (rc != pdPASS) {
        if (pairing) { pairing->fail(); }
        if (!report.failure_code) {
            report.failure_stage = "task.create";
            report.failure_code = ESP_ERR_NO_MEM;
        }
        report.status = "failed";
        report.worker_active = false;
        report.teardown_complete = true;
        report.restart_required = true;
    }
    portEXIT_CRITICAL(&lock);
    return rc == pdPASS ? 202 : 503;
}

void TeslaBleBenchDiscovery::supervise()
{
    portENTER_CRITICAL(&lock);
    const TickType_t now = xTaskGetTickCount();
    if (report.worker_active && (static_cast<TickType_t>(now - stage_started) >= pdMS_TO_TICKS(stage_budget_ms) ||
        (!timed_out && static_cast<TickType_t>(now - attempt_started) >= pdMS_TO_TICKS(!strcmp(report.action,"pair_start")?90000:(!strcmp(report.action, "scan") ? 60000 : 30000))))) {
        timed_out = true;
        report.status = "restart_required";
        report.restart_required = true;
        if (!report.failure_code) { report.failure_stage = report.stage; report.failure_code = ESP_ERR_TIMEOUT; }
        if (teardown_stage && !report.teardown_code) { report.teardown_stage = report.stage; report.teardown_code = ESP_ERR_TIMEOUT; }
    }
    portEXIT_CRITICAL(&lock);
}

bool TeslaBleBenchDiscovery::measure(const char *name, const char *phase, int code, bool gate)
{
    multi_heap_info_t exec{}, internal8{}, psram{};
    heap_caps_get_info(&exec, MALLOC_CAP_EXEC);
    heap_caps_get_info(&internal8, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    heap_caps_get_info(&psram, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const unsigned stack = uxTaskGetStackHighWaterMark(nullptr); // Pinned Xtensa StackType_t is one byte.
    portENTER_CRITICAL(&lock);
    report.measurements_valid = true;
    report.exec_free = exec.total_free_bytes; report.exec_largest = exec.largest_free_block; report.exec_min = exec.minimum_free_bytes;
    report.internal8_free = internal8.total_free_bytes; report.internal8_largest = internal8.largest_free_block; report.internal8_min = internal8.minimum_free_bytes;
    report.psram_free = psram.total_free_bytes; report.psram_largest = psram.largest_free_block; report.psram_min = psram.minimum_free_bytes;
    if (!report.stack_min_bytes || stack < report.stack_min_bytes) { report.stack_min_bytes = stack; }
    const unsigned mask=(internal8.total_free_bytes<MIN_INTERNAL8_FREE?1u:0u) |
        (internal8.largest_free_block<MIN_INTERNAL8_LARGEST?2u:0u) |
        (internal8.minimum_free_bytes<MIN_INTERNAL8_MIN?4u:0u) | (stack<MIN_WORKER_WATERMARK?8u:0u);
    if (gate && mask && !report.resource_gate_failed) {
        report.resource_gate_failed=true; report.resource_gate_mask=mask;
        report.resource_failure={name,phase,code,static_cast<unsigned>(internal8.total_free_bytes),static_cast<unsigned>(internal8.largest_free_block),static_cast<unsigned>(internal8.minimum_free_bytes),stack};
        if (!report.failure_code) { report.failure_stage=name; report.failure_code=ESP_ERR_NO_MEM; }
        report.restart_required=true; report.transport_ready=false;
    }
    if (pair_rx) {
        if (pair_rx->sample_count<32) { pair_rx->samples[pair_rx->sample_count++]={name,phase,code,static_cast<unsigned>(internal8.total_free_bytes),static_cast<unsigned>(internal8.largest_free_block),static_cast<unsigned>(internal8.minimum_free_bytes),stack}; }
        else { ++report.resource_samples_dropped; }
    }
    const bool allowed=!report.resource_gate_failed;
    portEXIT_CRITICAL(&lock);
    // Memory-only sampling in nested worker paths. Stage/error fields and these
    // measurements are published by the separate HTTP task, not deep newlib
    // formatting while GATT/crypto frames remain live on the 8KiB worker stack.
    return allowed;
}

bool TeslaBleBenchDiscovery::stage(const char *name, bool teardown)
{
    supervise();
    portENTER_CRITICAL(&lock);
    if ((timed_out || report.failure_code || report.resource_gate_failed) && !teardown) { portEXIT_CRITICAL(&lock); return false; }
    report.stage = name;
    stage_started = xTaskGetTickCount();
    teardown_stage = teardown;
    stage_budget_ms = !strcmp(name,"pair.card_wait")?62000:(!strcmp(name,"pair.verify_wait")?7000:(!strcmp(name,"pair.prepare")?15000:(!strcmp(name, "scan.complete_event") ? 32000 : 5000)));
    portEXIT_CRITICAL(&lock);
    const bool resources_ok=measure(name, "before", 0, !teardown);
    return teardown || resources_ok;
}

bool TeslaBleBenchDiscovery::result(const char *name, int rc, bool teardown)
{
    supervise();
    measure(name, "after", rc, !teardown);
    portENTER_CRITICAL(&lock);
    if (rc != ESP_OK) {
        if (!report.failure_code) { report.failure_stage = name; report.failure_code = rc; }
        if (teardown && !report.teardown_code) { report.teardown_stage = name; report.teardown_code = rc; }
    }
    if (security_requested && !report.failure_code) { report.failure_stage = "security.request"; report.failure_code = ESP_ERR_INVALID_STATE; }
    const bool ok = rc == ESP_OK && (teardown || (!timed_out && !report.failure_code));
    portEXIT_CRITICAL(&lock);
    return ok;
}

bool TeslaBleBenchDiscovery::wait(Event event, const char *name, unsigned ms, bool teardown)
{
    if (!stage(name, teardown)) { return false; }
    const TickType_t start = xTaskGetTickCount();
    do {
        supervise();
        portENTER_CRITICAL(&lock);
        const Outcome outcome = events[event];
        const bool expired = timed_out, security = security_requested;
        const int security_rc = security_code;
        portEXIT_CRITICAL(&lock);
        if (!teardown && security) { result("security.reject", security_rc); return result("security.request", ESP_ERR_INVALID_STATE); }
        if (expired && !teardown) { return false; }
        if (outcome.seen) { return result(name, outcome.code, teardown); }
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (static_cast<TickType_t>(xTaskGetTickCount() - start) < pdMS_TO_TICKS(ms));
    result(name, ESP_ERR_TIMEOUT, teardown);
    portENTER_CRITICAL(&lock);
    timed_out = true; report.restart_required = true;
    portEXIT_CRITICAL(&lock);
    return false;
}

void TeslaBleBenchDiscovery::observe(const uint8_t *data, size_t adv_length, size_t rsp_length,
                                    const uint8_t *address, uint8_t type, int rssi, esp_ble_evt_type_t event)
{
    // Pinned Bluedroid supplies ADV || SCAN_RSP and retains the original ADV
    // event type. Never infer connectability from an unassociated SCAN_RSP.
    if (adv_length > 31 || rsp_length > 31 || type > 1 || event > ESP_BLE_EVT_NON_CONN_ADV) { return; }
    char name[19]{};
    const auto parse = [&name](const uint8_t *bytes, size_t length) {
        for (size_t offset = 0; offset < length;) {
            const size_t n = bytes[offset];
            if (!n) { // Zero padding ends this AD block, not the other block.
                for (; offset < length; ++offset) { if (bytes[offset]) { return false; } }
                break;
            }
            if (n > length - offset - 1) { return false; }
            const uint8_t ad_type = bytes[offset + 1];
            if (ad_type == 0x08 || ad_type == 0x09) {
                // A shortened name alone is not a candidate; a complete name
                // in the other block can still supply the exact official shape.
                if (ad_type == 0x08 && n != 19) { offset += n + 1; continue; }
                const auto *value = bytes + offset + 2;
                if (n != 19 || value[0] != 'S' || value[17] != 'C') { return false; }
                for (unsigned i = 1; i <= 16; ++i) {
                    if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) { return false; }
                }
                if (name[0] && memcmp(name, value, 18)) { return false; }
                memcpy(name, value, 18);
            }
            offset += n + 1;
        }
        return true;
    };
    if (!parse(data, adv_length) || !parse(data + adv_length, rsp_length) || !name[0]) { return; }
    const bool connectable = event == ESP_BLE_EVT_CONN_ADV || event == ESP_BLE_EVT_CONN_DIR_ADV;
    // Caller holds lock; fixed storage, no callback allocations or guessed VIN.
    Target *target = nullptr;
    for (unsigned i = 0; i < report.target_count; ++i) {
        if (report.targets[i].address_type == type && !memcmp(report.targets[i].address, address, 6)) { target = &report.targets[i]; break; }
    }
    if (!target) {
        if (report.target_count == 8) { ++report.dropped_targets; return; }
        target = &report.targets[report.target_count++];
        memcpy(target->address, address, 6); target->address_type = type;
    }
    target->rssi = rssi; target->connectable = connectable; memcpy(target->local_name, name, sizeof(name));
}

void TeslaBleBenchDiscovery::gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p)
{
    if (!p) { return; }
    auto &s = tesla_ble_bench_discovery;
    portENTER_CRITICAL(&s.lock);
    const bool accepted = s.accept_events;
    if (accepted && !strcmp(s.report.action, "scan")) {
        if (event == ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT) { s.events[Params] = {true, p->scan_param_cmpl.status}; }
        else if (event == ESP_GAP_BLE_SCAN_START_COMPLETE_EVT) { s.events[ScanStart] = {true, p->scan_start_cmpl.status}; }
        else if (event == ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT) { s.events[ScanStop] = {true, p->scan_stop_cmpl.status}; }
        else if (event == ESP_GAP_BLE_SCAN_RESULT_EVT && s.scanning) {
            if (p->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
                s.observe(p->scan_rst.ble_adv, p->scan_rst.adv_data_len, p->scan_rst.scan_rsp_len,
                          p->scan_rst.bda, p->scan_rst.ble_addr_type, p->scan_rst.rssi, p->scan_rst.ble_evt_type);
            } else if (p->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) { s.events[ScanDone] = {true, ESP_OK}; }
        }
    }
    const bool reject = accepted && event == ESP_GAP_BLE_SEC_REQ_EVT;
    if (reject) { s.security_requested = true; s.report.security_request_seen = true; }
    portEXIT_CRITICAL(&s.lock);
    if (reject) {
        // IDF's nonblocking negative security reply: never authorize SMP/bonding.
        const int rc = esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr, false);
        portENTER_CRITICAL(&s.lock);
        s.security_code = rc; s.report.security_reply_code = rc;
        portEXIT_CRITICAL(&s.lock);
    }
}

void TeslaBleBenchDiscovery::gatt_callback(esp_gattc_cb_event_t event, esp_gatt_if_t iface, esp_ble_gattc_cb_param_t *p)
{
    auto &s = tesla_ble_bench_discovery;
    portENTER_CRITICAL(&s.lock);
    if (!s.accept_events) { portEXIT_CRITICAL(&s.lock); return; }
    if (event == ESP_GATTC_REG_EVT && p && p->reg.app_id == APP_ID) {
        s.events[Register] = {true, p->reg.status};
        if (p->reg.status == ESP_GATT_OK) { s.interface = iface; }
    } else if (iface == s.interface && iface != ESP_GATT_IF_NONE) {
        const auto &target = s.report.targets[s.report.selected_target];
        if (event == ESP_GATTC_UNREG_EVT) { s.events[Unregister] = {true, ESP_OK}; }
        else if (p && event == ESP_GATTC_OPEN_EVT && s.opening && !memcmp(p->open.remote_bda, target.address, 6)) {
            s.events[Open] = {true, p->open.status};
            if (p->open.status == ESP_GATT_OK) { s.connection = p->open.conn_id; s.link_open = true; }
        } else if (p && event == ESP_GATTC_CANCEL_OPEN_EVT && !memcmp(p->cancel_open.remote_bda, target.address, 6)) {
            s.events[Cancel] = {true, p->cancel_open.status};
        } else if (p && event == ESP_GATTC_CLOSE_EVT && s.link_open && p->close.conn_id == s.connection && !memcmp(p->close.remote_bda, target.address, 6)) {
            s.events[Close] = {true, p->close.status};
            if (p->close.status == ESP_GATT_OK) { s.link_open = false; }
            if (s.pair_action() && !s.teardown_stage && !s.report.failure_code) {
                s.report.failure_code=ESP_FAIL; s.report.failure_stage="pair.disconnect";
            }
        } else if (p && event == ESP_GATTC_DISCONNECT_EVT && s.link_open && p->disconnect.conn_id == s.connection && !memcmp(p->disconnect.remote_bda, target.address, 6)) {
            s.link_open = false; s.events[Close] = {true, ESP_OK};
            if (!s.teardown_stage && !s.report.failure_code) { s.report.failure_stage = "gatt.disconnect"; s.report.failure_code = p->disconnect.reason; }
        } else if (p && s.link_open && event == ESP_GATTC_DIS_SRVC_CMPL_EVT && p->dis_srvc_cmpl.conn_id == s.connection) {
            s.events[Discover] = {true, p->dis_srvc_cmpl.status};
        } else if (p && s.link_open && event == ESP_GATTC_SEARCH_CMPL_EVT && p->search_cmpl.conn_id == s.connection) {
            s.events[Search] = {true, p->search_cmpl.status};
        } else if (s.pair_action() && p && s.link_open) {
            if (event==ESP_GATTC_REG_FOR_NOTIFY_EVT && p->reg_for_notify.handle==s.report.notify_handle) { s.events[Subscribe]={true,p->reg_for_notify.status}; }
            else if (event==ESP_GATTC_WRITE_DESCR_EVT && p->write.conn_id==s.connection && p->write.handle==s.report.cccd_handle) { s.events[Descriptor]={true,p->write.status}; }
            else if (event==ESP_GATTC_WRITE_CHAR_EVT && p->write.conn_id==s.connection && p->write.handle==s.report.write_handle) { s.events[Write]={true,p->write.status}; }
            else if (event==ESP_GATTC_NOTIFY_EVT && p->notify.conn_id==s.connection && p->notify.handle==s.report.notify_handle &&
                     p->notify.is_notify && !memcmp(p->notify.remote_bda,target.address,6)) {
                if (!p->notify.value || p->notify.value_len>sizeof(s.pair_rx->incoming)-s.pair_rx->length) {
                    if (!s.report.failure_code) { s.report.failure_code=ESP_ERR_NO_MEM; s.report.failure_stage="pair.rx_overflow"; }
                } else {
                    memcpy(s.pair_rx->incoming+s.pair_rx->length,p->notify.value,p->notify.value_len); s.pair_rx->length+=p->notify.value_len; s.pair_rx->received_ms=pair_now();
                }
            }
        }
    }
    portEXIT_CRITICAL(&s.lock);
}

void TeslaBleBenchDiscovery::scan()
{
    esp_ble_scan_params_t params{};
    params.scan_type = BLE_SCAN_TYPE_ACTIVE; // Official pinned Tesla Linux workflow; standard BLE scan requests only.
    params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    params.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    params.scan_interval = 0x10; params.scan_window = 0x10;
    params.scan_duplicate = BLE_SCAN_DUPLICATE_ENABLE;
    if (!stage("scan.params") || !result("scan.params", esp_ble_gap_set_scan_params(&params)) || !wait(Params, "scan.params_event")) { return; }
    if (!stage("scan.start")) { return; }
    portENTER_CRITICAL(&lock); scanning = true; portEXIT_CRITICAL(&lock);
    const int rc = esp_ble_gap_start_scanning(30);
    if (!result("scan.start", rc) || !wait(ScanStart, "scan.start_event") || !wait(ScanDone, "scan.complete_event", 31500)) { return; }
}

void TeslaBleBenchDiscovery::probe()
{
    // Pinned SMP may automatically encrypt a peer's security request using an
    // existing bond, without asking the GAP application for approval. Refuse
    // ALL pre-existing BLE bonds; never delete them or reuse authenticated links.
    if (!stage("security.bond_check")) { return; }
    const int bonds = esp_ble_get_bond_device_num();
    portENTER_CRITICAL(&lock); report.ble_bond_count = bonds; portEXIT_CRITICAL(&lock);
    if (!result("security.bond_check", bonds < 0 ? bonds : (bonds ? ESP_ERR_INVALID_STATE : ESP_OK))) { return; }
    if (!stage("gatt.callback_register") || !result("gatt.callback_register", esp_ble_gattc_register_callback(gatt_callback)) ||
        !stage("gatt.app_register") || !result("gatt.app_register", esp_ble_gattc_app_register(APP_ID)) || !wait(Register, "gatt.register_event")) { return; }
    const auto target = snapshot().targets[snapshot().selected_target];
    uint8_t address[6]; memcpy(address, target.address, 6);
    if (!stage("gatt.open")) { return; }
    portENTER_CRITICAL(&lock); opening = true; portEXIT_CRITICAL(&lock);
    if (!result("gatt.open", esp_ble_gattc_open(interface, address, static_cast<esp_ble_addr_type_t>(target.address_type), true)) ||
        !wait(Open, "gatt.open_event") || !wait(Discover, "gatt.discover_event")) { return; }
    auto service_uuid = tesla_ble_uuid(0x11);
    if (!stage("gatt.search") || !result("gatt.search", esp_ble_gattc_search_service(interface, connection, &service_uuid)) || !wait(Search, "gatt.search_event")) { return; }
    esp_gattc_service_elem_t service{};
    esp_gattc_char_elem_t write{}, notify{};
    esp_gattc_descr_elem_t cccd{};
    uint16_t count = 1;
    if (!stage("gatt.service") || !result("gatt.service", esp_ble_gattc_get_service(interface, connection, &service_uuid, &service, &count, 0))) { return; }
    if (count != 1) { result("gatt.service_count", ESP_ERR_NOT_FOUND); return; }
    count = 1;
    if (!stage("gatt.write_metadata") || !result("gatt.write_metadata", esp_ble_gattc_get_char_by_uuid(interface, connection, service.start_handle, service.end_handle, tesla_ble_uuid(0x12), &write, &count))) { return; }
    if (count != 1 || !(write.properties & ESP_GATT_CHAR_PROP_BIT_WRITE)) { result("gatt.write_properties", ESP_ERR_NOT_FOUND); return; }
    count = 1;
    if (!stage("gatt.notify_metadata") || !result("gatt.notify_metadata", esp_ble_gattc_get_char_by_uuid(interface, connection, service.start_handle, service.end_handle, tesla_ble_uuid(0x13), &notify, &count))) { return; }
    if (count != 1 || !(notify.properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY)) { result("gatt.notify_properties", ESP_ERR_NOT_FOUND); return; }
    esp_bt_uuid_t descriptor_uuid{}; descriptor_uuid.len = ESP_UUID_LEN_16; descriptor_uuid.uuid.uuid16 = 0x2902;
    count = 1;
    if (!stage("gatt.cccd_metadata") || !result("gatt.cccd_metadata", esp_ble_gattc_get_descr_by_char_handle(interface, connection, notify.char_handle, descriptor_uuid, &cccd, &count))) { return; }
    if (count != 1) { result("gatt.cccd_count", ESP_ERR_NOT_FOUND); return; }
    portENTER_CRITICAL(&lock);
    report.transport_ready = link_open && !security_requested && !report.failure_code && !timed_out;
    report.write_handle = write.char_handle; report.notify_handle = notify.char_handle; report.cccd_handle = cccd.handle;
    portEXIT_CRITICAL(&lock);
    // Metadata only. No subscription, CCCD write, characteristic read/write,
    // Tesla framing/session/key generation, wake, command, pairing or SoC.
}

void TeslaBleBenchDiscovery::task_entry(void *context)
{
    static_cast<TeslaBleBenchDiscovery *>(context)->run();
    vTaskDelete(nullptr);
}

void TeslaBleBenchDiscovery::run()
{
    if (!strcmp(report.action,"pair_delete")) {
        const char *name="pair.local_delete";
        const bool cleanup=report.resource_gate_failed;
        bool clean=false;
        if (stage(name,cleanup)) { clean=result(name,pairing->delete_local(true)?ESP_OK:ESP_FAIL,cleanup); }
        measure("complete","after",0,false);
        portENTER_CRITICAL(&lock); report.worker_active=false; report.teardown_complete=clean;
        report.status=report.failure_code?"failed":"succeeded"; report.stage="complete";
        report.restart_required|=timed_out; portEXIT_CRITICAL(&lock); return;
    }
    bool controller_initialized = false, controller_enabled = false, host_initialized = false, host_enabled = false;
    const auto initialize = [this](const char *name, auto submit, bool &owned) {
        if (!stage(name)) { return false; }
        const int rc = submit(); owned = rc == ESP_OK; return result(name, rc);
    };
    if (stage("ownership.check")) {
        if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE || esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED) {
            result("ownership.check", ESP_ERR_INVALID_STATE);
        } else {
            esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
            do {
                if (!initialize("controller.init", [&cfg]() { return esp_bt_controller_init(&cfg); }, controller_initialized) ||
                    !initialize("controller.enable", []() { return esp_bt_controller_enable(ESP_BT_MODE_BLE); }, controller_enabled)) { break; }
                if (!strcmp(report.action,"pair_prepare")) {
                    if (stage("pair.prepare")) { result("pair.prepare",pairing->prepare()?ESP_OK:ESP_FAIL); }
                    break; // Persisted key; no host, scan, connection or protocol payload.
                }
                if (!initialize("host.init", []() { return esp_bluedroid_init(); }, host_initialized) ||
                    !initialize("host.enable", []() { return esp_bluedroid_enable(); }, host_enabled)) { break; }
                if (!strcmp(report.action,"pair_rehearsal")) {
                    // Structurally disjoint BEFORE GAP/GATT registration, scan,
                    // connection, subscription, and every protocol transmitter.
                    if (stage("rehearsal.local")) { result("rehearsal.local",pairing->rehearse_local()?ESP_OK:ESP_FAIL); }
                    break;
                }
                portENTER_CRITICAL(&lock); accept_events = true; portEXIT_CRITICAL(&lock);
                if (!stage("gap.callback_register") || !result("gap.callback_register", esp_ble_gap_register_callback(gap_callback))) { break; }
                if (!strcmp(report.action, "scan")) { scan(); } else { probe(); if (pair_action() && report.transport_ready) { pair_transport(); } }
            } while (false);
        }
    }
    portENTER_CRITICAL(&lock);
    const bool stop_scan = scanning && !events[ScanDone].seen;
    const bool cancel = opening && !events[Open].seen;
    portEXIT_CRITICAL(&lock);
    if (stop_scan) {
        stage("scan.stop", true);
        if (result("scan.stop", esp_ble_gap_stop_scanning(), true)) { wait(ScanStop, "scan.stop_event", 2000, true); }
    }
    if (cancel) {
        esp_ble_gattc_cancel_open_params_t params{};
        params.gattc_if = interface;
        portENTER_CRITICAL(&lock);
        memcpy(params.remote_bda, report.targets[report.selected_target].address, 6);
        portEXIT_CRITICAL(&lock);
        stage("gatt.cancel_open", true);
        if (result("gatt.cancel_open", esp_ble_gattc_cancel_open(&params), true)) { wait(Cancel, "gatt.cancel_event", 2000, true); }
    }
    portENTER_CRITICAL(&lock); const bool close = link_open; portEXIT_CRITICAL(&lock);
    if (close) {
        stage("gatt.close", true);
        if (result("gatt.close", esp_ble_gattc_close(interface, connection), true)) { wait(Close, "gatt.close_event", 2000, true); }
    }
    if (interface != ESP_GATT_IF_NONE) {
        stage("gatt.app_unregister", true);
        if (result("gatt.app_unregister", esp_ble_gattc_app_unregister(interface), true)) { wait(Unregister, "gatt.unregister_event", 2000, true); }
    }
    bool clean = true;
    if (host_enabled) { stage("host.disable", true); clean = result("host.disable", esp_bluedroid_disable(), true); }
    if (clean && host_initialized) { stage("host.deinit", true); clean = result("host.deinit", esp_bluedroid_deinit(), true); }
    if (clean && controller_enabled) { stage("controller.disable", true); clean = result("controller.disable", esp_bt_controller_disable(), true); }
    if (clean && controller_initialized) { stage("controller.deinit", true); clean = result("controller.deinit", esp_bt_controller_deinit(), true); }
    measure("complete", "after", 0, false);
    portENTER_CRITICAL(&lock);
    if (pair_action()) { report.transport_ready=false; if (report.failure_code) { pairing->fail(); } }
    accept_events = false; scanning = false; opening = false; interface = ESP_GATT_IF_NONE;
    report.worker_active = false;
    report.teardown_complete = clean && !report.teardown_code;
    report.restart_required |= timed_out || !report.teardown_complete || report.failure_code != ESP_OK;
    report.restart_required |= !strcmp(report.action,"pair_rehearsal"); // Never promote a rehearsal boot to B/C.
    report.status = timed_out || !report.teardown_complete ? "restart_required" : (report.failure_code ? "failed" : "succeeded");
    report.stage = "complete";
    if (!strcmp(report.action, "scan")) { scan_finished = xTaskGetTickCount(); }
    portEXIT_CRITICAL(&lock);
}
bool TeslaBleBenchDiscovery::pair_owns(void *context,uint64_t)
{
    auto &s=*static_cast<TeslaBleBenchDiscovery *>(context); s.supervise();
    portENTER_CRITICAL(&s.lock); const bool ok=s.link_open && !s.timed_out && !s.security_requested && !s.report.failure_code;
    portEXIT_CRITICAL(&s.lock); return ok;
}
bool TeslaBleBenchDiscovery::pair_write(void *context,tesla_clean::Bytes bytes,uint64_t)
{
    auto &s=*static_cast<TeslaBleBenchDiscovery *>(context);
    portENTER_CRITICAL(&s.lock); s.events[Write]={}; portEXIT_CRITICAL(&s.lock);
    if (!s.stage("pair.write") || !s.result("pair.write",esp_ble_gattc_write_char(s.interface,s.connection,s.report.write_handle,bytes.size,
        const_cast<uint8_t *>(bytes.data),ESP_GATT_WRITE_TYPE_RSP,ESP_GATT_AUTH_REQ_NONE))) { return false; }
    return s.wait(Write,"pair.write_event");
}
bool TeslaBleBenchDiscovery::pair_send(void *context,tesla_clean::Bytes bytes)
{
    return tesla_clean::send_frame(bytes,20,1,pair_write,pair_owns,context);
}
void TeslaBleBenchDiscovery::pair_frame(void *context,const tesla_clean::Frame &frame)
{
    auto &s=*static_cast<TeslaBleBenchDiscovery *>(context);
    // Capture into checked heap-owned RX state, never crypto under the clean
    // FrameReceiver's detached 1024-byte local frame/callback.
    memcpy(s.pair_rx->completed.data.data(),frame.data.data(),frame.length);
    s.pair_rx->completed.length=frame.length;
}
void TeslaBleBenchDiscovery::drain_pair_rx()
{
    portENTER_CRITICAL(&lock); const size_t length=pair_rx->length;
    memcpy(pair_rx->detached,pair_rx->incoming,length); pair_rx->detached_ms=pair_rx->received_ms; pair_rx->length=0; portEXIT_CRITICAL(&lock);
    // Bounded byte feeding preserves arbitrary coalescing/fragmentation without
    // a fixed frame-count queue: at most one emit per call, then unwind before
    // cryptographic parsing. Timestamp/timeout semantics stay unchanged.
    for(size_t offset=0;offset<length;++offset) {
        if(!pair_rx->frames.receive({pair_rx->detached+offset,1},pair_now(),1,pair_frame,this)) { result("pair.rx_framing",ESP_FAIL); pairing->fail(); return; }
        if(pair_rx->completed.length) {
            pairing->receive_frame({pair_rx->completed.data.data(),pair_rx->completed.length},pair_rx->detached_ms);
            pair_rx->completed.length=0;
        }
    }
}
void TeslaBleBenchDiscovery::pair_transport()
{
    // Only the separately admitted pairing/verification actions reach this path.
    portENTER_CRITICAL(&lock); report.transport_ready=false; pair_rx->length=0; portEXIT_CRITICAL(&lock);
    pair_rx->frames.reset(1);
    // Copy only the immutable selected address/action under the report lock,
    // never retain one or two full report snapshots across crypto/GATT calls.
    uint8_t address[6];
    portENTER_CRITICAL(&lock);
    memcpy(address,report.targets[report.selected_target].address,6);
    const bool verify=!strcmp(report.action,"pair_verify");
    portEXIT_CRITICAL(&lock);
    if (!stage("pair.subscribe") || !result("pair.subscribe",esp_ble_gattc_register_for_notify(interface,address,report.notify_handle)) || !wait(Subscribe,"pair.subscribe_event")) { return; }
    uint8_t enable[2]={1,0};
    if (!stage("pair.cccd_enable") || !result("pair.cccd_enable",esp_ble_gattc_write_char_descr(interface,connection,report.cccd_handle,2,enable,ESP_GATT_WRITE_TYPE_RSP,ESP_GATT_AUTH_REQ_NONE)) || !wait(Descriptor,"pair.cccd_event")) { return; }

    if (!stage(verify?"pair.verify_request":"pair.present_key") || !result(verify?"pair.verify_request":"pair.present_key",
        (verify?pairing->verify(pair_now(),pair_send,this):pairing->send_pair(pair_now(),pair_send,this))?ESP_OK:ESP_FAIL)) { return; }
    const char *name=verify?"pair.verify_wait":"pair.card_wait";
    if (!stage(name)) { return; }
    while (pairing->active() && pair_owns(this,1)) { drain_pair_rx(); pairing->expire(pair_now()); vTaskDelay(pdMS_TO_TICKS(10)); }
    result(name,!strcmp(pairing->state(),"error")?ESP_FAIL:ESP_OK);
    if (!pair_owns(this,1)) { pairing->fail(); }
}
#endif
