// SPDX-License-Identifier: Apache-2.0
// Execute the real module/callback binding with diagnostics enabled. SDK heap
// capture/serial wrappers are independently exercised by test_iram_diagnostics.py.
#define TESLA_BLE_IRAM_DIAGNOSTICS 1
#include "../../../src/modules/tesla_vehicle/tesla_vehicle.cpp"
#include "../../../src/modules/tesla_vehicle/tesla_ble_gatt.cpp"
#include <vector>
#include <string>

struct Snapshot { std::string stage, phase; int rc; bool rc_valid; };
static std::vector<Snapshot> snapshots;
void tesla_ble_iram_snapshot(const char *stage, const char *phase, int rc, bool rc_valid)
{
    snapshots.push_back({stage, phase, rc, rc_valid});
}

struct TeslaBleModuleTest {
    static void run()
    {
        TeslaVehicle module;
        module.pre_setup();
        module.setup();
        assert(module.initialized && !module.runtime_owner.get());
        assert(!module.start_attempted && queues_live == 0 && queue_creates == 0);
        assert(task_launches == 0 && ble_init_calls == 0 && LittleFS.files.empty());
        assert(snapshots.size() == 1 && snapshots[0].stage == "module.setup" &&
               snapshots[0].phase == "boot" && !snapshots[0].rc_valid);
        module.loop();
        assert(!module.runtime_owner.get() && queues_live == 0 && task_launches == 0 && ble_init_calls == 0);
        assert(snapshots.size() == 1);

        // No protocol/worker execution needed to check asynchronous status
        // observation and preservation of the existing callback admission gates.
        module.config.get("enable")->updateBool(true);
        module.config.get("vin")->updateString("5YJ3E1EA7KF000001");
        module.config.get("ble_address")->updateString("01:02:03:04:05:06");
        assert(module.start_runtime());
        auto *r = module.runtime_owner.get();
        TeslaVehicle::gatt_owner.store(&module);
        esp_ble_gattc_cb_param_t param{};
        param.reg.app_id = 0x1234;
        TeslaVehicle::gatt_callback(ESP_GATTC_REG_EVT, 3, &param);
        assert(snapshots.size() == 1);
        param.reg.app_id = TESLA_GATT_APP;
        param.reg.status = 133;
        TeslaVehicle::gatt_callback(ESP_GATTC_REG_EVT, 3, &param);
        assert(snapshots.back().stage == "gatt.app_register" && snapshots.back().rc == 133 && snapshots.back().rc_valid);
        assert(r->gatt_if.load() == ESP_GATT_IF_NONE);
        param.reg.status = ESP_GATT_OK;
        TeslaVehicle::gatt_callback(ESP_GATTC_REG_EVT, 3, &param);
        assert(r->gatt_if.load() == 3 && snapshots.back().rc == ESP_GATT_OK);

        const auto previous = snapshots.size();
        TeslaVehicle::gatt_callback(ESP_GATTC_OPEN_EVT, 3, &param); // wrong address
        assert(snapshots.size() == previous);
        memcpy(param.open.remote_bda, r->remote_address, 6);
        param.open.status = 133;
        TeslaVehicle::gatt_callback(ESP_GATTC_OPEN_EVT, 3, &param);
        assert(!r->link_open.load() && snapshots.back().stage == "gatt.open" && snapshots.back().rc == 133);
        param.open.status = ESP_GATT_OK;
        param.open.conn_id = 7;
        TeslaVehicle::gatt_callback(ESP_GATTC_OPEN_EVT, 3, &param);
        assert(r->link_open.load() && r->gatt_connection.load() == 7 && snapshots.back().rc == ESP_GATT_OK);
        assert(snapshots.back().phase == "event");
        r->stop_requested.store(true);
        const auto stopped = snapshots.size();
        TeslaVehicle::gatt_callback(ESP_GATTC_OPEN_EVT, 3, &param);
        assert(snapshots.size() == stopped);
        TeslaVehicle::gatt_owner.store(nullptr);
    }
};

int main()
{
    TeslaBleModuleTest::run();
    assert(queues_live == 0);
}
