// Interactive offline bridge: mounted production UI talks to one real worker.
 static void browser_bridge(TeslaVehicle&m) {
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;explicit_scan(m,0);seed(1);
  String error;TeslaVehicle::Runtime*r=nullptr;unsigned serial=0,frames=0,yields=0;bool waiting=false,active=false;std::vector<uint8_t>wire;
  on_gatt_bytes=[&](const uint8_t*p,size_t n){wire.insert(wire.end(),p,p+n);if(wire.size()>=2&&wire.size()==size_t(wire[0])*256+wire[1]+2){++frames;wire.clear();}};
  auto string=[](const String&s){std::cout<<'"';for(char c:s){if(c=='"'||c=='\\')std::cout<<'\\';if(c=='\n')std::cout<<"\\n";else std::cout<<c;}std::cout<<'"';};
  auto object=[&](Config*c,const std::vector<const char*>&texts,const std::vector<const char*>&numbers,const std::vector<const char*>&bools){
   std::cout<<'{';bool first=true;auto key=[&](const char*k){if(!first)std::cout<<',';first=false;std::cout<<'"'<<k<<"\":";};
   for(auto k:texts){key(k);string(c->get(k)->asString());}for(auto k:numbers){key(k);std::cout<<c->get(k)->asInt();}for(auto k:bools){key(k);std::cout<<(c->get(k)->asBool()?"true":"false");}std::cout<<'}';
  };
  auto emit=[&]{m.loop();m.publish_guidance();std::cout<<"BRIDGE {\"serial\":"<<++serial<<",\"epoch\":"<<(r?r->epoch:0)<<",\"clock_ms\":"<<millis()<<",\"pair_started_ms\":"<<(r?r->pair_started_ms:0)<<",\"frames\":"<<frames<<",\"yields\":"<<yields<<",\"error\":";string(error);std::cout<<",\"state\":";
   object(&m.tesla->state,{"key_presence","last_error","pair_outcome"},{"profile_ev_index","active_ev_index","pair_response_code","pair_response_outer","pair_response_operation"},{"running","stopping","connected","key_present","paired","pairing_pending","pair_request_sent","restart_required","action_busy","pair_response_seen","pair_response_whitelist"});
   std::cout<<",\"guidance\":{\"generation\":";string(m.tesla->guidance.get("generation")->asString());std::cout<<",\"revision\":"<<m.tesla->config_revision<<",\"request\":";string(m.tesla->guidance.get("request")->asString());std::cout<<",\"ready\":"<<(m.tesla->guidance.get("ready")->asBool()?"true":"false")<<",\"profiles\":[";
   auto rows=m.tesla->guidance.get("profiles");for(size_t i=0;i<rows->count();++i){if(i)std::cout<<',';object(rows->get(i),{"ev_mac","key_presence"},{"ev_index"},{"namespace_clean","binding_fresh","paired"});}std::cout<<"]},\"profiles\":{\"entries\":[";
   for(int i=0;i<2;++i){if(i)std::cout<<',';object(m.find_profile(i),{"ev_mac","vin","ble_address"},{"ev_index","ble_address_type","discovery_revision","poll_interval"},{"enable","wake_once"});}std::cout<<"]},\"discovery\":";
   m.publish_discovery();object(&m.tesla->discovery_state,{"status","error","searched_vin","ev_mac","generation"},{"revision","ev_index","result_age_ms","result_ttl_ms"},{"busy","truncated"});std::cout<<'}'<<std::endl;
  };
  std::deque<std::function<void()>> main_callbacks;unsigned queued_executed=0;
  auto main_pass=[&]{auto*worker=running_task;running_task=nullptr;while(!main_callbacks.empty()){auto f=std::move(main_callbacks.front());main_callbacks.pop_front();f();r=m.tesla->runtime_owner.get();++queued_executed;}m.loop();running_task=worker;};
  if(const char*mode=std::getenv("TESLA_REVIEW_SCHEDULE")){if(std::string(mode)=="aged"){TeslaBleDiscovery::Snapshot snap;assert(m.bluetooth.discovery_snapshot(snap));clock_ms.store(snap.completed_at_ms+119000);}}
  const bool connect_mixed=std::getenv("TESLA_REVIEW_SCHEDULE")&&std::string(std::getenv("TESLA_REVIEW_SCHEDULE"))=="connect-mixed";
  unsigned open_attempt=0;uint32_t connect_terminal=0,disconnect_terminal=0;
  unsigned response_attempt=0;
  std::vector<std::string> responses;
  if(const char *sequence=std::getenv("TESLA_PAIR_RESPONSE_SEQUENCE")){std::istringstream list(sequence);std::string hex;while(std::getline(list,hex,';'))responses.push_back(hex);}
  test_submit=[&](int stage){
   if(connect_mixed&&stage==1){++open_attempt;if(open_attempt==1||open_attempt==3)return;}
   if(connect_mixed&&stage==7){connect_terminal=millis()+150;return;}
   if(connect_mixed&&stage==6&&open_attempt==3){disconnect_terminal=millis()+170;return;}
   nimble_auto_submit(stage);
  };
  on_task_delay=[&](uint32_t){
   if(connect_terminal&&millis()>=connect_terminal){connect_terminal=0;ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect.status=open_attempt==3?0:13;e.connect.conn_handle=4;test_gap(&e,test_gap_arg);}
   if(disconnect_terminal&&millis()>=disconnect_terminal){disconnect_terminal=0;nimble_force_disconnect();}
   main_pass();
  };
  on_delay=[&]{
   if(r&&(!running_task||running_task->name!="tesla_ble"||r->profile_pending.load()))return;++yields;main_pass();
   if(m.tesla->state.get("pairing_pending")->asBool()){
    if(m.tesla->state.get("pair_request_sent")->asBool()&&!active){
     active=true;waiting=false;emit();std::this_thread::sleep_for(std::chrono::milliseconds(250));
     if(response_attempt<responses.size()){
      const auto hex=responses[response_attempt++];std::vector<uint8_t>payload;
      for(size_t i=0;i<hex.size();i+=2){unsigned b;assert(std::sscanf(hex.c_str()+i,"%2x",&b)==1);payload.push_back(b);}
      std::vector<uint8_t>wire{uint8_t(payload.size()>>8),uint8_t(payload.size())};wire.insert(wire.end(),payload.begin(),payload.end());
      for(size_t i=0;i<wire.size();i+=3){os_mbuf mb;mb.bytes.assign(wire.begin()+i,wire.begin()+std::min(wire.size(),i+3));ble_gap_event e{};e.type=BLE_GAP_EVENT_NOTIFY_RX;e.notify_rx.conn_handle=4;e.notify_rx.attr_handle=20;e.notify_rx.om=&mb;test_gap(&e,test_gap_arg);}
     }
    }return;
   }
   if(waiting||active||serial==0){waiting=false;active=false;emit();}
   std::string line;if(!std::getline(std::cin,line)||line=="quit"){r->stop_requested=true;return;}
   std::istringstream in(line);std::string phase,request,generation,mac,vin,address;uint32_t revision,type,discovery,poll;int index,enable,wake;
   in>>phase>>request>>revision>>generation>>index>>mac>>enable>>vin>>address>>type>>discovery>>poll>>wake;assert(in);
   main_callbacks.push_back([&,phase,request,generation,mac,vin,address,revision,type,discovery,poll,index,enable,wake]{
   if(phase=="scan") {
    auto &s=m.tesla->scan_command;s.get("ev_index")->updateInt(index);s.get("ev_mac")->updateString(mac);s.get("vin")->updateString(vin);error.clear();api.commands.at("tesla_vehicle/scan")(0,error);assert(error.empty());
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!test_scanning&&std::chrono::steady_clock::now()<end)std::this_thread::yield();assert(test_scanning);auto yielded=host_bt_yields.load();while(host_bt_yields.load()==yielded&&std::chrono::steady_clock::now()<end)std::this_thread::yield();
    char name[19]{};assert(tesla_ble_vehicle_name(vin.c_str(),name));uint8_t data[20]={19,9};memcpy(data+2,name,18);ble_gap_event e{};e.type=BLE_GAP_EVENT_DISC;e.disc.addr.val[5]=1;for(int i=0;i<5;++i)e.disc.addr.val[i]=6-i;e.disc.addr.type=type;e.disc.data=data;e.disc.length_data=20;test_scan(&e,test_scan_arg);e.type=BLE_GAP_EVENT_DISC_COMPLETE;test_scanning=false;test_scan(&e,test_scan_arg);
    TeslaBleDiscovery::Snapshot snap;while(std::chrono::steady_clock::now()<end){if(m.bluetooth.discovery_snapshot(snap)&&snap.status==TeslaBleDiscovery::Status::Complete)break;std::this_thread::yield();}assert(snap.count==1&&snap.status==TeslaBleDiscovery::Status::Complete);waiting=true;return;
   }
   if(phase=="save") {
    auto *p=m.find_profile(index);p->get("discovery_revision")->updateUint(discovery);p->get("ble_address")->updateString(address);p->get("ble_address_type")->updateUint(type);
    error=m.validate_profiles(m.tesla->profiles,ConfigSource::API);if(error.empty())api.writeConfig("tesla_vehicle/profiles",&m.tesla->profiles);waiting=true;return;
   }
   auto&c=m.tesla->guided_command;auto*p=c.get("profile");p->get("ev_index")->updateInt(index);p->get("ev_mac")->updateString(mac);p->get("vin")->updateString(vin);p->get("ble_address")->updateString(address);p->get("enable")->updateBool(enable);p->get("wake_once")->updateBool(wake);
   p->get("ble_address_type")->updateUint(type);p->get("discovery_revision")->updateUint(discovery);p->get("poll_interval")->updateUint(poll);
   c.get("phase")->updateString(phase);c.get("request")->updateString(request);c.get("revision")->updateUint(revision);c.get("generation")->updateString(generation);
   error.clear();api.commands.at("tesla_vehicle/guided")(0,error);waiting=true;
   });
  };
  while(!r){on_delay();main_pass();}run_task(r->task_handle);on_delay={};on_task_delay={};m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();assert(ev.applied==0);std::cerr<<"QUEUED_MAIN_CALLBACKS "<<queued_executed<<std::endl;
 }
