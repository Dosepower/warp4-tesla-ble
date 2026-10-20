#include "host/module_platform.h"
#include "../../src/modules/tesla_vehicle/tesla_vehicle.cpp"
#include "host/bluetooth_driver.h"
#include <cstdio>
// Synthetic fixtures only. No real vehicle data; stdout contains case labels only.
struct TeslaBleModuleTest {
 static constexpr uint64_t A=0x112233445500ULL,B=0x112233445501ULL;
 template<class F> static void wait(F f) {
  auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  bool done=false;
  while(!(done=f()) && std::chrono::steady_clock::now()<end)std::this_thread::yield();
  assert(done);
 }
 static void emit(const char *name,uint8_t first,uint8_t type) {
  esp_ble_gap_cb_param_t v;v.scan_rst.bda[0]=first;v.scan_rst.ble_addr_type=type;
  v.scan_rst.adv_data_len=20;v.scan_rst.ble_adv[0]=19;v.scan_rst.ble_adv[1]=9;memcpy(v.scan_rst.ble_adv+2,name,18);
  host_gap_handler(ESP_GAP_BLE_SCAN_RESULT_EVT,&v);
 }
 static void entry(Config *p,int index=0) {
  p->get("ev_index")->updateInt(index);p->get("ev_mac")->updateString(index?"11:22:33:44:55:01":"11:22:33:44:55:00");
  p->get("vin")->updateString(index?"5YJ3E1EA7KF000002":"5YJ3E1EA7KF000001");p->get("enable")->updateBool(true);
  p->get("ble_address")->updateString(index?"02:02:03:04:05:06":"01:02:03:04:05:06");p->get("ble_address_type")->updateUint(1);
 }
 static uint32_t scan(TeslaVehicle &m,Config *p,bool matching=true,bool duplicate=false,bool conflict=false,bool truncated=false) {
  for(auto key:{"vin","ev_mac"})m.tesla->scan_command.get(key)->updateString(p->get(key)->asString());
  m.tesla->scan_command.get("ev_index")->updateInt(p->get("ev_index")->asInt());
  String error;const auto starts=scan_start_calls;api.commands.at("tesla_vehicle/scan")(0,error);assert(error.empty());
  wait([&]{return scan_start_calls>starts;});
  TeslaBleDiscovery::Snapshot s;
  char name[19]{};assert(tesla_ble_vehicle_name(p->get("vin")->asString().c_str(),name));
  // Emit through the SDK boundary until the real worker has entered Scanning.
  wait([&]{emit("S0000000000000000C",9,0);return m.bluetooth.discovery_snapshot(s)&&s.count;});
  unsigned first=0;assert(sscanf(p->get("ble_address")->asString().c_str(),"%2x:",&first)==1);
  if(matching)emit(name,first,1);
  if(duplicate)emit(name,3,1);
  if(conflict)emit(name,first,0);
  if(truncated)for(unsigned i=10;i<20;++i)emit("S1111111111111111C",i,0);
  wait([&]{host_emit_done();return m.bluetooth.discovery_snapshot(s)&&s.status==TeslaBleDiscovery::Status::Complete;});
  // The host clock is injected/frozen while the worker changes phases.
  // Snapshot publication must not silently lose a completed scan on contention.
  const auto completed_at=clock_ms.load();
  m.publish_discovery();p->get("discovery_revision")->updateUint(s.revision);
  assert(clock_ms.load()==completed_at);
  assert(m.tesla->discovery_state.get("revision")->asUint()==s.revision);
  return s.revision;
 }
 static void vectors() {
  const char *v[]={"5YJ3E1EA7KF000001","5YJ3E1EA7KF000002","00000000000000000","ABCDEFGHJKLMNPRST"};
  const char *n[]={"S3acc31774a738ea0C","S79b59e14f8725d47C","Sc1f6ea8523102b1bC","S7f5b2b678cc58d97C"};
  for(unsigned i=0;i<4;++i){char out[19]{};assert(tesla_ble_vehicle_name(v[i],out)&&!strcmp(out,n[i]));}
  for(auto invalid:{"","5YJ3E1EA7KF00000","5YJ3E1EA7KF0000010","5yj3E1EA7KF000001","5YJ3E1EA7KF00000I","5YJ3E1EA7KF00000O","5YJ3E1EA7KF00000Q","5YJ3E1EA7KF00000 ","5YJ3E1EA7KF00000-"}) {
   char out[19];memset(out,'x',sizeof(out));assert(!tesla_ble_vehicle_name(invalid,out));for(char c:out)assert(!c);
  }
  char out[19]{};assert(!tesla_ble_vehicle_name(nullptr,out));
 }
 static void run(const std::string &mode) {
  vectors();TeslaVehicle m;m.pre_setup();m.setup();m.bluetooth_boot_enabled=true;m.bluetooth_config.get("enable")->updateBool(true);
  assert(m.prepare_tesla());m.register_urls();HostBluetoothDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);
  auto update=Config::Object({{"entries",Config::Array({},&m.tesla->profile,0,16)}});
  auto e=update.get("entries");assert(e->setCount(1));auto p=e->get(size_t{0});entry(p);
  ev.index=0;ev.mac=A;String error;
  assert(!m.validate_profiles(update,ConfigSource::API).empty() && "Manual address must not bypass first VIN scan");
  assert(LittleFS.files.empty() && peer_open_calls==0 && scan_start_calls==0);
  if(mode=="authorized-expired" || mode=="authorized" || mode=="address-change" || mode=="legacy-authorized") {
   TeslaBleStorageAdapter store;assert(store.select_profile(0,A));tesla_clean::IdentityStore key;
   assert(store.initialize_identity(key,{TeslaBleTransportAdapter::random,nullptr}));
   const auto mac=encode_vehicle_mac(A);const uint8_t auth=1;uint8_t record[26]{};
   assert(tesla_clean::KeyStorage::committed(store.save_transaction("vehicle_vin",reinterpret_cast<const uint8_t *>("5YJ3E1EA7KF000001"),17)));
   assert(tesla_clean::KeyStorage::committed(store.save_transaction("warp_ev_mac",mac.data(),mac.size())));
   assert(tesla_clean::KeyStorage::committed(store.save_transaction("authorized",&auth,1)));
   assert(profile_transport_record("5YJ3E1EA7KF000001","01:02:03:04:05:06",1,record));
   if(mode!="legacy-authorized")assert(tesla_clean::KeyStorage::committed(store.save_transaction("ble_binding",record,sizeof(record))));
   const auto original=LittleFS.files;m.tesla->profiles=update;
   if(mode=="authorized-expired") {
    scan(m,p);clock_ms.fetch_add(TeslaBleDiscovery::RESULT_TTL_MS);
    assert(!m.select_profile(0,false,error) && "Stored authorization must not bypass fresh discovery TTL");
    assert(LittleFS.files==original && !peer_open_calls);return;
   }
   if(mode=="legacy-authorized") {
    assert(!m.validate_profiles(update,ConfigSource::API).empty());assert(!m.select_profile(0,false,error));assert(LittleFS.files==original);
    scan(m,p);error.clear();
   } else {
    clock_ms.fetch_add(TeslaBleDiscovery::RESULT_TTL_MS+1);
    assert(m.validate_profiles(update,ConfigSource::API).empty());assert(!scan_start_calls);
    scan(m,p);error.clear();
   }
   if(mode=="address-change") {
    p->get("ble_address")->updateString("04:02:03:04:05:06");
    assert(!m.validate_profiles(update,ConfigSource::API).empty());assert(!m.select_profile(0,false,error));assert(LittleFS.files==original);
    scan(m,p);error.clear();assert(m.validate_profiles(update,ConfigSource::API).empty());
   }
   assert(m.select_profile(0,false,error));auto *r=m.tesla->runtime_owner.get();m.apply_profile();m.refresh_live_session();m.publish_profile();
   assert(r->authorized.load()&&m.runtime_binding_matches());clock_ms.fetch_add(TeslaBleDiscovery::RESULT_TTL_MS+1);assert(!m.runtime_binding_matches());
   for(const auto &item:original)if(item.first.find("ble_binding")==std::string::npos)assert(LittleFS.files.at(item.first)==item.second);
   assert(!peer_open_calls);if(mode=="authorized")assert(LittleFS.files==original && scan_start_calls==1);
   assert(profile_transport_matches(*r->storage,"5YJ3E1EA7KF000001",p->get("ble_address")->asString().c_str(),1));
   r->stop_requested.store(true);run_task(r->task_handle);m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();return;
  }
  if(mode=="manual") {
   m.tesla->profiles=update;assert(!m.select_profile(0,false,error));assert(!m.tesla->runtime_owner.get());return;
  }
  // Invalid VIN never starts a search.
  m.tesla->scan_command.get("vin")->updateString("INVALID");error.clear();api.commands.at("tesla_vehicle/scan")(0,error);assert(!error.empty()&&!scan_start_calls);
  const auto rev=scan(m,p,mode!="no-match",mode=="ambiguous",mode=="type-conflict",mode=="truncated");
  if(mode=="no-match"||mode=="ambiguous"||mode=="type-conflict"||mode=="truncated") {
   assert(!m.validate_profiles(update,ConfigSource::API).empty());assert(!peer_open_calls&&LittleFS.files.empty());return;
  }
  assert(m.validate_profiles(update,ConfigSource::API).empty());
  assert(m.tesla->discovery_state.get("targets")->count()==2);
  if(mode=="scan-generation") {
   ++ev.generation;
   m.tesla->profiles=update;
   assert(!m.select_profile(0,false,error) && "A new SLAC generation must invalidate old discovery even for the same EV");
   assert(!peer_open_calls && LittleFS.files.empty());return;
  }
  assert(!m.tesla->discovery_state.get("targets")->get(size_t{0})->get("matches_vin")->asBool());
  assert(m.tesla->discovery_state.get("targets")->get(size_t{1})->get("matches_vin")->asBool());
  p->get("vin")->updateString("5YJ3E1EA7KF000002");assert(!m.validate_profiles(update,ConfigSource::API).empty());entry(p);
  p->get("ble_address")->updateString("09:02:03:04:05:06");assert(!m.validate_profiles(update,ConfigSource::API).empty());entry(p);
  p->get("ble_address_type")->updateUint(0);assert(!m.validate_profiles(update,ConfigSource::API).empty());p->get("ble_address_type")->updateUint(1);
  p->get("discovery_revision")->updateUint(rev+1);assert(!m.validate_profiles(update,ConfigSource::API).empty());p->get("discovery_revision")->updateUint(rev);
  auto scopeMismatch=update;entry(scopeMismatch.get("entries")->get(size_t{0}),1);assert(!m.validate_profiles(scopeMismatch,ConfigSource::API).empty());entry(p);
  if(mode=="ttl") {
   clock_ms.fetch_add(TeslaBleDiscovery::RESULT_TTL_MS-1);
   assert(m.validate_profiles(update,ConfigSource::API).empty());
   clock_ms.fetch_add(1);
   assert(!m.validate_profiles(update,ConfigSource::API).empty());return;
  }
  if(mode=="rescan") {
   const auto newer=scan(m,p);assert(newer!=rev);p->get("discovery_revision")->updateUint(rev);assert(!m.validate_profiles(update,ConfigSource::API).empty());
   p->get("discovery_revision")->updateUint(newer);assert(m.validate_profiles(update,ConfigSource::API).empty());return;
  }
  if(mode=="match") {assert(!peer_open_calls && LittleFS.files.empty());return;}
  m.tesla->profiles=update;
  error.clear();const bool selected=m.select_profile(0,false,error);if(!selected)std::fprintf(stderr,"selection refused: %s\n",error.c_str());assert(selected);
  auto *r=m.tesla->runtime_owner.get();m.apply_profile();m.refresh_live_session();m.update_active_vehicle();m.publish_profile();
  r->ble_ready.store(true);m.publish_bluetooth_state();m.tesla->profile_command.get("ev_index")->updateInt(0);
  assert(!peer_open_calls && !r->authorized.load());
  const auto before=LittleFS.files;
  if(mode=="profile") {++ev.generation;ev.index=1;ev.mac=B;error.clear();api.commands.at("tesla_vehicle/pair")(0,error);assert(!error.empty());}
  else {
   error.clear();api.commands.at("tesla_vehicle/pair")(0,error);assert(error.empty());
   error.clear();api.commands.at("tesla_vehicle/scan")(0,error);assert(!error.empty()); // No TOCTOU rescan via API.
   if(mode=="queued-ttl")clock_ms.fetch_add(TeslaBleDiscovery::RESULT_TTL_MS);
   else if(mode=="queued-profile"){++ev.generation;ev.index=1;ev.mac=B;}
   else if(mode=="queued-revision") {
    TeslaBleDiscovery::Scope scope;scope.index=0;scope.mac=A;assert(tesla_ble_vehicle_name("5YJ3E1EA7KF000001",scope.name));
    assert(m.bluetooth.request_discovery(scope)); // Inject invalidation at the lifecycle boundary.
   } else if(mode=="radio-ttl") {
    clock_ms.fetch_add(TeslaBleDiscovery::RESULT_TTL_MS);assert(!m.ensure_connected());
    assert(!m.gatt_operation(TeslaVehicle::Open,0,[]{assert(false);return true;}));
   }
   bool changed=false;
   if(mode=="mid-generation")fs_fault=[&](const std::string &operation){
    if(!changed && operation.find("rename:")==0 && operation.find("vehicle_vin.tmp")!=std::string::npos){changed=true;++ev.generation;m.refresh_live_session();}
    return false;
   };
   m.process_commands();assert(!r->pair_requested.load());
   fs_fault={};if(mode=="mid-generation")assert(changed);
  }
  assert(!peer_open_calls);if(mode!="mid-generation")assert(LittleFS.files==before);
  assert(!LittleFS.files.count("/config/tesla_p00_112233445500_private_key"));
  r->stop_requested.store(true);run_task(r->task_handle);m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();
 }
};
int main(int argc,char **argv){assert(argc==2);TeslaBleModuleTest::run(argv[1]);assert_charging_untouched();assert(ev.applied==0 && ev.tesla_source_updates==0);std::printf("VIN_BINDING %s PASS\n",argv[1]);}
