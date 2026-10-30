#include "host/module_platform.h"
#include "../../src/modules/tesla_vehicle/tesla_vehicle.cpp"
#include "host/bluetooth_driver.h"
#include <cstdio>
// Persisted trusted binding: one-time explicit search/adoption + authenticated
// pairing, then normal SoC refresh and the firmware scheduler use the saved
// VIN + BLE address/type + WARP EV MAC + authorized key without a fresh scan.
// Production entrypoints only: tesla_vehicle/guided API, validate_profiles,
// select/apply_profile, runtime_binding_matches, auto_poll(), ensure_connected.
// Synthetic fixtures only (README); stdout contains case labels only.
struct TeslaBleModuleTest {
 static constexpr uint64_t A=0x112233445500ULL,B=0x112233445501ULL;
 static constexpr const char *VIN="5YJ3E1EA7KF000001",*ADDR="01:02:03:04:05:06";
 static void entry(Config *p) {
  p->get("ev_index")->updateInt(0);p->get("ev_mac")->updateString("11:22:33:44:55:00");p->get("vin")->updateString(VIN);
  p->get("enable")->updateBool(true);p->get("ble_address")->updateString(ADDR);p->get("ble_address_type")->updateUint(1);
  p->get("discovery_revision")->updateUint(1);p->get("poll_interval")->updateUint(300);
 }
 // As after the user's explicit search, adoption and authenticated pairing.
 static void paired_material(bool authorized=true,bool transport=true,const char *transport_address=ADDR) {
  TeslaBleStorageAdapter store;assert(store.select_profile(0,A));tesla_clean::IdentityStore key;
  assert(store.initialize_identity(key,{TeslaBleTransportAdapter::random,nullptr}));
  const auto mac=encode_vehicle_mac(A);const uint8_t auth=1;uint8_t record[26]{};
  assert(tesla_clean::KeyStorage::committed(store.save_transaction("vehicle_vin",reinterpret_cast<const uint8_t *>(VIN),17)));
  assert(tesla_clean::KeyStorage::committed(store.save_transaction("warp_ev_mac",mac.data(),mac.size())));
  if(authorized)assert(tesla_clean::KeyStorage::committed(store.save_transaction("authorized",&auth,1)));
  assert(profile_transport_record(VIN,transport_address,1,record));
  if(transport)assert(tesla_clean::KeyStorage::committed(store.save_transaction("ble_binding",record,sizeof(record))));
 }
 static std::string guided(TeslaVehicle &m,const char *phase,unsigned id) {
  auto &c=m.tesla->guided_command;auto *q=c.get("profile");auto *p=m.find_profile(0);
  for(const char *k:{"ev_mac","vin","ble_address"})q->get(k)->updateString(p->get(k)->asString());
  q->get("ev_index")->updateInt(0);
  for(const char *k:{"ble_address_type","discovery_revision","poll_interval"})q->get(k)->updateUint(p->get(k)->asUint());
  q->get("enable")->updateBool(p->get("enable")->asBool());q->get("wake_once")->updateBool(p->get("wake_once")->asBool());
  char request[33];snprintf(request,sizeof(request),"%032x",id);
  c.get("phase")->updateString(phase);c.get("request")->updateString(request);
  c.get("generation")->updateString(std::to_string(ev.generation));c.get("revision")->updateUint(m.tesla->config_revision);
  String error;api.commands.at("tesla_vehicle/guided")(0,error);return error.c_str();
 }
 static void worker_apply(TeslaVehicle &m) {
  auto *r=m.tesla->runtime_owner.get();assert(r);
  m.apply_profile();m.refresh_live_session();m.update_active_vehicle();m.publish_profile();
  r->ble_ready.store(true);m.publish_bluetooth_state();m.tesla->guidance_refresh=true;m.publish_guidance();
 }
 static void stop(TeslaVehicle &m) {
  auto *r=m.tesla->runtime_owner.get();if(!r)return;
  r->stop_requested.store(true);run_task(r->task_handle);m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();
 }
 static void run(const std::string &mode) {
  TeslaVehicle m;m.pre_setup();m.setup();m.bluetooth_boot_enabled=true;m.bluetooth_config.get("enable")->updateBool(true);
  assert(m.prepare_tesla());m.register_urls();HostBluetoothDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);
  auto *profiles=m.tesla->profiles.get("entries");assert(profiles->setCount(1));entry(profiles->get(size_t{0}));
  ev.index=0;ev.mac=A;
  const bool unauthorized=mode=="unpaired-refresh",unbound=mode=="unbound-refresh";
  paired_material(!unauthorized,!unbound,mode=="stale-transport"?"04:02:03:04:05:06":ADDR);
  // Reboot restore: only persisted config + storage; no discovery result exists at all.
  // Even an aged clock (well past any TTL) needs no rediscovery.
  clock_ms.fetch_add(10*TeslaBleDiscovery::RESULT_TTL_MS+1);
  // Negative identity controls: configuration/identity/key differ from the saved binding.
  {auto *p=m.find_profile(0);
   if(mode=="changed-address")p->get("ble_address")->updateString("04:02:03:04:05:06"); // never auto-adopted
   if(mode=="changed-vin")p->get("vin")->updateString("5YJ3E1EA7KF000002");
   if(mode=="changed-plc")ev.profiles[0]=B; // WARP profile MAC changed: identity binding broken
   if(mode=="no-key")LittleFS.files.erase("/config/tesla_p00_112233445500_private_key");}
  const auto original=LittleFS.files;const auto scans0=scan_start_calls;
  m.publish_guidance();
  auto row=m.tesla->guidance.get("profiles")->get(size_t{0});
  const bool expect_persisted=mode=="refresh"||mode=="reuse"||mode=="scheduler"||mode=="scheduler-reuse"||mode=="connect-fail"||mode=="pair-unchanged"||mode=="changed-generation";
  assert(!row->get("binding_fresh")->asBool());
  if(row->get("binding_persisted")->asBool()!=expect_persisted){std::printf("RED: %s binding_persisted=%d\n",mode.c_str(),row->get("binding_persisted")->asBool());std::exit(1);}
  if(mode=="pair-unchanged") {
   // Pairing keeps the fresh exact-VIN discovery gate (and authorized needs none).
   assert(!guided(m,"prepare_pair",1).empty());String e;assert(!m.select_profile(0,false,e));
   assert(scan_start_calls==scans0&&!peer_open_calls&&LittleFS.files==original);std::printf("PERSISTED_BINDING %s PASS\n",mode.c_str());return;
  }
  if(mode=="scheduler"||mode=="scheduler-reuse"||mode=="connect-fail") {
   // Firmware scheduler (no browser/API): plug-in request after the settle delay.
   host_evse.set_charger_state(CHARGER_STATE_READY_TO_CHARGE);
   if(mode=="scheduler-reuse") {
    assert(guided(m,"prepare_poll",1).empty());worker_apply(m);
   }
   auto *before=m.tesla->runtime_owner.get();const uint64_t epoch=before?before->epoch:0;
   for(unsigned i=0;i<2000&&m.tesla->auto_poll.phase!=TeslaVehicle::Tesla::AutoPoll::Phase::Selecting;++i){clock_ms.fetch_add(20);m.loop();}
   if(m.tesla->auto_poll.phase!=TeslaVehicle::Tesla::AutoPoll::Phase::Selecting){
    std::printf("RED: %s scheduler did not start a cycle without scan (auto_poll=%s scans=%u)\n",mode.c_str(),m.tesla->state.get("auto_poll")->asString().c_str(),scan_start_calls-scans0);std::exit(1);}
   auto *r=m.tesla->runtime_owner.get();assert(r);
   if(scan_start_calls!=scans0){std::printf("RED: %s scheduler scanned\n",mode.c_str());std::exit(1);}
   if(mode=="scheduler-reuse") {
    // Existing correctly owned runtime: no reload, no reconnect.
    assert(r==before&&r->epoch==epoch&&!r->profile_pending.load());
   } else { assert(r->profile_pending.load()&&r->profile_request.persisted); worker_apply(m); }
   m.loop();
   assert(m.tesla->auto_poll.phase==TeslaVehicle::Tesla::AutoPoll::Phase::Polling&&r->auto_poll_requested.load());
   assert(m.runtime_binding_matches()&&r->transport_bound.load()&&r->authorized.load());
   if(mode=="connect-fail") {
    // Stored address unreachable: bounded failure, actionable state, binding untouched, no scan/adoption.
    const auto opens=peer_open_calls;peer_open_fail=true;std::memset(peer_open_address,0,6);
    // Run the real worker task: its own loop claims auto_poll_requested
    // (process_commands) and request_soc -> ensure_connected registers GATT and
    // opens the stored address in worker context, exactly as on the target.
    bool observed=false;
    on_delay=[&] {
     if(observed||r->auto_poll_requested.load()||peer_open_calls==opens||r->in_flight_started_ms!=0)return;
     for(int i=0;i<10&&m.tesla->auto_poll.phase!=TeslaVehicle::Tesla::AutoPoll::Phase::Idle;++i){m.loop();}
     if(m.tesla->auto_poll.phase!=TeslaVehicle::Tesla::AutoPoll::Phase::Idle)return; // attempt still in progress
     observed=true;r->stop_requested.store(true);
    };
    run_task(r->task_handle);on_delay={};
    std::printf("CONNECT_FAIL auto_poll=%s failure=%s opens=%u type=%u addr=%02x..%02x\n",m.tesla->state.get("auto_poll")->asString().c_str(),m.tesla->state.get("poll_failure")->asString().c_str(),peer_open_calls-opens,unsigned(peer_open_type),peer_open_address[0],peer_open_address[5]);std::fflush(stdout);
    assert(observed);
    assert(m.tesla->state.get("auto_poll")->asString()=="not_found"&&m.tesla->state.get("poll_failure")->asString()=="connect");
    // The open went to exactly the persisted address/type, never another one.
    const uint8_t stored[6]{1,2,3,4,5,6};
    assert(peer_open_calls>opens&&std::memcmp(peer_open_address,stored,6)==0&&peer_open_type==1);
    assert(LittleFS.files==original&&scan_start_calls==scans0);
    assert(m.find_profile(0)->get("ble_address")->asString()==ADDR&&m.find_profile(0)->get("enable")->asBool());
    assert(m.find_profile(0)->get("ble_address_type")->asUint()==1&&m.find_profile(0)->get("vin")->asString()==VIN);
    assert(!r->connected.load()&&!r->link_open.load());
    peer_open_fail=false;
    m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();
    std::printf("PERSISTED_BINDING %s PASS\n",mode.c_str());return;
   }
   assert(LittleFS.files==original&&scan_start_calls==scans0);
   stop(m);std::printf("PERSISTED_BINDING %s PASS\n",mode.c_str());return;
  }
  // Explicit manual refresh through the guided API (what guided_vehicle.tsx sends).
  if(mode=="changed-address"||mode=="changed-vin"||mode=="changed-plc"||mode=="changed-generation"||mode=="no-key"||
     mode=="unpaired-refresh"||mode=="unbound-refresh"||mode=="stale-transport") {
   const auto files=LittleFS.files;std::string e;
   if(mode=="changed-generation"){auto &c=m.tesla->guided_command;(void)c;const auto g=ev.generation;e=guided(m,"prepare_poll",1);
    assert(e.empty());++ev.generation;m.refresh_live_session();
    // Prepared under the old SLAC generation: commit rejected, nothing sent.
    worker_apply(m);ev.generation=g+1;e=guided(m,"commit",1);assert(!e.empty());
    assert(!m.tesla->runtime_owner.get()->poll_requested.load());stop(m);std::printf("PERSISTED_BINDING %s PASS\n",mode.c_str());return;}
   e=guided(m,"prepare_poll",1);
   if(e.empty()){std::printf("RED: %s persisted refresh accepted\n",mode.c_str());std::exit(1);}
   String se;assert(!m.select_profile(0,false,se,0,true));
   assert(LittleFS.files==files&&scan_start_calls==scans0&&!peer_open_calls);
   stop(m);std::printf("PERSISTED_BINDING %s PASS\n",mode.c_str());return;
  }
  // refresh / reuse
  std::string e=guided(m,"prepare_poll",1);
  if(!e.empty()){std::printf("RED: %s prepare_poll refused: %s\n",mode.c_str(),e.c_str());std::exit(1);}
  auto *r=m.tesla->runtime_owner.get();assert(r&&r->profile_pending.load()&&r->profile_request.persisted);
  assert(scan_start_calls==scans0&&!peer_open_calls);
  worker_apply(m);
  assert(r->authorized.load()&&r->transport_bound.load()&&m.runtime_binding_matches());
  assert(m.tesla->guidance.get("ready")->asBool());
  assert(guided(m,"commit",1).empty()&&r->poll_requested.load());
  assert(LittleFS.files==original&&scan_start_calls==scans0); // nothing written, no scan
  if(mode=="reuse") {
   // Existing valid runtime/link: a further refresh reuses it, no reload/reconnect.
   r->poll_requested.store(false);r->manual_poll_pending.store(false);
   r->connected.store(true);r->link_open.store(true);const uint64_t epoch=r->epoch;const auto opens=peer_open_calls;
   assert(guided(m,"prepare_poll",2).empty());
   assert(m.tesla->runtime_owner.get()==r&&r->epoch==epoch&&!r->profile_pending.load()&&r->profile_ready.load());
   m.publish_guidance();assert(m.tesla->guidance.get("ready")->asBool());
   assert(m.ensure_connected()&&peer_open_calls==opens); // reused, not reopened
   assert(guided(m,"commit",2).empty()&&r->poll_requested.load());
   r->connected.store(false);r->link_open.store(false);
   assert(LittleFS.files==original&&scan_start_calls==scans0);
  }
  stop(m);std::printf("PERSISTED_BINDING %s PASS\n",mode.c_str());
 }
};
int main(int argc,char **argv){assert(argc==2);TeslaBleModuleTest::run(argv[1]);assert_charging_untouched();assert(ev.applied==0);return 0;}
