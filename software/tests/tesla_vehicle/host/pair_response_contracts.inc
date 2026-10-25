// Official-protoc bytes -> native RX -> real worker/main -> strict Config API.
 static void pair_response_flow(TeslaVehicle &m) {
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;explicit_scan(m,0);seed(0);seed(1);
  TeslaBleStorageAdapter store;assert(store.select_profile(0,A));assert(store.remove_key("authorized"));
  const auto retained=LittleFS.files;String error;assert(m.select_profile(0,false,error));auto*r=m.tesla->runtime_owner.get();const auto epoch=r->epoch;
  struct Case{std::string hex;int code=24;bool terminal=true;};std::vector<Case>cases;
  if(const char *path=std::getenv("TESLA_PAIR_RESPONSE_CASES")){std::ifstream in(path);std::string line;while(std::getline(in,line)){std::istringstream row(line);Case c;row>>c.code>>c.terminal>>c.hex;assert(row);cases.push_back(c);}}
  else{const char *hex=std::getenv("TESLA_PAIR_RESPONSE_HEX");assert(hex);cases.push_back({hex,24,true});}
  assert(!cases.empty());unsigned stage=0,index=0,duplicates=0;uint32_t sent=0;uint64_t last_generation=0;std::vector<uint8_t>oldwire;
  auto inject=[&](const std::string&hex){if(hex=="-")return;std::vector<uint8_t>wire;
   const bool routed=!hex.empty()&&hex[0]=='@';
   for(size_t i=routed?1:0;i<hex.size();i+=2){unsigned b=0;assert(std::sscanf(hex.c_str()+i,"%2x",&b)==1);wire.push_back(b);}
   if(routed){const std::array<uint8_t,16>marker{{'A','A','A','A','A','A','A','A','A','A','A','A','A','A','A','A'}};auto at=std::search(wire.begin(),wire.end(),marker.begin(),marker.end());assert(at!=wire.end());std::copy(r->routing_address.begin(),r->routing_address.end(),at);}
   std::vector<uint8_t>framed{uint8_t(wire.size()>>8),uint8_t(wire.size())};framed.insert(framed.end(),wire.begin(),wire.end());
   for(size_t i=0;i<framed.size();i+=7){os_mbuf mb;mb.bytes.assign(framed.begin()+i,framed.begin()+std::min(framed.size(),i+7));ble_gap_event e{};e.type=BLE_GAP_EVENT_NOTIFY_RX;e.notify_rx.conn_handle=4;e.notify_rx.attr_handle=20;e.notify_rx.om=&mb;test_gap(&e,test_gap_arg);}oldwire=framed;m.process_rx();
  };
  test_submit=nimble_auto_submit;on_task_delay=[&](uint32_t){m.loop();};
  on_delay=[&]{
   if(!running_task||running_task->name!="tesla_ble"||r->profile_pending.load())return;m.loop();assert(r->epoch==epoch&&!m.tesla->runtime_owner.restart_required());
   if(stage==0){m.tesla->profile_command.get("ev_index")->updateInt(0);error.clear();api.commands.at("tesla_vehicle/pair")(0,error);assert(error.empty());stage=1;return;}
   if(stage==1&&m.tesla->state.get("pair_request_sent")->asBool()){
    sent=r->pair_started_ms;
    // Previous connection callbacks cannot poison the successor's raw status.
    if(last_generation){assert(!r->vehicle->receive({oldwire.data(),oldwire.size()},last_generation));TeslaVehicle::gatt_callback({TeslaNimbleEvent::Rx,last_generation,4,20,0,oldwire.data(),oldwire.size()});m.process_rx();}
    os_mbuf wrong;wrong.bytes={0,6,0x22,4,0x1a,2,8,24};ble_gap_event foreign{};foreign.type=BLE_GAP_EVENT_NOTIFY_RX;foreign.notify_rx.conn_handle=5;foreign.notify_rx.attr_handle=20;foreign.notify_rx.om=&wrong;test_gap(&foreign,test_gap_arg);m.process_rx();
    last_generation=r->connection_generation.load();
    if(cases[index].terminal){inject("22020802");inject("22020801");}
    inject(cases[index].hex);
    if(cases[index].terminal){inject("22020801");inject("22041a02081c");}
    stage=2;return;
   }
   if(stage==2){
    const auto &c=cases[index];if(c.terminal)assert(millis()-sent<2000 && "Specific response ignored / overwritten by local timeout");
    if(m.tesla->state.get("pairing_pending")->asBool()){assert(!r->authorized.load()&&!m.tesla->state.get("paired")->asBool());assert(r->pair_started_ms==sent);if(!c.terminal&&c.code==0&&duplicates<3&&millis()-sent>1000*(duplicates+1)){inject(c.hex);++duplicates;}return;}
    if(m.tesla->state.get("pair_outcome")->asString()!=(c.terminal?"vehicle_rejected":"local_expiry"))std::fprintf(stderr,"RESPONSE_OUTCOME code=%d outcome=%s error=%s elapsed=%u\n",c.code,m.tesla->state.get("pair_outcome")->asString().c_str(),m.tesla->state.get("last_error")->asString().c_str(),millis()-sent);
    assert(m.tesla->state.get("pair_outcome")->asString()==(c.terminal?"vehicle_rejected":"local_expiry"));
    if(c.terminal){assert(m.tesla->state.get("pair_response_code")->asUint()==unsigned(c.code));assert(m.tesla->state.get("last_error")->asString().find("VCSEC whitelist information=")==0);}
    else assert(millis()-sent>=60000&&millis()-sent<61000);
    assert(!r->authorized.load()&&!r->connected.load()&&!r->operation.retired()&&!m.tesla->runtime_owner.restart_required());
    assert(!m.tesla->state.get("paired")->asBool()&&LittleFS.files==retained&&ev.applied==0);
    const auto first_error=m.tesla->state.get("last_error")->asString();r->closing_link=true;m.post_event(TeslaVehicle::EventType::Error,"cleanup secondary error");r->closing_link=false;m.loop();assert(m.tesla->state.get("last_error")->asString()==first_error);
    // Unsolicited terminal after retirement cannot mutate the retained outcome.
    inject(c.hex);m.process_rx();m.loop();assert(m.tesla->state.get("last_error")->asString()==first_error);
    std::printf("PAIR_RESPONSE_CASE_PASS index=%u code=%d terminal=%d elapsed=%u epoch=%llu\n",index,c.code,c.terminal,millis()-sent,(unsigned long long)epoch);
    if(++index==cases.size()){stage=3;r->stop_requested=true;}else{stage=0;duplicates=0;}
   }
  };
  run_task(r->task_handle);on_delay={};on_task_delay={};test_submit={};assert(stage==3);m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();
  std::puts("PAIR_RESPONSE_PASS native adapter/queue/worker/main/API, repeat same runtime/key, no false authentication");
 }
