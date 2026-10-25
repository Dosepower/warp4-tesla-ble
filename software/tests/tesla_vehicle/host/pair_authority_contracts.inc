 static void authority_flow(TeslaVehicle&m,const std::string&mode) {
  if(mode=="authority-rollover")clock_ms.store(UINT32_MAX-150000);
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;explicit_scan(m,0);
  seed(0);TeslaBleStorageAdapter store;assert(store.select_profile(0,A));assert(store.remove_key("authorized"));
  TeslaBleDiscovery::Snapshot snap;assert(m.bluetooth.discovery_snapshot(snap));
  const bool aged=mode!="authority-fresh"&&mode!="authority-prepare-delay";if(aged)clock_ms.store(snap.completed_at_ms+119000);
  auto*p=m.find_profile(0);assert(m.fresh_binding(0,A,p->get("vin")->asString().c_str(),p->get("ble_address")->asString().c_str(),0,p->get("discovery_revision")->asUint()));
  std::deque<std::function<void()>>main_callbacks;unsigned callbacks=0,stage=0,frames=0;uint32_t began=0,sent=0;String error;std::vector<uint8_t>wire;
  auto command=[&](const char*phase){
   auto&c=m.tesla->guided_command;auto*q=c.get("profile");entry(q,0);
   for(const char*k:{"ble_address_type","discovery_revision","poll_interval"})q->get(k)->updateUint(p->get(k)->asUint());
   c.get("phase")->updateString(phase);c.get("request")->updateString("00000000000000000000000000000001");c.get("generation")->updateString(std::to_string(ev.generation));c.get("revision")->updateUint(m.tesla->config_revision);
   error.clear();api.commands.at("tesla_vehicle/guided")(0,error);
   if(mode=="authority-commit-expired"&&std::string(phase)=="commit") {assert(!error.empty());return;}
   assert(error.empty());++callbacks;
  };
  command("prepare_pair");auto*r=m.tesla->runtime_owner.get();const auto epoch=r->epoch;
  on_gatt_bytes=[&](const uint8_t*b,size_t n){wire.insert(wire.end(),b,b+n);if(wire.size()>1&&wire.size()==size_t(wire[0])*256+wire[1]+2){++frames;wire.clear();}};
  test_submit=nimble_auto_submit;
  auto main_pass=[&]{auto*worker=running_task;running_task=nullptr;while(!main_callbacks.empty()){auto f=std::move(main_callbacks.front());main_callbacks.pop_front();f();}m.loop();running_task=worker;};
  on_task_delay=[&](uint32_t){main_pass();};
  on_delay=[&]{
   if(!running_task||running_task->name!="tesla_ble"||stage==9)return;
   main_pass();if(stage==9||r->profile_pending.load())return;
   if(stage==0){assert(m.tesla->guidance.get("ready")->asBool());main_callbacks.push_back([&]{
    if(mode=="authority-prepare-delay")clock_ms.fetch_add(14000);
    if(mode=="authority-commit-expired")clock_ms.store(snap.completed_at_ms+120001);
    command("commit");began=millis();
    if(mode=="authority-commit-expired"){assert(callbacks==1&&!frames&&!r->authorized.load());stage=9;r->stop_requested=true;}
   });stage=1;return;}
   if(m.tesla->state.get("pair_request_sent")->asBool()&&!sent)sent=millis();
   const auto err=m.tesla->state.get("last_error")->asString();
   if(!err.empty()&&!m.tesla->state.get("pairing_pending")->asBool()) {
    std::printf("REVIEW_TTL mode=%s accepted_callbacks=%u elapsed_ms=%u sent=%u frames=%u result=%d restart=%d stopped=%d same_epoch=%d error=%s\n",mode.c_str(),callbacks,millis()-began,sent?1:0,frames,r->pair_result.load(),m.tesla->state.get("restart_required")->asBool(),r->stop_requested.load(),r->epoch==epoch,err.c_str());std::fflush(stdout);
    assert(callbacks==2&&!r->authorized.load()&&!r->operation.retired()&&!r->stop_requested.load());assert(m.bluetooth.central()->idle());assert(!r->command_outcome.pair_binding_current(millis(),current_ev_identity(),m.tesla->config_revision,r->binding_revision).value_or(false));
    assert(millis()-began>=60000);assert(err.find("Approval deadline expired")!=std::string::npos);
    stage=9;r->stop_requested=true;
   }
   assert(millis()-snap.completed_at_ms<200000);
  };
  run_task(r->task_handle);on_delay={};on_task_delay={};on_gatt_bytes={};assert(stage==9);m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();
 }
