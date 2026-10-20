#include "host/module_platform.h"
#include "../../src/modules/tesla_vehicle/tesla_vehicle.cpp"
#include <cstdio>
struct TeslaBleModuleTest {
 static void run(const std::string &mode) {
  TeslaVehicle m;m.pre_setup();m.setup();m.register_urls();
  assert(!api.commands.count("tesla_vehicle/scan") && scan_start_calls==0);
  m.bluetooth_boot_enabled=true;m.bluetooth_config.get("enable")->updateBool(true);assert(m.prepare_tesla());m.register_urls();
  assert(api.commands.count("tesla_vehicle/scan") && api.states.count("tesla_vehicle/discovery"));
  m.tesla->scan_command.get("vin")->updateString("5YJ3E1EA7KF000001");
  m.tesla->scan_command.get("ev_index")->updateInt(0);m.tesla->scan_command.get("ev_mac")->updateString("11:22:33:44:55:00");
  String error;api.commands.at("tesla_vehicle/scan")(0,error);assert(!error.empty() && scan_start_calls==0);
  host_status=controller_status=0;host_gatt_success=true;assert(m.bluetooth.start(TeslaVehicle::gatt_callback));auto *worker=tasks.back().get();
  bool admitted=false,finished=false;unsigned callbacks=0;
  on_delay=[&] {
   if(!m.bluetooth.active())return;
   m.publish_bluetooth_state();m.publish_discovery();
   if(!admitted) {
    assert(scan_start_calls==0 && !m.tesla->runtime_owner.get());
    // Persisted Bluetooth change, action busy and restart pending reject scans.
    m.bluetooth_config.get("enable")->updateBool(false);error.clear();api.commands.at("tesla_vehicle/scan")(0,error);assert(!error.empty());
    m.bluetooth_config.get("enable")->updateBool(true);
    m.service_start_requested=true;error.clear();api.commands.at("tesla_vehicle/scan")(0,error);assert(!error.empty());m.service_start_requested=false;
    m.bluetooth_restart_pending=true;error.clear();api.commands.at("tesla_vehicle/scan")(0,error);assert(!error.empty());m.bluetooth_restart_pending=false;
    error.clear();api.commands.at("tesla_vehicle/scan")(0,error);assert(error.empty());admitted=true;
    error.clear();api.commands.at("tesla_vehicle/scan")(0,error);assert(!error.empty());
    m.tesla->config.get("enable")->updateBool(true);
    for(const char *name:{"tesla_vehicle/start","tesla_vehicle/pair","tesla_vehicle/poll","tesla_vehicle/forget_key","bluetooth/restart"}) {
     error.clear();api.commands.at(name)(0,error);assert(!error.empty());
    }
    assert(peer_open_calls==0 && !m.tesla->runtime_owner.get());
   }
   if(scan_start_calls && callbacks++==0) {
    assert(scan_start_calls==1 && scan_seconds==10);
    if(mode=="cancel")m.bluetooth.request_stop();
    else if(mode=="success") {
     host_emit_target("S0123456789abcdefC");host_emit_target("headphones");host_emit_done();
    } else if(mode=="empty")host_emit_done();
   }
   if(admitted && !m.bluetooth.discovery_busy()) {
    TeslaBleDiscovery::Snapshot s;assert(m.bluetooth.discovery_snapshot(s));
    assert(s.status==TeslaBleDiscovery::Status::Complete);
    assert(s.count==(mode=="success"?1u:0u));
    assert(peer_open_calls==0 && !m.tesla->runtime_owner.get());finished=true;m.bluetooth.request_stop();
   }
  };
  if(mode=="params-error")scan_fail="params";
  if(mode=="start-error")scan_fail="start";
  if(mode=="stop-error")scan_fail="stop";
  if(mode=="stop-timeout")scan_defer_stop=true;
  run_task(worker);on_delay={};m.bluetooth.supervise();m.publish_discovery();
  assert(admitted && scan_start_calls<2 && peer_open_calls==0 && queues_live==0);
  assert(host_status==0 && controller_status==0);
  const bool success=mode=="success" || mode=="empty";
  assert(finished==success);assert(m.bluetooth.failed()==!success);
  assert(!m.bluetooth.discovery_busy());
  if(!success) {TeslaBleDiscovery::Snapshot s;assert(m.bluetooth.discovery_snapshot(s));assert(s.status==TeslaBleDiscovery::Status::Error && !s.count);assert(!m.bluetooth.request_discovery());}
  assert(!m.tesla->runtime_owner.get() && LittleFS.files.empty());
  std::printf("MANUAL_DISCOVERY_API %s PASS starts=%u stops=%u no peer/key/storage/runtime side effects\n",mode.c_str(),scan_start_calls,scan_stop_calls);
 }
};
int main(int argc,char **argv){assert(argc==2);TeslaBleModuleTest::run(argv[1]);}
