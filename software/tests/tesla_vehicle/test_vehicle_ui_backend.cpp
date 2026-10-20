// Real module/worker/actions; only OS, storage and SDK boundaries are native fakes.
#include "host/module_platform.h"
#include "../../src/modules/tesla_vehicle/tesla_vehicle.cpp"
#include "host/bluetooth_driver.h"
#include "host/vin_scan_fixture.h"
#include <cstdio>
struct TeslaBleModuleTest {
    static void run(const std::string &mode) {
        TeslaVehicle m; m.pre_setup(); m.setup(); m.register_urls();
        assert(!m.tesla && peer_open_calls==0);
        api.commands.clear(); api.states.clear();
        m.bluetooth_boot_enabled=true; m.bluetooth_config.get("enable")->updateBool(true);
        assert(m.prepare_tesla());
        auto driver=std::make_unique<HostBluetoothDriver>(m.bluetooth,TeslaVehicle::gatt_callback);
        auto &cfg=m.tesla->config;
        cfg.get("enable")->updateBool(true); cfg.get("vin")->updateString("5YJ3E1EA7KF000001");
        cfg.get("ble_address")->updateString("01:02:03:04:05:06"); cfg.get("ev_index")->updateInt(ev.index);
        auto entries=m.tesla->profiles.get("entries");assert(entries->setCount(1));auto p=entries->get(size_t{0});
        p->get("enable")->updateBool(true);p->get("vin")->updateString("5YJ3E1EA7KF000001");
        p->get("ev_mac")->updateString("11:22:33:44:55:66");p->get("ble_address")->updateString("01:02:03:04:05:06");p->get("ev_index")->updateInt(ev.index);
        TeslaBleStorageAdapter store;assert(store.select_profile(ev.index,ev.mac));tesla_clean::IdentityStore key;
        assert(store.initialize_identity(key,{TeslaBleTransportAdapter::random,nullptr}));
        assert(tesla_clean::KeyStorage::committed(store.save_transaction("vehicle_vin",reinterpret_cast<const uint8_t *>("5YJ3E1EA7KF000001"),17)));
        auto binding=encode_vehicle_mac(ev.mac);assert(tesla_clean::KeyStorage::committed(store.save_transaction("warp_ev_mac",binding.data(),binding.size())));
        m.tesla->profile_command.get("ev_index")->updateInt(ev.index);
        m.register_urls();
        assert(api.states.size()==5 && api.commands.size()==7);
        for(auto &entry:api.commands) assert(entry.first=="bluetooth/config_update" || entry.first=="bluetooth/restart" || entry.first=="tesla_vehicle/start" || entry.first=="tesla_vehicle/pair" || entry.first=="tesla_vehicle/poll" || entry.first=="tesla_vehicle/forget_key" || entry.first=="tesla_vehicle/scan");
        assert(!m.tesla->runtime_owner.get() && peer_open_calls==0);
        p->get("discovery_revision")->updateUint(host_vin_scan(m.bluetooth,"5YJ3E1EA7KF000001",ev.index,ev.mac));
        String error; api.commands.at("tesla_vehicle/start")(0,error); assert(error.empty());
        m.loop(); auto *r=m.tesla->runtime_owner.get(); assert(r && !r->poll_requested.load() && !r->pair_requested.load());
        m.apply_profile();m.publish_profile();
        if(mode=="idle") {
            unsigned loops=0;
            on_delay=[&] { assert(peer_open_calls==0); if(++loops==4) r->stop_requested.store(true); };
            run_task(r->task_handle); on_delay={};
            assert(loops==4 && peer_open_calls==0);
            m.loop(); if(r->teardown_handle) run_task(r->teardown_handle); m.loop();
        } else {
            // The worker readiness marker is an OS-boundary scheduling seam.
            r->ble_ready.store(true); m.publish_bluetooth_state();
            if(mode=="admission") {
                api.commands.at("tesla_vehicle/poll")(0,error); assert(error.empty());
                error.clear(); api.commands.at("tesla_vehicle/poll")(0,error); assert(!error.empty());
                error.clear(); api.commands.at("tesla_vehicle/pair")(0,error); assert(!error.empty());
                error.clear(); api.commands.at("tesla_vehicle/forget_key")(0,error); assert(!error.empty());
                m.publish_bluetooth_state(); assert(m.tesla->state.get("action_busy")->asBool());
            } else if(mode=="pair-poll") {
                api.commands.at("tesla_vehicle/pair")(0,error); assert(error.empty());
                error.clear();api.commands.at("tesla_vehicle/poll")(0,error);assert(!error.empty() && !r->poll_requested.load());
                error.clear();api.commands.at("tesla_vehicle/forget_key")(0,error);assert(!error.empty());
            } else if(mode=="restart") {
                m.bluetooth_config.get("enable")->updateBool(false); m.publish_bluetooth_state();
                api.commands.at("tesla_vehicle/poll")(0,error); assert(!error.empty() && !r->poll_requested.load());
                error.clear();api.commands.at("tesla_vehicle/pair")(0,error); assert(!error.empty());
            } else if(mode=="soc") {
                r->session.publish({ev.generation,ev.index,ev.mac}); r->session.bind(ev.mac); r->session.connect();
                auto request=r->session.arm(r->epoch,{ev.generation,ev.index,ev.mac},ev.index);
                m.post_event(TeslaVehicle::EventType::Soc,nullptr,61.5f,request,true,true);
                m.process_module_events();assert(ev.applied==1);
                // A late value after unplugging is never accepted into shared EV state.
                request=r->session.arm(r->epoch,{ev.generation,ev.index,ev.mac},ev.index);
                ++ev.generation;ev.mac=0;
                m.post_event(TeslaVehicle::EventType::Soc,nullptr,99.0f,request,true,true);
                m.process_module_events();assert(ev.applied==1);
            } else if(mode=="disconnect-pending") {
                r->manual_poll_pending.store(true);r->in_flight_started_ms=millis();
                m.disconnect_internal(false);
                assert(!r->manual_poll_pending.load());
            }
            r->stop_requested.store(true);run_task(r->task_handle);m.loop();
            if(r->teardown_handle) run_task(r->teardown_handle);m.loop();
        }
        driver.reset();
        std::printf("VEHICLE_UI_BACKEND %s PASS; peer opens=%d EV publications=%d\n",mode.c_str(),peer_open_calls,ev.applied);
    }
};
int main(int argc,char **argv){assert(argc==2);TeslaBleModuleTest::run(argv[1]);}
