// Executes TeslaVehicle production setup/loop/worker/URL callbacks.
#include "host/module_platform.h"
#include "../../src/modules/tesla_vehicle/tesla_vehicle.cpp"
#include "../../src/modules/tesla_vehicle/bluetooth_config_store.h"
#include <cstdio>
#define R13_NO_PERSISTENCE_MAIN
#include "test_r13_persistence.cpp"
struct TeslaBleModuleTest {
    static void run(const char *mode) {
        TeslaVehicle m; m.pre_setup(); m.setup(); m.register_urls();
        if(std::string(mode)=="default") {
            assert(!m.runtime_owner.get()); assert(task_launches==0 && ble_init_calls==0);
            // Legacy service enable can never grant global Bluetooth permission.
            m.config.get("enable")->updateBool(true);
            m.config.get("vin")->updateString("5YJ3E1EA7KF000001");
            m.config.get("ble_address")->updateString("01:02:03:04:05:06");
            m.config.get("ev_index")->updateInt(2);
            m.loop(); assert(!m.runtime_owner.get());
            String error; api.commands.at("tesla_vehicle/poll")(0,error); assert(!error.empty());
        }
#ifdef R13_API
        else if(std::string(mode)=="persist-fail") {
            assert(m.bluetooth_store.save(false));
            const auto keys = LittleFS.files;
            fault = std::getenv("R13_FAULT"); closes=renames=0;
            m.bluetooth_update.get("enable")->updateBool(true);
            String error; api.commands.at("bluetooth/config_update")(0,error);
            assert(!error.empty() && !m.bluetooth_config.get("enable")->asBool());
            assert(m.bluetooth_state.get("persistence_error")->asBool());
            error.clear(); api.commands.at("bluetooth/restart")(0,error);
            assert(!error.empty() && reboot_calls==0);
            assert(!m.runtime_owner.get() && LittleFS.files==keys);
        } else if(std::string(mode)=="save") {
            assert(api.commands.count("bluetooth/config_update"));
            m.bluetooth_update.get("enable")->updateBool(true);
            String error; api.commands.at("bluetooth/config_update")(0,error);
            assert(error.empty()); assert(m.bluetooth_config.get("enable")->asBool());
            assert(m.bluetooth_state.get("restart_required")->asBool());
            m.loop(); assert(!m.runtime_owner.get() && ble_init_calls==0);
            assert(reboot_calls==0);
            error.clear(); api.commands.at("bluetooth/restart")(0,error);
            assert(error.empty() && reboot_calls==1);
        } else if(std::string(mode)=="forget-disabled") {
            assert(m.bluetooth_boot_enabled && m.runtime_owner.get());
            assert(!m.config.get("enable")->asBool());
            String error;api.commands.at("tesla_vehicle/forget_key")(0,error);
            assert(error.find("Tesla service is disabled")!=std::string::npos);
            assert(error.find("stopping")==std::string::npos);
            assert(!m.runtime_owner.get()->forget_requested.load());
            assert(!m.runtime_owner.get()->stop_requested.load());
        } else if(std::string(mode)=="boot") {
            assert(m.runtime_owner.get());
            assert(m.bluetooth_state.get("phase")->asString()=="starting");
            assert(!m.state.get("running")->asBool());
            on_delay=[&] {
                m.loop(); assert(m.bluetooth_state.get("phase")->asString()=="active");
                m.runtime_owner.get()->stop_requested.store(true);
            };
            run_task(m.runtime_owner.get()->task_handle); on_delay={};
            assert(ble_init_calls==1); // No VIN/profile needed for general permission.
            m.loop(); run_task(m.runtime_owner.get()->teardown_handle); m.loop();
            assert(m.runtime_owner.restart_required());
            m.bluetooth_update.get("enable")->updateBool(true);
            String error; api.commands.at("bluetooth/config_update")(0,error); m.loop();
            assert(error.empty() && m.bluetooth_state.get("restart_required")->asBool());
        } else if(std::string(mode)=="unsupported") {
            assert(!m.bluetooth_state.get("supported")->asBool());
            assert(!m.bluetooth_state.get("unsupported_reason")->asString().empty());
            m.bluetooth_update.get("enable")->updateBool(true);
            String error; api.commands.at("bluetooth/config_update")(0,error);
            assert(!error.empty()); assert(!m.bluetooth_config.get("enable")->asBool());
            assert(!m.bluetooth_state.get("restart_required")->asBool());
            assert(!m.runtime_owner.get() && ble_init_calls==0);
        }
#endif
    }
};
int main(int argc,char **argv) {
    assert(argc==2); TeslaBleModuleTest::run(argv[1]); puts("R13 module slice passed");
}
