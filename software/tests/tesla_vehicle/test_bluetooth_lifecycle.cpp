#include "host/module_platform.h"
#include "../../src/modules/tesla_vehicle/bluetooth_lifecycle.cpp"
#include <cstdio>
static unsigned forwarded = 0;
static void peer_handler(esp_gattc_cb_event_t, esp_gatt_if_t, esp_ble_gattc_cb_param_t *) { ++forwarded; }
int main(int argc, char **argv) {
    assert(argc == 2); const std::string mode = argv[1];
    host_status = controller_status = 0; host_gatt_success = true;
    if (mode == "destroy-starting" || mode == "destroy-failed") {
        auto pending = std::make_unique<BluetoothLifecycle>(); assert(pending->start(peer_handler));
        auto *task = tasks.back().get();
        if (mode == "destroy-failed") {
            on_delay = [&] { cleanup_result = false; pending->request_stop(); };
            run_task(task); on_delay = {}; assert(pending->failed());
            pending.reset();
            esp_ble_gattc_cb_param_t p; host_gatt_handler(ESP_GATTC_NOTIFY_EVT, 1, &p);
            assert(forwarded == 0 && host_status == 2); // Failed host still has a callback, never a dangling facade.
        } else {
            pending.reset();
            run_task(task); // A disappearing facade cannot release an unacknowledged worker context.
            assert(host_status == 0 && controller_status == 0 && peer_open_calls == 0);
        }
        std::printf("direct-IDF lifecycle %s passed\n", mode.c_str()); return 0;
    }
    BluetoothLifecycle owner;
    assert(!owner.active() && !owner.failed() && !owner.gatt_ready());
    assert(task_launches == 0 && lifecycle_calls.empty());
    if (mode == "task-failure") {
        fail_task = "bluetooth"; assert(!owner.start());
        assert(owner.failed() && !owner.start() && lifecycle_calls.empty());
    } else {
        if (mode == "foreign") { host_status = controller_status = 2; }
        if (mode.rfind("init-", 0) == 0) { sdk_fail_stage = mode.substr(5); }
        if (mode.rfind("cleanup-", 0) == 0) { sdk_fail_stage = mode.substr(8); }
        if (mode == "async" || mode == "reg-negative" || mode == "reg-timeout") { defer_gatt_registration = true; }
        assert(owner.start(peer_handler));
        auto *task = tasks.back().get(); assert(task->name == "bluetooth" && task->stack_bytes == 4096);
        assert(lifecycle_calls.empty() && queues_live == 0 && !owner.active());
        assert(!owner.start()); // no second worker/owner
        if (mode == "timeout") {
            clock_ms.fetch_add(15001); owner.supervise();
            assert(owner.failed() && !task->deleted && !owner.stopped() && !owner.start());
            run_task(task); owner.supervise(); assert(task->deleted && owner.failed());
        } else {
            bool registration_delivered = false;
            on_delay = [&] {
                if (!owner.active() && (mode == "async" || mode == "reg-negative")) {
                    assert(!owner.gatt_ready());
                    esp_ble_gattc_cb_param_t p; p.reg.app_id = 0x1234;
                    host_gatt_handler(ESP_GATTC_REG_EVT, 1, &p);
                    assert(!owner.active() && !owner.gatt_ready());
                    p.reg.app_id = 0x5231; p.reg.status = mode == "reg-negative" ? -1 : ESP_GATT_OK;
                    host_gatt_handler(ESP_GATTC_REG_EVT, 1, &p); registration_delivered = true;
                } else if (owner.active()) {
                    assert(owner.gatt_ready() && host_status == 2 && controller_status == 2);
                    if (mode == "async") { assert(registration_delivered); }
                    esp_ble_gattc_cb_param_t p;
                    host_gatt_handler(ESP_GATTC_NOTIFY_EVT, 1, &p); assert(forwarded >= 1);
                    owner.request_stop();
                    const auto before = forwarded; host_gatt_handler(ESP_GATTC_NOTIFY_EVT, 1, &p);
                    assert(forwarded == before); // closed admission immediately, not after teardown
                }
            };
            if (mode == "cancel-start") { owner.request_stop(); }
            run_task(task); on_delay = {};
            const bool error = mode == "foreign" || mode.rfind("init-", 0) == 0 || mode.rfind("cleanup-", 0) == 0 || mode == "reg-negative" || mode == "reg-timeout";
            assert(owner.failed() == error && owner.stopped() == !error);
            owner.supervise(); assert(task->deleted && !owner.active() && !owner.start());
            if (mode == "foreign") { assert(lifecycle_calls.empty() && cleanup_calls.empty()); }
            else if (!error) { assert(host_status == 0 && controller_status == 0); }
            if (mode == "ok" || mode == "async") {
                assert(lifecycle_calls == std::vector<std::string>({"controller-init", "controller-enable", "host-init", "host-enable", "callback-register", "app-register"}));
                assert(cleanup_calls == std::vector<std::string>({"host-disable", "host-deinit", "controller-disable", "controller-deinit"}));
            }
            if (mode.rfind("cleanup-", 0) == 0) {
                BluetoothLifecycle rival; assert(!rival.start()); // failed teardown retains sole ownership
                assert(rival.failed());
            }
        }
    }
    assert(queues_live == 0 && peer_open_calls == 0 && ble_deinit_calls == 0);
    std::printf("direct-IDF lifecycle %s passed\n", mode.c_str());
}
