// Official-protoc bytes -> native RX -> real worker/main -> API, offline only.
 static void pair_response_flow(TeslaVehicle &m) {
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;explicit_scan(m,0);seed(0);seed(1);
  TeslaBleStorageAdapter store;assert(store.select_profile(0,A));assert(store.remove_key("authorized"));
  const auto retained=LittleFS.files;String error;assert(m.select_profile(0,false,error));auto*r=m.tesla->runtime_owner.get();
  const char *hex=std::getenv("TESLA_PAIR_RESPONSE_HEX");assert(hex);std::vector<uint8_t> payload;
  for(size_t i=0;i<std::strlen(hex);i+=2){unsigned b=0;assert(std::sscanf(hex+i,"%2x",&b)==1);payload.push_back(b);}
  unsigned stage=0;uint32_t sent=0;
  test_submit=nimble_auto_submit;on_task_delay=[&](uint32_t){m.loop();};
  on_delay=[&]{
   if(!running_task||running_task->name!="tesla_ble"||r->profile_pending.load())return;m.loop();
   if(stage==0){m.tesla->profile_command.get("ev_index")->updateInt(0);api.commands.at("tesla_vehicle/pair")(0,error);assert(error.empty());stage=1;return;}
   if(stage==1&&m.tesla->state.get("pair_request_sent")->asBool()){
    sent=millis();std::vector<uint8_t> wire{uint8_t(payload.size()>>8),uint8_t(payload.size())};wire.insert(wire.end(),payload.begin(),payload.end());
    for(size_t i=0;i<wire.size();i+=3){os_mbuf mb;mb.bytes.assign(wire.begin()+i,wire.begin()+std::min(wire.size(),i+3));ble_gap_event e{};e.type=BLE_GAP_EVENT_NOTIFY_RX;e.notify_rx.conn_handle=4;e.notify_rx.attr_handle=20;e.notify_rx.om=&mb;test_gap(&e,test_gap_arg);}stage=2;return;
   }
   if(stage==2){
    assert(millis()-sent<2000 && "Specific Tesla rejection was ignored by production RX and became a local timeout");
    if(m.tesla->state.get("pairing_pending")->asBool())return;
    assert(m.tesla->state.get("last_error")->asString().find("VCSEC whitelist information=24")!=std::string::npos);
    assert(!r->authorized.load()&&!r->connected.load()&&!r->operation.retired()&&!m.tesla->runtime_owner.restart_required());
    assert(!m.tesla->state.get("paired")->asBool()&&LittleFS.files==retained&&ev.applied==0);stage=3;r->stop_requested=true;
   }
  };
  run_task(r->task_handle);on_delay={};on_task_delay={};test_submit={};assert(stage==3);m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();
  std::puts("PAIR_RESPONSE_PASS official wire reached native adapter/queue/worker/main/API; explicit UI denial not timeout");
 }
