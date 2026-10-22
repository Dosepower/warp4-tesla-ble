// Included inside friend test struct. Synthetic profiles, actual production entrypoint.
 static void guided(TeslaVehicle &m,const std::string &mode) {
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
  c.get("phase")->updateString(mode=="guided-poll" || mode=="guided-no-key"?"prepare_poll":"prepare_pair");
  const auto before=LittleFS.files;String error;
  if(mode=="guided-old-generation")++ev.generation;
  if(mode=="guided-wrong-config")c.get("profile")->get("ble_address_type")->updateUint(1);
  api.commands.at("tesla_vehicle/guided")(0,error);
  if(mode=="guided-old-generation" || mode=="guided-wrong-config") {
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
   if(mode=="guided-cancel"){c.get("phase")->updateString("cancel");api.commands.at("tesla_vehicle/guided")(0,error);c.get("phase")->updateString("commit");}
   error.clear();api.commands.at("tesla_vehicle/guided")(0,error);
   const bool success=mode=="guided" || mode=="guided-adopt-disabled" || mode=="guided-poll";
   assert(error.empty()==success);
   assert(r->pair_requested.load()==(mode=="guided" || mode=="guided-adopt-disabled"));
   assert(r->poll_requested.load()==(mode=="guided-poll"));
   if(success){error.clear();api.commands.at("tesla_vehicle/guided")(0,error);assert(!error.empty());}
  }
  assert(peer_open_calls==0 && ev.applied==0); // No worker radio execution before explicit commit.
  r->pair_requested.store(false);r->poll_requested.store(false);r->manual_poll_pending.store(false);
  r->stop_requested.store(true);run_task(r->task_handle);m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();
  std::printf("GUIDED_CASE_PASS %s\n",mode.c_str());
 }
