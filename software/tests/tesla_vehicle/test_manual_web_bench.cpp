// Real module/lifecycle; native config, filesystem and SDK boundaries only.
#include "host/module_platform.h"
#include "../../src/modules/tesla_vehicle/tesla_vehicle.cpp"
#define R13_NO_PERSISTENCE_MAIN
#include "test_r13_persistence.cpp"
void tesla_ble_iram_snapshot(const char *, const char *, int, bool) {}
static bool resource_ok = true;
bool r13_psram_ok() { return resource_ok; }
void r13_psram_worker_begin() {}
void r13_psram_worker_end() {}
struct TeslaBleModuleTest {
    static void routes(TeslaVehicle &v) {
        api.states.clear(); api.commands.clear(); v.register_urls();
        assert(api.states.size() == 2 && api.states.count("bluetooth/config") && api.states.count("bluetooth/state"));
        assert(api.commands.size() == 2 && api.commands.count("bluetooth/config_update") && api.commands.count("bluetooth/restart"));
        assert(!v.tesla && host_restore_calls == 0);
    }
    static void save(TeslaVehicle &v, bool enabled, String &error) {
        v.bluetooth_update.get("enable")->updateBool(enabled);
        error.clear(); api.commands.at("bluetooth/config_update")(0, error);
    }
    static void run(const std::string &mode) {
        host_status = controller_status = 0;
        host_restore_service_enabled = true; // Persisted Tesla consent must be ignored.
        TeslaVehicle first; first.pre_setup(); first.setup(); routes(first);
        assert(!first.bluetooth_boot_enabled && !task_launches && !ble_init_calls);
        Serial.input = "r13:on\nr13:restart\nr13:psram:on\n";
        first.loop();
        assert(!first.bluetooth_config.get("enable")->asBool() && !task_launches && !reboot_calls);
        String error;
        if (mode == "persist-fail") {
            ::fault = "fsync"; save(first, true, error);
            assert(!error.empty() && !first.bluetooth_config.get("enable")->asBool());
            assert(first.bluetooth_state.get("persistence_error")->asBool());
            error.clear(); api.commands.at("bluetooth/restart")(0, error);
            assert(!error.empty() && !reboot_calls && !task_launches);
            return;
        }
        save(first, true, error);
        assert(error.empty() && first.bluetooth_config.get("enable")->asBool());
        assert(first.bluetooth_state.get("restart_required")->asBool() && !task_launches && !reboot_calls);
        error.clear(); api.commands.at("bluetooth/restart")(0, error);
        assert(error.empty() && reboot_calls == 1);
        TeslaVehicle second; second.pre_setup(); second.setup(); routes(second);
        assert(second.bluetooth_boot_enabled && task_launches == 0);
        second.loop(); // Starts only after normal complete boot, never during module setup.
        assert(second.bluetooth_boot_enabled && task_launches == 1 && !second.tesla);
        host_gatt_success = true;
        const bool fault = mode != "ok";
        if (mode == "init-fail") sdk_fail_stage = "host-enable";
        if (mode == "teardown-fail") sdk_fail_stage = "host-deinit";
        if (mode == "resource-fail") resource_ok = false;
        if (mode == "ok" || mode == "teardown-fail") {
            on_delay = [&] {
                if (!second.master_bench.active()) return;
                save(second, false, error);
                assert(error.empty() && second.master_bench.active());
                error.clear(); api.commands.at("bluetooth/restart")(0, error);
                assert(error.empty() && reboot_calls == 1);
                second.loop(); // teardown dispatched only after API handler returns
            };
        }
        run_task(tasks.back().get()); on_delay = {};
        second.loop(); routes(second);
        if (fault) {
            assert(second.master_bench.failed() && !second.master_bench.active());
            assert(reboot_calls == 1);
            error.clear(); api.commands.at("bluetooth/restart")(0, error);
            assert(!error.empty() && reboot_calls == 1);
        } else {
            assert(second.master_bench.stopped() && !second.master_bench.failed() && reboot_calls == 2);
            assert(host_status == 0 && controller_status == 0);
            TeslaVehicle third; third.pre_setup(); third.setup(); routes(third);
            assert(!third.bluetooth_boot_enabled && !third.bluetooth_config.get("enable")->asBool());
            assert(task_launches == 1 && ble_init_calls == 1);
        }
        assert(!second.tesla && ev.applied == 0);
    }
};
int main(int argc, char **argv) {
    assert(argc == 2);
    unlink(BLUETOOTH_CONFIG_PATH); unlink(BLUETOOTH_CONFIG_PATH ".bak"); unlink(BLUETOOTH_CONFIG_PATH ".tmp");
    TeslaBleModuleTest::run(argv[1]);
    std::printf("MANUAL_WEB_API_PASS %s exact_states=2 exact_commands=2 no_tesla=true no_serial_control=true\n", argv[1]);
}
