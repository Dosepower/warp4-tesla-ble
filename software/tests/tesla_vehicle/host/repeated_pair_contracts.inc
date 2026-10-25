// Complete production worker/main/guided/queue/transport path, no hardware.
 static void repeated_flow(TeslaVehicle &m,bool cleanup) {
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;explicit_scan(m,0);
  seed(0);seed(1);TeslaBleStorageAdapter store;assert(store.select_profile(0,A));assert(store.remove_key("authorized"));
  const auto other_prefix=std::string("/config/tesla_p01_");const auto initial=LittleFS.files;
  const auto assigned_address=m.find_profile(0)->get("ble_address")->asString();auto assigned_revision=m.find_profile(0)->get("discovery_revision")->asUint();
  String error;unsigned attempt=0,stage=0,frames=0,main_ticks=0;uint32_t started=0;uint64_t epoch=0;bool deleted=false;std::vector<uint8_t> wire,retained;
  auto command=[&](const char*phase){
   auto &c=m.tesla->guided_command;auto*p=m.find_profile(0);entry(c.get("profile"),0);
   for(const char*k:{"ble_address_type","discovery_revision","poll_interval"})c.get("profile")->get(k)->updateUint(p->get(k)->asUint());
   c.get("phase")->updateString(phase);char id[33];snprintf(id,sizeof(id),"%032u",attempt+1);c.get("request")->updateString(id);
   c.get("generation")->updateString(std::to_string(ev.generation));c.get("revision")->updateUint(m.tesla->config_revision);
   error.clear();api.commands.at("tesla_vehicle/guided")(0,error);if(!error.empty())std::fprintf(stderr,"REPEAT phase=%s stage=%u attempt=%u error=%s\n",phase,stage,attempt,error.c_str());assert(error.empty());
  };
  command("prepare_pair");auto*r=m.tesla->runtime_owner.get();assert(r);epoch=r->epoch;
  on_gatt_bytes=[&](const uint8_t*p,size_t n){wire.insert(wire.end(),p,p+n);if(wire.size()>=2 && wire.size()==size_t(wire[0])*256+wire[1]+2){++frames;wire.clear();}};
  test_submit=nimble_auto_submit;
  on_task_delay=[&](uint32_t){++main_ticks;m.loop();};
  on_delay=[&]{
   if(!running_task||running_task->name!="tesla_ble")return;m.loop();assert(r==m.tesla->runtime_owner.get()&&r->epoch==epoch&&!r->stop_requested.load());
   if(r->profile_pending.load())return;
   assert(!m.tesla->runtime_owner.restart_required());
   if(stage==0){m.publish_guidance();assert(m.tesla->guidance.get("ready")->asBool());command("commit");stage=1;return;}
   if(stage==1 && m.tesla->state.get("pair_request_sent")->asBool()) {started=r->pair_started_ms;stage=2;return;}
   if(stage==2 && !m.tesla->state.get("pairing_pending")->asBool()) {
    assert(millis()-started>=60000 && millis()-started<=61000 && frames>=3);
    assert(!r->authorized.load()&&!r->operation.retired()&&!r->connected.load()&&!m.tesla->state.get("paired")->asBool());
    assert(m.tesla->state.get("last_error")->asString().find("Approval deadline expired;")==0 && "Normal expiry must be distinct from session/link errors");
    assert(m.find_profile(0)->get("ble_address")->asString()==assigned_address && m.find_profile(0)->get("discovery_revision")->asUint()==assigned_revision && m.find_profile(0)->get("enable")->asBool());
    for(const auto&f:initial)if(f.first.find(other_prefix)==0)assert(LittleFS.files.at(f.first)==f.second);
    auto key=LittleFS.files.at("/config/tesla_p00_112233445500_private_key");if(retained.empty())retained=key;else assert(key==retained);
    std::printf("SEQUENTIAL_ATTEMPT_PASS attempt=%u elapsed_ms=%u frames=%u epoch=%llu\n",attempt+1,millis()-started,frames,(unsigned long long)epoch);
    ++attempt;frames=0;
    if(attempt==4){r->stop_requested=true;stage=9;return;}
    if(cleanup && attempt==3){command("cleanup");stage=3;return;}
    // Expired discovery is refreshed ONLY as part of this next explicit consent.
    if(!m.fresh_binding(0,A,m.find_profile(0)->get("vin")->asString().c_str(),assigned_address.c_str(),0,assigned_revision)) {
     explicit_scan(m,0);assigned_revision=m.find_profile(0)->get("discovery_revision")->asUint();
     assert(m.validate_profiles(m.tesla->profiles,ConfigSource::API).empty());api.writeConfig("tesla_vehicle/profiles",&m.tesla->profiles);
    }
    command("prepare_pair");stage=0;return;
   }
   if(stage==3){
    m.publish_guidance();assert(store.private_key_presence()==TeslaBleStorageAdapter::KeyPresence::Absent);
    assert(m.tesla->state.get("key_presence")->asString()=="absent" && !m.tesla->state.get("key_present")->asBool());
    assert(m.find_profile(0)->get("ble_address")->asString()==assigned_address && m.find_profile(0)->get("enable")->asBool());
    retained.clear();deleted=true;command("prepare_pair");stage=0;
   }
  };
  run_task(r->task_handle);on_delay={};on_task_delay={};on_gatt_bytes={};test_submit={};assert(stage==9&&attempt==4&&main_ticks>1000&&(!cleanup||deleted));
  m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();assert(ev.applied==0);std::puts("SEQUENTIAL_RUNTIME_PASS 4 complete 60s attempts, no restart/manual rediscovery/key deletion workaround; no false pairing/SoC");
 }
