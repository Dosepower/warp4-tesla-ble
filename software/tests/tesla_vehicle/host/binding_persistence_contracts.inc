// Pairing VIN/MAC binding persistence through the complete production
// guided API -> main -> worker -> mutation guard -> TeslaBleStorageAdapter
// POSIX target branch -> extracted esp_littlefs 1.22.3 VFS -> full lfs.c path.
// Built only by test_binding_persistence.py (TESLA_BLE_BINDING_VFS_TEST).
//
// The publication hook below exists only in the scratch copy of
// tesla_ble_result.h compiled by that runner; it pauses a writer exactly after
// its odd coherence-version store, i.e. the state a concurrent main-loop
// republication exposes to the worker on the dual-core target.
#ifdef TESLA_BLE_BINDING_VFS_TEST
// Hook state (tesla_binding_*) and tesla_identity_publication_hook() are
// injected with the hook into the scratch tesla_ble_result.h copy.
 static std::vector<uint8_t> stored(int index,uint64_t mac,const char*key) {
  TeslaBleStorageAdapter s;assert(s.select_profile(index,mac));static uint8_t b[4096];size_t n=0;
  const auto r=s.load_into(key,b,sizeof(b),n);if(r==tesla_clean::LoadResult::Absent)return {};
  assert(r==tesla_clean::LoadResult::Loaded);return std::vector<uint8_t>(b,b+n);
 }
 static std::map<std::string,std::vector<uint8_t>> namespace_bytes(int index,uint64_t mac) {
  std::map<std::string,std::vector<uint8_t>> out;
  for(const char*k:{"private_key","session_vcsec","session_infotainment","warp_ev_mac","vehicle_vin","authorized","ble_binding"})out[k]=stored(index,mac,k);
  return out;
 }
 // Modes:
 //  binding-republish  retained key/VIN/MAC; every attempt: main republishes the
 //                     UNCHANGED live identity, paused mid-publication, while the
 //                     worker persists the MAC binding. Each attempt must transmit.
 //  binding-first      same, but no previously committed MAC binding file.
 //  binding-change     control: attempt 1 publishes a CHANGED identity paused
 //                     mid-publication during the MAC save -> must fail closed
 //                     without transmission/mutation; later explicit attempts with
 //                     unchanged republication / no interference must transmit.
 //  binding-faults     control: real storage failures at each MAC-save step must
 //                     fail closed, retain key/VIN/assignment, never transmit, and
 //                     a subsequent explicit attempt must succeed.
 static void binding_flow(TeslaVehicle &m,const std::string &mode) {
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;explicit_scan(m,0);
  seed(0);seed(1);TeslaBleStorageAdapter store;assert(store.select_profile(0,A));assert(store.remove_key("authorized"));
  if(mode=="binding-first")assert(store.remove_key("warp_ev_mac"));
  const auto other=namespace_bytes(1,B);const auto initial=namespace_bytes(0,A);
  const auto expected_mac=encode_vehicle_mac(A);
  const auto assigned_address=m.find_profile(0)->get("ble_address")->asString();auto assigned_revision=m.find_profile(0)->get("discovery_revision")->asUint();
  // Every pre-commit step of the real transaction. Close failure (quarantine) and
  // post-commit .bak cleanup are covered by the existing LittleFS-close suites.
  const std::vector<std::string> faults={"open","write","fsync","fstat","read","stat","rename"};
  const unsigned attempts=mode=="binding-faults"?unsigned(faults.size())+1:3;
  String error;unsigned attempt=0,stage=0,frames=0,injections=0,sent_attempts=0,failed_attempts=0;std::vector<uint8_t> wire;
  std::thread publisher;bool arm=false,change=false,committed=false;std::string fault_op;unsigned fault_hits=0;
  auto command=[&](const char*phase){
   auto &c=m.tesla->guided_command;auto*p=m.find_profile(0);entry(c.get("profile"),0);
   for(const char*k:{"ble_address_type","discovery_revision","poll_interval"})c.get("profile")->get(k)->updateUint(p->get(k)->asUint());
   c.get("phase")->updateString(phase);char id[33];snprintf(id,sizeof(id),"%032u",attempt+101);c.get("request")->updateString(id);
   c.get("generation")->updateString(std::to_string(ev.generation));c.get("revision")->updateUint(m.tesla->config_revision);
   error.clear();api.commands.at("tesla_vehicle/guided")(0,error);
   if(!error.empty())std::fprintf(stderr,"BINDING phase=%s stage=%u attempt=%u error=%s\n",phase,stage,attempt,error.c_str());assert(error.empty());
  };
  auto plan=[&]{
   arm=false;change=false;committed=false;fault_op.clear();fault_hits=0;
   if(mode=="binding-change"){arm=attempt<2;change=attempt==0;}
   else if(mode=="binding-faults"){if(attempt<faults.size())fault_op=faults[attempt];}
   else arm=true;
  };
  decltype(m.tesla->runtime_owner.get()) r=nullptr;
  vfs_bridge_fault=[&](const char*op,const std::string&path)->int{
   const bool mac=path.find("_warp_ev_mac")!=std::string::npos && path.find("/config/tesla_p00_")!=std::string::npos;
   if(!mac||!committed)return 0; // Only the accepted command's worker transaction.
   if(!fault_op.empty() && fault_op==op && !fault_hits){
    // Real failure of the selected step; never the canonical stat probe of a load.
    if(std::string(op)=="stat" && path.find(".bak")==std::string::npos)return 0;
    ++fault_hits;return EIO;
   }
   if(arm && std::string(op)=="open" && path.size()>4 && path.compare(path.size()-4,4,".tmp")==0) {
    arm=false;++injections;
    {std::lock_guard<std::mutex> lock(tesla_binding_schedule);tesla_binding_paused=tesla_binding_release=tesla_binding_done=false;}
    const auto identity=change?TeslaBleIdentity{ev.generation+1,0,A}:current_ev_identity();
    publisher=std::thread([r,identity]{
     tesla_binding_publisher=true;r->session.publish(identity);tesla_binding_publisher=false; // main-loop publication statement
     std::lock_guard<std::mutex> lock(tesla_binding_schedule);tesla_binding_done=true;tesla_binding_cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(tesla_binding_schedule);tesla_binding_cv.wait(lock,[]{return tesla_binding_paused||tesla_binding_done;});
    return 0;
   }
   if(std::string(op)=="close" && path.compare(path.size()-4,4,".tmp")==0) {
    std::lock_guard<std::mutex> lock(tesla_binding_schedule);if(tesla_binding_paused&&!tesla_binding_release){tesla_binding_release=true;tesla_binding_cv.notify_all();}
   }
   return 0;
  };
  auto finish_publisher=[&]{
   if(!publisher.joinable())return;
   {std::lock_guard<std::mutex> lock(tesla_binding_schedule);tesla_binding_release=true;tesla_binding_cv.notify_all();}
   publisher.join();
  };
  plan();command("prepare_pair");r=m.tesla->runtime_owner.get();assert(r);const auto epoch=r->epoch;
  on_gatt_bytes=[&](const uint8_t*p,size_t n){wire.insert(wire.end(),p,p+n);if(wire.size()>=2 && wire.size()==size_t(wire[0])*256+wire[1]+2){++frames;wire.clear();}};
  test_submit=nimble_auto_submit;
  on_task_delay=[&](uint32_t){m.loop();};
  on_delay=[&]{
   if(!running_task||running_task->name!="tesla_ble")return;
   finish_publisher();m.loop();assert(r==m.tesla->runtime_owner.get()&&r->epoch==epoch&&!r->stop_requested.load());
   if(r->profile_pending.load())return;
   assert(!m.tesla->runtime_owner.restart_required() && !r->storage->restart_required());
   if(stage==0){m.publish_guidance();assert(m.tesla->guidance.get("ready")->asBool());committed=true;command("commit");stage=1;return;}
   const bool expect_fail=(mode=="binding-change"&&attempt==0)||(mode=="binding-faults"&&attempt<faults.size());
   if(stage==1 && m.tesla->state.get("pair_request_sent")->asBool()) {
    if(expect_fail){std::fprintf(stderr,"FAIL: attempt %u transmitted despite %s\n",attempt+1,change?"changed identity":fault_op.c_str());std::abort();}
    // Exact binary EV/SLAC MAC binding persisted before transmission; key reused.
    assert(stored(0,A,"warp_ev_mac")==std::vector<uint8_t>(expected_mac.begin(),expected_mac.end()));
    assert(stored(0,A,"private_key")==initial.at("private_key") && stored(0,A,"vehicle_vin")==initial.at("vehicle_vin"));
    assert(stored(0,A,"authorized").empty() && !r->authorized.load() && !m.tesla->state.get("paired")->asBool());
    ++sent_attempts;stage=2;return;
   }
   if(stage==1 && !m.tesla->state.get("pairing_pending")->asBool()) {
    const auto last=m.tesla->state.get("last_error")->asString();
    if(!expect_fail) {
     std::fprintf(stderr,"RED_MECHANISM attempt=%u injections=%u outcome=%s last_error=%s: unchanged identity republication during the MAC binding save aborted persistence before transmission\n",
      attempt+1,injections,m.tesla->state.get("pair_outcome")->asString().c_str(),last.c_str());
     // Immediate distinct exit from inside the parked worker context: no
     // static destructors/leak scan racing the still-registered host threads.
     std::fflush(stdout);std::fflush(stderr);std::_Exit(3);
    }
    assert(frames==0 && !r->authorized.load() && !m.tesla->state.get("paired")->asBool());
    assert(last=="Could not persist WARP vehicle MAC binding");
    // Fail closed: nothing pretended committed; key/VIN/assignment retained.
    const auto mac_now=stored(0,A,"warp_ev_mac");assert(mac_now==initial.at("warp_ev_mac")||mac_now==std::vector<uint8_t>(expected_mac.begin(),expected_mac.end()));
    assert(stored(0,A,"private_key")==initial.at("private_key") && stored(0,A,"vehicle_vin")==initial.at("vehicle_vin") && stored(0,A,"authorized").empty());
    assert(m.find_profile(0)->get("ble_address")->asString()==assigned_address && m.find_profile(0)->get("enable")->asBool());
    if(mode=="binding-faults")assert(fault_hits==1);
    std::printf("BINDING_FAIL_CLOSED attempt=%u cause=%s outcome=%s\n",attempt+1,change?"changed-identity":fault_op.c_str(),m.tesla->state.get("pair_outcome")->asString().c_str());
    ++failed_attempts;stage=3;
   }
   if(stage==2 && !m.tesla->state.get("pairing_pending")->asBool()) {
    assert(m.tesla->state.get("last_error")->asString().find("Approval deadline expired;")==0);
    std::printf("BINDING_ATTEMPT_PASS mode=%s attempt=%u injected=%u frames=%u\n",mode.c_str(),attempt+1,injections,frames);stage=3;
   }
   if(stage==3) {
    for(const auto&f:other)assert(stored(1,B,f.first.c_str())==f.second); // multi-profile isolation
    assert(!m.tesla->state.get("restart_required")->asBool() && m.bluetooth.active());
    ++attempt;frames=0;
    if(attempt==attempts){r->stop_requested=true;stage=9;return;}
    if(!m.fresh_binding(0,A,m.find_profile(0)->get("vin")->asString().c_str(),assigned_address.c_str(),0,assigned_revision)) {
     explicit_scan(m,0);assigned_revision=m.find_profile(0)->get("discovery_revision")->asUint();
     assert(m.validate_profiles(m.tesla->profiles,ConfigSource::API).empty());api.writeConfig("tesla_vehicle/profiles",&m.tesla->profiles);
    }
    plan();command("prepare_pair");stage=0;
   }
  };
  run_task(r->task_handle);finish_publisher();on_delay={};on_task_delay={};on_gatt_bytes={};test_submit={};vfs_bridge_fault={};
  assert(stage==9&&attempt==attempts);
  if(mode=="binding-change")assert(failed_attempts==1&&sent_attempts==2&&injections==2);
  else if(mode=="binding-faults")assert(failed_attempts==faults.size()&&sent_attempts==1);
  else assert(sent_attempts==3&&injections==3);
  m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();assert(ev.applied==0);
  std::printf("BINDING_RUNTIME_PASS mode=%s attempts=%u transmitted=%u fail_closed=%u isolation=profile1-unchanged\n",mode.c_str(),attempts,sent_attempts,failed_attempts);
 }
 // binding-restore: the key + authorization + ble_binding committed through the production
// storage transactions on the real esp_littlefs VFS/lfs (seed(): TeslaBleStorageAdapter
// save_transaction, the same path pairing uses) are restored into a NEW Runtime by the normal
// guided refresh, with the clock far past the discovery TTL and no discovery result:
//   boot 0: new Runtime in the same module instance (worker recreation, no reboot);
//   boot 1: reboot-equivalent - a FRESH TeslaVehicle + fresh Bluetooth lifecycle/NimBLE host
//           (like the empty-legacy boots); only the VFS/lfs image and the WARP EV identity
//           survive. (After a same-module teardown of a stopped worker the old module reports
//           restart_required=1 / Bluetooth inactive - identical on the baseline production by
//           control - so only a fresh module models a reboot. Not a gate of this change.)
// Each boot: worker connects only to the stored address; nothing is written, nothing scanned.
// Control: a persisted profile address that differs from the stored binding is refused.
// Uses only API common to the pre-change module, so --expect-red shows RED_RESTORE there.
static void binding_restore_flow(TeslaVehicle &m0) {
 auto driver0=std::make_unique<HostNimbleDriver>(m0.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;test_submit=nimble_auto_submit;
 seed(0);seed(1);
 const auto initial=namespace_bytes(0,A),other=namespace_bytes(1,B);
 assert(!initial.at("private_key").empty()&&!initial.at("ble_binding").empty()&&initial.at("authorized")==std::vector<uint8_t>{1});
 const int scans0=test_scans;String error;unsigned id=500;uint64_t last_epoch=0;
 TeslaVehicle *mod=&m0;std::unique_ptr<TeslaVehicle> fresh;std::unique_ptr<HostNimbleDriver> driver1;
 auto guided=[&](const char*phase)->std::string{
  auto &m=*mod;auto &c=m.tesla->guided_command;auto*p=m.find_profile(0);auto*q=c.get("profile");
  for(const char*k:{"ev_mac","vin","ble_address"})q->get(k)->updateString(p->get(k)->asString());
  q->get("ev_index")->updateInt(0);q->get("enable")->updateBool(true);
  for(const char*k:{"ble_address_type","discovery_revision","poll_interval"})q->get(k)->updateUint(p->get(k)->asUint());
  c.get("phase")->updateString(phase);char rid[33];snprintf(rid,sizeof(rid),"%032u",++id);c.get("request")->updateString(rid);
  c.get("generation")->updateString(std::to_string(ev.generation));c.get("revision")->updateUint(m.tesla->config_revision);
  error.clear();api.commands.at("tesla_vehicle/guided")(0,error);return error.c_str();
 };
 for(unsigned boot=0;boot<2;++boot){
  if(boot==1){
   // Reboot: the old module and its Bluetooth lifecycle are gone; only the VFS image stays.
   driver0.reset();
   fresh=std::make_unique<TeslaVehicle>();fresh->pre_setup();fresh->setup();
   fresh->bluetooth_boot_enabled=true;fresh->bluetooth_config.get("enable")->updateBool(true);
   assert(fresh->prepare_tesla());fresh->register_urls();
   auto *entries=fresh->tesla->profiles.get("entries");assert(entries->setCount(2));entry(entries->get(size_t{0}),0);entry(entries->get(size_t{1}),1);
   assert(fresh->validate_profiles(fresh->tesla->profiles,ConfigSource::File).empty()); // profiles as restored from the config file
   mod=fresh.get();driver1=std::make_unique<HostNimbleDriver>(mod->bluetooth,TeslaVehicle::gatt_callback);
   assert(namespace_bytes(0,A)==initial&&namespace_bytes(1,B)==other);
  }
  auto &m=*mod;
  assert(!m.tesla->runtime_owner.get()&&m.bluetooth.active());clock_ms.fetch_add(10*TeslaBleDiscovery::RESULT_TTL_MS+1);
  const auto opens=peer_open_calls;
  const auto e=guided("prepare_poll");
  if(!e.empty()){std::fprintf(stderr,"RED_RESTORE boot=%u persisted refresh refused: %s\n",boot,e.c_str());std::fflush(stderr);std::_Exit(3);}
  auto *r=m.tesla->runtime_owner.get();assert(r&&(boot==1||r->epoch!=last_epoch));last_epoch=r->epoch;
  bool checked=false;
  on_delay=[&]{
   if(checked||r->profile_pending.load())return;checked=true;m.loop();
   // Restored from the VFS by the worker's apply_profile: authorization, exact key/VIN/MAC, stored address.
   if(!(r->authorized.load()&&m.runtime_binding_matches()&&m.runtime_binding_matches(true))){
    std::fprintf(stderr,"RED_RESTORE boot=%u restored runtime not bound\n",boot);std::fflush(stderr);std::_Exit(3);}
   const uint8_t stored[6]{1,2,3,4,5,6};assert(!memcmp(r->remote_address,stored,6)&&r->remote_address_type==0);
   assert(m.ensure_connected()&&peer_open_calls==opens+1); // connects to the stored address, no scan
   assert(test_peer.type==0&&test_peer.val[5]==1&&test_peer.val[0]==6);
   m.publish_guidance();assert(m.tesla->guidance.get("ready")->asBool());
   r->stop_requested.store(true);
  };
  run_task(r->task_handle);on_delay={};assert(checked);m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();
  assert(namespace_bytes(0,A)==initial&&namespace_bytes(1,B)==other&&test_scans==scans0);
  std::printf("BINDING_RESTORE boot=%u restored from VFS: authorized, stored address, no scan, no write\n",boot);
 }
 // Control (rebooted module): a persisted profile address that differs from the committed
 // binding is refused, nothing opened or written. Needs a module whose Bluetooth is usable.
 driver1.reset();
 fresh=std::make_unique<TeslaVehicle>();fresh->pre_setup();fresh->setup();
 fresh->bluetooth_boot_enabled=true;fresh->bluetooth_config.get("enable")->updateBool(true);
 assert(fresh->prepare_tesla());fresh->register_urls();
 {auto *entries=fresh->tesla->profiles.get("entries");assert(entries->setCount(2));entry(entries->get(size_t{0}),0);entry(entries->get(size_t{1}),1);}
 assert(fresh->validate_profiles(fresh->tesla->profiles,ConfigSource::File).empty());
 mod=fresh.get();driver1=std::make_unique<HostNimbleDriver>(mod->bluetooth,TeslaVehicle::gatt_callback);
 auto &m=*mod;
 m.find_profile(0)->get("ble_address")->updateString("04:02:03:04:05:06");clock_ms.fetch_add(1000);
 const auto opens=peer_open_calls;const auto refused=guided("prepare_poll");
 assert(!refused.empty()&&refused.find("VIN")!=std::string::npos&&refused.find("Bluetooth inactive")==std::string::npos); // binding refusal, not an inactive stack
 std::printf("BINDING_RESTORE control changed address refused: %s\n",refused.c_str());
 if(auto *r=m.tesla->runtime_owner.get()){r->stop_requested.store(true);run_task(r->task_handle);m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();}
 assert(peer_open_calls==opens&&namespace_bytes(0,A)==initial&&namespace_bytes(1,B)==other&&test_scans==scans0);
 test_submit={};assert(ev.applied==0);
 std::printf("BINDING_RESTORE_PASS runtime recreation + reboot (fresh module) restored key+ble_binding from VFS; changed address refused\n");
}
#endif
