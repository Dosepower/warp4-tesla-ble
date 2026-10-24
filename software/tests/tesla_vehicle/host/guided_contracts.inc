// Included inside friend test struct. Synthetic profiles, actual production entrypoint.
 static void guided(TeslaVehicle &m,const std::string &mode) {
  if(mode=="guided-refresh-other-expired") {
   HostBluetoothDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;
   m.find_profile(1)->get("discovery_revision")->updateUint(1);
   auto update=Config::Object({{"entries",Config::Array({},&m.tesla->profile,0,EV_MAX_EVS)}});
   auto entries=update.get("entries");assert(entries->setCount(2));entry(entries->get(size_t{0}),0);entry(entries->get(size_t{1}),1);
   entries->get(size_t{1})->get("discovery_revision")->updateUint(1);
   const auto files=LittleFS.files;
   entries->get(size_t{0})->get("ble_address_type")->updateUint(1);
   entries->get(size_t{0})->get("discovery_revision")->updateUint(host_vin_scan(m.bluetooth,"5YJ3E1EA7KF000001",0,A,1,1));
   assert(m.validate_profiles(update,ConfigSource::API).empty() && "Unchanged expired B must not block explicit fresh A adoption");
   assert(LittleFS.files==files && !m.tesla->runtime_owner.get() && !peer_open_calls && !ev.applied);
   entries->get(size_t{1})->get("ble_address_type")->updateUint(1);
   assert(!m.validate_profiles(update,ConfigSource::API).empty() && "Changed stale B transport still requires fresh exact proof");return;
  }
  if(mode.rfind("guided-reset",0)==0) {
   HostBluetoothDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;
   explicit_scan(m,0);seed(1);auto *p=m.find_profile(0);const auto vin=p->get("vin")->asString();
   auto &c=m.tesla->guided_command;entry(c.get("profile"),0);c.get("profile")->get("discovery_revision")->updateUint(p->get("discovery_revision")->asUint());
   c.get("phase")->updateString("reset");c.get("request")->updateString("00000000000000000000000000000001");c.get("generation")->updateString(std::to_string(ev.generation));
   if(mode=="guided-reset-orphan" || mode=="guided-reset-paired" || mode=="guided-reset-missing-vin") {
    seed(0);TeslaBleStorageAdapter store;assert(store.select_profile(0,A));
    if(mode!="guided-reset-paired")assert(store.remove_key("authorized"));
    if(mode=="guided-reset-missing-vin")assert(store.remove_key("vehicle_vin"));
   }
   c.get("revision")->updateUint(m.tesla->config_revision);
   if(mode=="guided-reset-stale")++m.tesla->config_revision;
   if(mode=="guided-reset-generation")++ev.generation;
   if(mode=="guided-reset-profile")c.get("profile")->get("vin")->updateString("5YJ3E1EA7KF000009");
   if(mode=="guided-reset-unknown"){seed(0);fs_fault=[](const std::string &operation){return operation.rfind("open:",0)==0;};}
   const auto files=LittleFS.files;String error;api.commands.at("tesla_vehicle/guided")(0,error);fs_fault={};
   const bool rejected=mode=="guided-reset-paired" || mode=="guided-reset-stale" || mode=="guided-reset-generation" || mode=="guided-reset-profile" || mode=="guided-reset-missing-vin" || mode=="guided-reset-unknown";
   if(rejected){assert(!error.empty());assert(p->get("enable")->asBool() && !p->get("ble_address")->asString().empty());assert(LittleFS.files==files && api.written_configs.empty() && !peer_open_calls);return;}
   assert(error.empty() && "Confirmed reset must be implemented, not just reopen editing");
   assert(!p->get("enable")->asBool() && p->get("ble_address")->asString().empty() && !p->get("ble_address_type")->asUint() && !p->get("discovery_revision")->asUint());
   assert(p->get("vin")->asString()==vin && !m.tesla->runtime_owner.get());
   for(const auto &file:files)if(file.first.find("tesla_p00_112233445500_ble_binding")==std::string::npos)assert(LittleFS.files.at(file.first)==file.second);
   assert(m.find_profile(1)->get("enable")->asBool());
   error.clear();api.commands.at("tesla_vehicle/guided")(0,error);assert(!error.empty() && "Reset consent single use");
   auto *persisted=api.written_configs.at("tesla_vehicle/profiles").get("entries")->get(size_t{0});
   assert(persisted->get("ble_address")->asString().empty() && !persisted->get("discovery_revision")->asUint() && !persisted->get("enable")->asBool());
   assert(api.written_configs.count("tesla_vehicle/profiles"));
   assert(!m.fresh_binding(0,A,vin.c_str(),"01:02:03:04:05:06",0,c.get("profile")->get("discovery_revision")->asUint()));
   assert(!peer_open_calls && scan_start_calls==1);return;
  }
  HostBluetoothDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;
  if(mode=="guided-poll" || mode=="guided-repair")seed(0);
  if(mode=="guided-adopt-disabled") {
   m.find_profile(0)->get("enable")->updateBool(false);
   m.find_profile(1)->get("enable")->updateBool(false);m.find_profile(1)->get("ble_address")->updateString("");
   m.loop();assert(!m.find_profile(0)->get("enable")->asBool() && !m.tesla->runtime_owner.get() && !peer_open_calls && !scan_start_calls);
  }
  explicit_scan(m,0);m.publish_guidance();
  if(mode=="guided-adopt-disabled") {
   assert(!m.find_profile(0)->get("enable")->asBool()); // Search alone cannot enable.
   m.find_profile(0)->get("enable")->updateBool(true);String error;
   assert(m.validate_profiles(m.tesla->profiles,ConfigSource::API).empty());
   api.writeConfig("tesla_vehicle/profiles",&m.tesla->profiles);m.loop();
   assert(!m.find_profile(1)->get("enable")->asBool() && !m.tesla->runtime_owner.get() && !peer_open_calls && scan_start_calls==1 && ev.applied==0);
  }
  auto &c=m.tesla->guided_command;
  entry(c.get("profile"),0);c.get("profile")->get("discovery_revision")->updateUint(m.find_profile(0)->get("discovery_revision")->asUint());
  c.get("request")->updateString("00000000000000000000000000000001");
  c.get("generation")->updateString(std::to_string(ev.generation));
  c.get("revision")->updateUint(m.tesla->config_revision);
  c.get("phase")->updateString(mode=="guided-poll" || mode=="guided-no-key"?"prepare_poll":"prepare_pair");
  const auto before=LittleFS.files;String error;
  if(mode=="guided-old-generation")++ev.generation;
  if(mode=="guided-stale-prepare")c.get("revision")->updateUint(m.tesla->config_revision+1);
  if(mode=="guided-wrong-config")c.get("profile")->get("ble_address_type")->updateUint(1);
  api.commands.at("tesla_vehicle/guided")(0,error);
  if(mode=="guided-old-generation" || mode=="guided-wrong-config" || mode=="guided-stale-prepare") {
   assert(!error.empty()&&!m.tesla->runtime_owner.get()&&peer_open_calls==0&&LittleFS.files==before);std::puts("GUIDED_PREPARE_REJECT_PASS");return;
  }
  assert(error.empty());auto *r=m.tesla->runtime_owner.get();assert(r && r->profile_pending.load());
  assert(peer_open_calls==0 && !r->pair_requested.load() && !r->poll_requested.load());
  c.get("phase")->updateString("commit");
  if(mode=="guided-pending") {
   error.clear();api.commands.at("tesla_vehicle/guided")(0,error);assert(!error.empty()&&!r->pair_requested.load());
  } else {
   m.apply_profile();m.refresh_live_session();m.update_active_vehicle();m.publish_profile();r->ble_ready.store(true);m.publish_bluetooth_state();m.publish_guidance();
   assert(m.tesla->guidance.get("ready")->asBool());
   if(mode=="guided-generation"){++ev.generation;ev.index=1;ev.mac=B;++ev.generation;ev.index=0;ev.mac=A;}
   if(mode=="guided-timeout")clock_ms.fetch_add(15000);
   if(mode=="guided-ttl") {
    clock_ms.fetch_add(TeslaBleDiscovery::RESULT_TTL_MS);m.publish_discovery();
    auto targets=m.tesla->discovery_state.get("targets");for(size_t i=0;i<targets->count();++i)assert(!targets->get(i)->get("matches_vin")->asBool());
   }
   if(mode=="guided-config"){m.find_profile(1)->get("ble_address")->updateString("");assert(m.validate_profiles(m.tesla->profiles,ConfigSource::API).empty());}
   if(mode=="guided-wrong-request")c.get("request")->updateString("00000000000000000000000000000002");
   if(mode=="guided-stale-commit")c.get("revision")->updateUint(m.tesla->config_revision+1);
   if(mode=="guided-stale-cancel") {
    c.get("phase")->updateString("cancel");c.get("revision")->updateUint(m.tesla->config_revision+1);
    api.commands.at("tesla_vehicle/guided")(0,error);assert(!error.empty());
    assert(m.tesla->guided.request[0] && "Stale cancellation must not consume current lease");
    c.get("revision")->updateUint(m.tesla->config_revision);c.get("phase")->updateString("commit");
   }
   if(mode=="guided-cancel"){c.get("phase")->updateString("cancel");api.commands.at("tesla_vehicle/guided")(0,error);c.get("phase")->updateString("commit");}
   error.clear();api.commands.at("tesla_vehicle/guided")(0,error);
   const bool success=mode=="guided" || mode=="guided-adopt-disabled" || mode=="guided-poll" || mode=="guided-stale-cancel";
   assert(error.empty()==success);
   assert(r->pair_requested.load()==(mode=="guided" || mode=="guided-adopt-disabled" || mode=="guided-stale-cancel"));
   assert(r->poll_requested.load()==(mode=="guided-poll"));
   if(success){error.clear();api.commands.at("tesla_vehicle/guided")(0,error);assert(!error.empty());}
  }
  assert(peer_open_calls==0 && ev.applied==0); // No worker radio execution before explicit commit.
  r->pair_requested.store(false);r->poll_requested.store(false);r->manual_poll_pending.store(false);
  r->stop_requested.store(true);run_task(r->task_handle);m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();
  std::printf("GUIDED_CASE_PASS %s\n",mode.c_str());
 }
