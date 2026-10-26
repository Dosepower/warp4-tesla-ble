// RX notification bursts while the REAL production worker waits on a returned
// but not yet acknowledged ATT WRITE. Path under test: worker task ->
// Vehicle::transmit -> TeslaVehicle::transport_write_bytes -> gatt_operation ->
// NimBLE adapter (ble_gattc_write_flat) ... NimBLE GAP NOTIFY_RX -> adapter ->
// TeslaVehicle::gatt_callback -> real FreeRTOS-shaped RX queue (capacity enforced
// by HostQueue) -> worker drain -> FrameReceiver -> Vehicle deferral/frame.
// Peer bytes come from the independent controller fixture (real crypto). The
// notification schedule is deterministic; it is not an RF/vehicle recording.
//
// Mode grammar: rx-<when>-<count>-<ack_delay_ms>[-pair]
//   when=sync  : all notifications delivered synchronously inside the WRITE
//                submission (host task preempting the worker at submit time)
//   when=yield : all notifications delivered within ONE worker OS yield
//   when=paced : one notification per worker OS yield while the WRITE is open
//   -pair      : burst during the final PRESENT_KEY fragment instead of the
//                first verification session-request (hello) fragment
// Special modes: rx-max-frame, rx-split-timer, rx-overload, rx-deferred-overflow,
//                rx-identity-change, rx-stale-mixed, rx-bad-only, rx-max-frame-sync,
//                rx-tiny-max-frame, rx-capacity-units (queue-level contract units)
// Optional fragmentation suffix (H1): -p<N> splits the concatenated burst stream
// into N-byte notification payloads, -f<N> splits every framed message
// separately (short tail per frame). N is a notification PAYLOAD length, not an
// ATT MTU. Default (no suffix): historical <=20-byte split.
static void rx_burst_flow(TeslaVehicle &m,const std::string &mode) {
 if(mode=="rx-capacity-units"){rx_capacity_units();return;}
 using namespace controller_fixture;
 Harness peer;IdentityLease key;assert(peer.keys.acquire(key));uint8_t pem[512]{};size_t n=0;assert(key.key()->export_pem(pem,sizeof(pem),n));
 TeslaBleStorageAdapter store;assert(store.select_profile(0,A));assert(tesla_clean::KeyStorage::committed(store.save_transaction("private_key",pem,n)));
 assert(tesla_clean::KeyStorage::committed(store.save_transaction("vehicle_vin",reinterpret_cast<const uint8_t *>("5YJ3E1EA7KF000001"),17)));
 const auto retained=LittleFS.files.at("/config/tesla_p00_112233445500_private_key");
 HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;explicit_scan(m,0);String error;assert(m.select_profile(0,false,error));auto*r=m.tesla->runtime_owner.get();

 // ---- parse mode
 std::string base=mode;size_t payload=0;bool per_frame=false;
 {const auto dash=base.rfind('-');
  if(dash!=std::string::npos&&base.size()>dash+2&&(base[dash+1]=='p'||base[dash+1]=='f')&&std::all_of(base.begin()+dash+2,base.end(),[](char c){return c>='0'&&c<='9';})){
   payload=std::stoul(base.substr(dash+2));per_frame=base[dash+1]=='f';base.resize(dash);assert(payload>=1&&payload<=512);}}
 const bool tiny_max=base=="rx-tiny-max-frame";if(tiny_max){payload=1;per_frame=false;}
 std::string when="yield";size_t count=12;uint32_t ack_delay=200;bool pair_target=false;
 const bool max_frame=base=="rx-max-frame"||base=="rx-max-frame-sync"||tiny_max,split_timer=base=="rx-split-timer",overload=base=="rx-overload",deferred_overflow=base=="rx-deferred-overflow";
 const bool identity_change=base=="rx-identity-change",stale_mixed=base=="rx-stale-mixed",bad_only=base=="rx-bad-only",late_approval=base=="rx-late-approval";
 if(max_frame){when=base=="rx-max-frame-sync"?"sync":"yield";ack_delay=200;}
 else if(overload){when="yield";count=0;ack_delay=200;}
 else if(deferred_overflow){when="yield";ack_delay=200;}
 else if(identity_change){when="paced";count=12;ack_delay=1000;}
 else if(stale_mixed){when="yield";count=12;ack_delay=200;}
 else if(bad_only){when="sync";count=12;ack_delay=200;}
 else if(late_approval){when="yield";count=12;ack_delay=200;}
 else if(split_timer){when="split";ack_delay=1500;}
 else {
  std::istringstream in(base.substr(3));std::string tok;std::vector<std::string> t;while(std::getline(in,tok,'-'))t.push_back(tok);
  assert(t.size()>=3);when=t[0];count=std::stoul(t[1]);ack_delay=std::stoul(t[2]);pair_target=t.size()>3&&t[3]=="pair";
  assert(when=="sync"||when=="yield"||when=="paced");
 }
 // Deliberate overload is derived from the CURRENT capacity (default split):
 // RX_QUEUE_DEPTH+6 notifications of 20 bytes, beyond the bounded intake.
 if(overload&&!payload)count=TeslaVehicle::RX_QUEUE_DEPTH+6;
 const bool expect_overflow=overload||tiny_max,expect_failure=overload||tiny_max||deferred_overflow||identity_change||bad_only;
 const bool regular=!max_frame&&!split_timer&&!overload&&!deferred_overflow;

 // ---- helpers
 auto framed=[](const std::vector<Frame>&frames){std::vector<uint8_t> all;for(const auto&f:frames){all.push_back(uint8_t(f.length>>8));all.push_back(uint8_t(f.length));all.insert(all.end(),f.data.begin(),f.data.begin()+f.length);}return all;};
 auto split=[](const std::vector<uint8_t>&all,size_t pieces){
  // ATT MTU 23 (production default): at most 20 payload bytes per notification.
  size_t size=std::min<size_t>(20,std::max<size_t>(1,(all.size()+pieces-1)/pieces));std::deque<std::vector<uint8_t>> out;
  for(size_t i=0;i<all.size();i+=size)out.emplace_back(all.begin()+i,all.begin()+std::min(all.size(),i+size));return out;};
 auto raw_frame=[](std::initializer_list<uint8_t> b){Frame f;f.length=b.size();std::copy(b.begin(),b.end(),f.data.begin());return f;};
 const Frame wait=raw_frame({0x22,0x02,0x08,0x01}); // official CommandStatus{operationStatus=WAIT}
 auto filler=[](size_t length){Frame f;f.length=length;std::fill(f.data.begin(),f.data.begin()+length,0);return f;}; // well framed, never decodes
 unsigned notifications=0;bool overflow_seen=false;
 auto notify=[&](const std::vector<uint8_t>&b,uint16_t conn=4,uint16_t attr=20){
  os_mbuf mb;mb.bytes=b;ble_gap_event e{};e.type=BLE_GAP_EVENT_NOTIFY_RX;e.notify_rx.conn_handle=conn;e.notify_rx.attr_handle=attr;e.notify_rx.om=&mb;e.notify_rx.indication=1;
  const bool before=r->connected.load();test_gap(&e,test_gap_arg);mb.bytes.clear();if(conn==4&&attr==20)++notifications;
  if(before&&!r->connected.load())overflow_seen=true;
 };

 // ---- peer / scheduler state (all hooks run on the worker thread)
 std::vector<uint8_t> wire;std::deque<std::vector<uint8_t>> burst,after;bool armed=!split_timer,burst_pending=false,paced=false,split_armed=false;
 size_t burst_size=0;WriteFn delayed=nullptr;void*delayed_arg=nullptr;uint16_t delayed_handle=0;uint32_t ack_at=0,write_started=0;
 unsigned hellos=0,statuses=0,status_counter=0,main_ticks=0,attempts=0;bool submitted=false,complete=false,finished=false,retry_started=false,identity_changed=false;
 uint32_t pair_at=0;std::vector<uint8_t> split_rest;bool split_rest_pending=false;uint64_t first_pair_command=0;
 auto deliver_burst=[&](){while(!burst.empty()){notify(burst.front());burst.pop_front();}};
 auto reply_status=[&](const Sent &s){
  ++status_counter;
  for(auto&p:split(framed({peer.response(s,{awake,sizeof(awake)},status_counter,0,true)}),1))after.push_back(p); // invalid first: must not authorize
  if(!bad_only)for(auto&p:split(framed({peer.response(s,{awake,sizeof(awake)},status_counter)}),1))after.push_back(p);
 };
 test_submit=[&](int stage){
  if(stage!=5||test_handle==21){nimble_auto_submit(stage);return;}
  wire.insert(wire.end(),test_write.begin(),test_write.end());bool done=false;Sent s;
  if(wire.size()>=2&&wire.size()==size_t(wire[0])*256+wire[1]+2){Frame f;f.length=wire.size()-2;memcpy(f.data.data(),wire.data()+2,f.length);wire.clear();s=decode(f);done=true;}
  peer.address=r->routing_address;
  if(split_timer&&split_armed&&!delayed){
   // F4: first fragment of a WAIT was consumed before this TX; the rest arrives 100ms into a 1.5s WRITE.
   split_armed=false;write_started=millis();delayed=test_written;delayed_arg=test_proc_arg;delayed_handle=test_handle;ack_at=millis()+ack_delay;split_rest_pending=true;
   if(done&&s.hello){++hellos;for(auto&p:split(framed({peer.hello(s)}),1))after.push_back(p);}
   return;
  }
  const bool late_target=late_approval&&done&&!s.hello&&!s.pair&&r->pair_verifying&&complete&&millis()-pair_at>=50000;
  const bool target=done&&armed&&!split_timer&&(late_approval?late_target:((pair_target&&s.pair)||(!pair_target&&s.hello&&r->pair_verifying)));
  if(done&&s.hello)++hellos;
  if(target){
   armed=false;std::vector<Frame> frames;
   if(pair_target)frames={wait,filler(200),wait};
   else if(max_frame)frames={filler(1024),wait,peer.hello(s)};
   else if(deferred_overflow)frames={wait,wait,wait,wait,wait,wait};
   else if(overload)frames={filler(1024),filler(1024),filler(1024)};
   else if(late_approval){++statuses;++status_counter;frames={wait,peer.response(s,{awake,sizeof(awake)},status_counter,0,true),peer.response(s,{awake,sizeof(awake)},status_counter)};}
   else frames={wait,peer.hello(s)};
   auto all=framed(frames);
   if(payload){
    burst.clear();
    if(per_frame){for(const auto&f:frames)for(auto&n:rx_split(framed({f}),payload))burst.push_back(n);}
    else for(auto&n:rx_split(all,payload))burst.push_back(n);
    size_t slots=0,shorter=0;for(auto&n:burst){slots+=rx_cost(n.size());shorter+=n.size()<RX_CONTRACT_FULL_MIN;}
    std::printf("RX_BURST_FRAGMENTATION mode=%s payload=%zu per_frame=%d frames=%zu stream_bytes=%zu notifications=%zu slots=%zu short=%zu\n",mode.c_str(),payload,per_frame,frames.size(),all.size(),burst.size(),slots,shorter);
    // Matrix bursts with payload >= 20 are inside the independent contract window.
    if(max_frame&&!tiny_max&&payload>=RX_CONTRACT_FULL_MIN)assert(all.size()<=RX_CONTRACT_BYTES&&shorter<=RX_CONTRACT_SHORT);
   } else {
    burst=split(all,overload?count:max_frame?(all.size()+19)/20:count);
    if(overload){assert(burst.size()>=count);burst.resize(count);}
   }
   burst_size=burst.size();
   if(stale_mixed){
    // Stale generation, foreign connection and foreign handle interleaved; none may consume capacity or frame state.
    std::deque<std::vector<uint8_t>> mixed;for(auto&p:burst){mixed.push_back(p);mixed.push_back({0xff,0xff,0xff});}burst=mixed;
   }
   write_started=millis();delayed=test_written;delayed_arg=test_proc_arg;delayed_handle=test_handle;ack_at=millis()+ack_delay;
   if(when=="sync")deliver_burst();else if(when=="yield")burst_pending=true;else paced=true;
   return;
  }
  if(done&&s.hello&&(!armed||late_approval))for(auto&p:split(framed({peer.hello(s)}),1))after.push_back(p);
  if(done&&!s.hello&&!s.pair&&r->pair_verifying&&!late_approval){++statuses;reply_status(s);}
  ble_gatt_error ok{};ble_gatt_attr a{test_handle};test_written(4,&ok,&a,test_proc_arg);
 };
 on_task_delay=[&](uint32_t ms){
  ++main_ticks;m.loop();
  if(delayed&&static_cast<int32_t>(millis()-ack_at)>=0){auto cb=delayed;delayed=nullptr;ble_gatt_error ok{};ble_gatt_attr attr{delayed_handle};cb(4,&ok,&attr,delayed_arg);}
  if(burst_pending){
   burst_pending=false;
   if(stale_mixed){
    const auto old=r->physical_generation.load()-1;bool real=true;
    for(auto&p:burst){
     if(real)notify(p);
     else {TeslaVehicle::gatt_callback({TeslaNimbleEvent::Rx,old,4,20,0,p.data(),p.size()});notify(p,5,20);notify(p,4,99);}
     real=!real;
    }
    burst.clear();
   } else deliver_burst();
  }
  if(paced&&!burst.empty()&&delayed){notify(burst.front());burst.pop_front();}
  if(identity_change&&paced&&burst.size()==burst_size/2&&!identity_changed){identity_changed=true;++ev.generation;ev.index=1;ev.mac=B;m.refresh_live_session();}
  if(split_rest_pending&&delayed&&millis()-write_started>=100){split_rest_pending=false;notify(split_rest);}
  if(split_timer&&!split_armed&&!split_rest_pending&&r->pair_verifying&&hellos==0&&static_cast<int32_t>(millis()-r->pair_next_ms)>=0&&split_rest.empty()){
   auto all=framed({wait});notify({all.begin(),all.begin()+3});split_rest.assign(all.begin()+3,all.end());split_armed=true;
  }
  if(ms==20&&!after.empty()&&!delayed){notify(after.front());after.pop_front();}
 };
 on_delay=[&]{
  if(!running_task||running_task->name!="tesla_ble"||r->profile_pending.load())return;
  if(!submitted){m.loop();m.tesla->profile_command.get("ev_index")->updateInt(0);error.clear();api.commands.at("tesla_vehicle/pair")(0,error);assert(error.empty());submitted=true;++attempts;return;}
  if(!complete&&r->pair_result.load()==int(tesla_clean::VehicleResultCode::PairRequestSent)){complete=true;pair_at=millis();if(!retry_started)first_pair_command=r->pair_command;}
  assert(millis()<300000&&"bounded test duration");
  if(overflow_seen&&!expect_overflow){
   std::fprintf(stderr,"RX_BURST_RED mode=%s overflow during outstanding WRITE: burst=%zu delivered=%u connected=%d write_open_ms=%u\n",mode.c_str(),burst_size,notifications,r->connected.load(),millis()-write_started);
   assert(false&&"legitimate RX burst during outstanding WRITE overflowed the RX queue and dropped the session");
  }
  if(!complete)return;
  if(regular&&burst_size&&!payload)assert(burst_size>8&&"burst must exceed the former 8-entry RX queue");
  assert(!r->stop_requested.load()&&!r->operation.retired()&&"bounded RX burst must never quarantine Bluetooth");
  if(r->authorized.load()){
   m.loop();m.publish_bluetooth_state();
   if(expect_failure&&!retry_started){std::fprintf(stderr,"RX_BURST mode=%s unexpectedly authorized\n",mode.c_str());assert(false);}
   assert(m.tesla->state.get("paired")->asBool()&&ev.applied==0&&!m.tesla->runtime_owner.restart_required());
   assert(LittleFS.files.at("/config/tesla_p00_112233445500_private_key")==retained);
   if(!retry_started){
    if(peer_open_calls!=1||r->pair_command!=first_pair_command||(!late_approval&&!m.tesla->state.get("pair_response_seen")->asBool())||(split_timer&&hellos>2))
     std::fprintf(stderr,"RX_BURST_DETAIL mode=%s opens=%d pair_command_changed=%d response_seen=%d hellos=%u statuses=%u elapsed_ms=%u\n",mode.c_str(),peer_open_calls,r->pair_command!=first_pair_command,m.tesla->state.get("pair_response_seen")->asBool(),hellos,statuses,millis()-pair_at);
    assert(peer_open_calls==1&&r->pair_command==first_pair_command&&"response handled in the same attempt and connection");
    // In rx-late-approval the WAIT and the authenticated approval share one burst;
    // authenticated success ends verification before the WAIT is published.
    if(!late_approval)assert(m.tesla->state.get("pair_response_seen")->asBool()&&"WAIT frame received during WRITE must be decoded in this attempt");
    assert(millis()-pair_at<60000);
    if(late_approval)assert(millis()-pair_at>=50000&&"late authenticated approval inside the 60s window");
    // Privacy-safe counters: no overflow, real queue pressure, worker-owned WRITE-wait service.
    assert(r->rx_overflows.load()==0&&r->rx_write_serviced>0);
    if(when!="paced"&&!split_timer)assert(r->rx_peak.load()>=burst_size&&r->rx_peak.load()<=TeslaVehicle::RX_QUEUE_DEPTH);
    // A 1.5s WRITE exceeds the unchanged 1s session-response timer (counted from
    // before TX), so one ordinary hello retry is expected; framing desync (RED:
    // response_seen=0, hellos=4) is not.
    if(split_timer)assert(hellos<=2&&"split frame across a 1.5s WRITE must not desynchronize framing");
   }
   std::printf("RX_BURST_PASS mode=%s burst_notifications=%zu delivered=%u ack_delay_ms=%u hellos=%u statuses=%u retry=%d elapsed_ms=%u rx_peak=%u rx_write_serviced=%u\n",mode.c_str(),burst_size,notifications,ack_delay,hellos,statuses,retry_started,millis()-pair_at,unsigned(r->rx_peak.load()),unsigned(r->rx_write_serviced));
   finished=true;r->stop_requested=true;return;
  }
  if(!r->pair_verifying&&!r->pair_sending&&!r->pair_status_active&&!m.tesla->state.get("pairing_pending")->asBool()&&!retry_started){
   m.loop();m.publish_bluetooth_state();
   const auto outcome=m.tesla->state.get("pair_outcome")->asString();
   if(!expect_failure){std::fprintf(stderr,"RX_BURST mode=%s attempt ended without approval outcome=%s error=%s overflow=%d\n",mode.c_str(),outcome.c_str(),m.tesla->state.get("last_error")->asString().c_str(),overflow_seen);assert(false);}
   if(expect_overflow)assert(overflow_seen&&"genuine overload beyond bounded intake must stay fail-safe");
   if(expect_overflow)assert(r->rx_overflows.load()>=1&&r->rx_peak.load()<=TeslaVehicle::RX_QUEUE_DEPTH);
   if(deferred_overflow)assert(r->vehicle&&r->vehicle->deferred_overflows()>=1&&r->rx_overflows.load()==0);
   assert(!r->authorized.load()&&!m.tesla->state.get("paired")->asBool()&&!m.tesla->runtime_owner.restart_required()&&m.bluetooth.active());
   assert(LittleFS.files.at("/config/tesla_p00_112233445500_private_key")==retained&&!LittleFS.files.count("/config/tesla_p00_112233445500_authorized")&&ev.applied==0);
   std::printf("RX_BURST_FAILSAFE mode=%s outcome=%s overflow=%d error=%s\n",mode.c_str(),outcome.c_str(),overflow_seen,m.tesla->state.get("last_error")->asString().c_str());
   if(bad_only){assert(millis()-pair_at>=60000&&outcome=="local_expiry");finished=true;r->stop_requested=true;return;}
   if(identity_change){finished=true;r->stop_requested=true;return;}
   // Explicit retry in the same runtime with the same key/assignment; no restart, rediscovery or deletion.
   retry_started=true;armed=false;burst.clear();after.clear();wire.clear();overflow_seen=false;
   if(!m.fresh_binding(0,A,m.find_profile(0)->get("vin")->asString().c_str(),m.find_profile(0)->get("ble_address")->asString().c_str(),0,m.find_profile(0)->get("discovery_revision")->asUint()))assert(false&&"binding must still be fresh");
   m.tesla->profile_command.get("ev_index")->updateInt(0);error.clear();api.commands.at("tesla_vehicle/pair")(0,error);
   if(!error.empty())std::fprintf(stderr,"RX_BURST retry refused: %s\n",error.c_str());assert(error.empty());++attempts;complete=false;return;
  }
  assert(millis()-pair_at<62000);
 };
 run_task(r->task_handle);on_delay={};on_task_delay={};test_submit=nimble_auto_submit;on_gatt_bytes={};assert(submitted&&finished&&main_ticks>20);
 if(expect_failure&&!bad_only&&!identity_change)assert(retry_started&&attempts==2);
 m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();test_submit={};
}
