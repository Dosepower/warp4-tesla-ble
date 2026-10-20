// Executes production setup/loop, persistence API and explicit service actions.
#include "host/module_platform.h"
#include "../../src/modules/tesla_vehicle/tesla_vehicle.cpp"
#include "../../src/modules/tesla_vehicle/bluetooth_config_store.h"
#include <cstdio>
#define R13_NO_PERSISTENCE_MAIN
#include "test_r13_persistence.cpp"
struct TeslaBleModuleTest {
    static void run(const char *mode) {
        host_status=controller_status=0;
        TeslaVehicle m; m.pre_setup(); m.setup(); m.register_urls();
        if(std::string(mode)=="default") {
            assert(!m.tesla && task_launches==0 && ble_init_calls==0);
            assert(host_restore_calls==0 && !m.accepts_commands());
            assert(api.commands.size()==2 && !api.commands.count("tesla_vehicle/poll"));
            m.loop(); assert(!m.tesla && queues_live==0);
        }
#ifdef R13_API
        else if(std::string(mode)=="persist-fail") {
            assert(m.persist_bluetooth(false)); const auto keys=LittleFS.files;
            fault=std::getenv("R13_FAULT"); closes=renames=0;
            m.bluetooth_update.get("enable")->updateBool(true);
            String error; api.commands.at("bluetooth/config_update")(0,error);
            assert(!error.empty() && !m.bluetooth_config.get("enable")->asBool());
            assert(m.bluetooth_state.get("persistence_error")->asBool());
            error.clear(); api.commands.at("bluetooth/restart")(0,error);
            assert(!error.empty() && reboot_calls==0 && !m.tesla && LittleFS.files==keys);
        } else if(std::string(mode)=="save") {
            m.bluetooth_update.get("enable")->updateBool(true);
            String error; api.commands.at("bluetooth/config_update")(0,error);
            assert(error.empty() && m.bluetooth_config.get("enable")->asBool());
            assert(m.bluetooth_state.get("restart_required")->asBool());
            m.loop(); assert(!m.tesla && ble_init_calls==0 && reboot_calls==0);
            api.commands.at("bluetooth/restart")(0,error); assert(error.empty() && reboot_calls==1);
        } else if(std::string(mode)=="forget-disabled") {
            assert(m.bluetooth_boot_enabled && m.tesla && !m.tesla->runtime_owner.get());
            assert(!m.tesla->config.get("enable")->asBool());
            String error;api.commands.at("tesla_vehicle/forget_key")(0,error);
            auto *r=m.tesla->runtime_owner.get(); assert(error.empty() && r && r->storage_only);
            assert(!r->service_enabled && !r->pair_requested.load() && !r->poll_requested.load());
            run_task(r->task_handle);m.loop();run_task(r->teardown_handle);m.loop();
            assert(!m.tesla->runtime_owner.get() && queues_live==0 && ble_init_calls==0);
        } else if(std::string(mode)=="boot") {
            assert(m.tesla && !m.tesla->runtime_owner.get());
            assert(m.bluetooth_state.get("phase")->asString()=="starting");
            assert(!m.tesla->state.get("running")->asBool());
            host_gatt_success=true;
            on_delay=[&] {
                m.loop(); assert(m.bluetooth_state.get("phase")->asString()=="active");
                assert(!m.tesla->runtime_owner.get() && queues_live==0 && peer_open_calls==0);
                m.bluetooth.request_stop();
            };
            run_task(tasks.front().get());on_delay={};m.loop();
            assert(ble_init_calls==1 && m.bluetooth.stopped());
            m.bluetooth_update.get("enable")->updateBool(false);
            String error;api.commands.at("bluetooth/config_update")(0,error);
            api.commands.at("bluetooth/restart")(0,error);assert(error.empty() && reboot_calls==1);
        } else if(std::string(mode)=="unsupported") {
            assert(!m.bluetooth_state.get("supported")->asBool());
            assert(!m.bluetooth_state.get("unsupported_reason")->asString().empty());
            m.bluetooth_update.get("enable")->updateBool(true);
            String error;api.commands.at("bluetooth/config_update")(0,error);
            assert(!error.empty() && !m.bluetooth_config.get("enable")->asBool());
            assert(!m.bluetooth_state.get("restart_required")->asBool());
            assert(!m.tesla && ble_init_calls==0 && task_launches==0);
        }
#endif
    }
};
int main(int argc,char **argv) {
    assert(argc==2);TeslaBleModuleTest::run(argv[1]);puts("R13 production module slice passed");
}
