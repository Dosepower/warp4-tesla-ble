#include "host/module_platform.h"
#include "../../src/modules/tesla_vehicle/tesla_vehicle.cpp"
#include "host/nimble_fixture.h"
#include "cleanroom/controller_fixture.hpp"
#ifdef TESLA_NIMBLE_REAL_DRAIN_HEADER
#include TESLA_NIMBLE_REAL_DRAIN_HEADER
#else
static void sdk_track_pending_write() {}
#endif
#include <cstdio>
#include <iostream>
#include <sstream>
struct TeslaBleModuleTest {
 static constexpr uint64_t A=0x112233445500ULL,B=0x112233445501ULL;
 static void entry(Config *p,int index) {
  p->get("ev_index")->updateInt(index);p->get("ev_mac")->updateString(index?"11:22:33:44:55:01":"11:22:33:44:55:00");
  p->get("vin")->updateString(index?"5YJ3E1EA7KF000002":"5YJ3E1EA7KF000001");
  p->get("ble_address")->updateString(index?"02:02:03:04:05:06":"01:02:03:04:05:06");p->get("enable")->updateBool(true);
 }
 static void seed(int index) {
  TeslaBleStorageAdapter store;assert(store.select_profile(index,index?B:A));
  tesla_clean::IdentityStore key;assert(store.initialize_identity(key,{TeslaBleTransportAdapter::random,nullptr}));
  auto mac=encode_vehicle_mac(index?B:A);const uint8_t auth=1;
  const char *vin=index?"5YJ3E1EA7KF000002":"5YJ3E1EA7KF000001";
  assert(tesla_clean::KeyStorage::committed(store.save_transaction("warp_ev_mac",mac.data(),mac.size())));
  assert(tesla_clean::KeyStorage::committed(store.save_transaction("vehicle_vin",reinterpret_cast<const uint8_t*>(vin),17)));
  assert(tesla_clean::KeyStorage::committed(store.save_transaction("authorized",&auth,1)));
  uint8_t transport[26]{};assert(profile_transport_record(vin,index?"02:02:03:04:05:06":"01:02:03:04:05:06",0,transport));
  assert(tesla_clean::KeyStorage::committed(store.save_transaction("ble_binding",transport,sizeof(transport))));
  const uint8_t bytes[]={uint8_t(index+10),uint8_t(index+20)};
  for(auto name:{"session_vcsec","session_infotainment"})assert(tesla_clean::KeyStorage::committed(store.save_transaction(name,bytes,2)));
 }
 static void soc(TeslaVehicle &m,float value) {
  auto *r=m.tesla->runtime_owner.get();r->session.connect();
  auto req=r->session.arm(r->epoch,{ev.generation,ev.index,ev.mac},ev.index);assert(req.request);
  m.post_event(TeslaVehicle::EventType::Soc,nullptr,value,req,true,true);m.process_module_events();
 }
 static void explicit_scan(TeslaVehicle&m,int index){auto*p=m.find_profile(index);p->get("discovery_revision")->updateUint(nimble_vin_scan(m.bluetooth,p->get("vin")->asString().c_str(),index,index?B:A,index?2:1,0));}
 static void native_roundtrip(TeslaVehicle&m){
 auto&r=*m.tesla->runtime_owner.get();m.disconnect_internal(false);auto gen=r.connection_generation.load();r.physical_generation=gen;auto*c=m.bluetooth.central();assert(c&&c->open(r.remote_address,r.remote_address_type,gen));nimble_complete_setup();assert(c->discover());nimble_discover_all();assert(c->subscribe());ble_gatt_error ok{};ble_gatt_attr attr{21};test_written(4,&ok,&attr,test_proc_arg);
 assert(r.session.connect(gen,[&]{r.write_handle=c->tx();r.notify_handle=c->rx();r.connected=true;}));assert(r.vehicle&&r.vehicle->open(gen));
 uint8_t raw[]={0,3,8,0,0};os_mbuf mb{{raw,raw+sizeof(raw)}};ble_gap_event e{};e.type=BLE_GAP_EVENT_NOTIFY_RX;e.notify_rx.conn_handle=4;e.notify_rx.attr_handle=20;e.notify_rx.om=&mb;test_gap(&e,test_gap_arg);mb.bytes.clear();TeslaVehicle::RxFragment fragment{};assert(xQueueReceive(r.queues.rx(),&fragment,0)==pdTRUE);assert(fragment.length==sizeof(raw)&&fragment.connection_generation==gen&&memcmp(fragment.data,raw,sizeof(raw))==0);
 // Delayed prior-generation callbacks cannot mutate the new physical link/queue.
 auto old=gen-1;TeslaVehicle::gatt_callback({TeslaNimbleEvent::Disconnected,old,4,0,0,nullptr,0});TeslaVehicle::gatt_callback({TeslaNimbleEvent::Rx,old,4,20,0,raw,sizeof(raw)});assert(r.connected.load()&&xQueueReceive(r.queues.rx(),&fragment,0)!=pdTRUE);
 e={};e.type=BLE_GAP_EVENT_ENC_CHANGE;e.enc_change.conn_handle=4;e.enc_change.status=23;test_gap(&e,test_gap_arg);assert(!r.connected.load()&&r.connection_generation.load()!=gen);nimble_force_disconnect();m.process_disconnect();assert(!r.connected.load());
 }
 static void pairing(TeslaVehicle &m,bool generating,bool delayed=false,const std::string &scenario="") {
  using namespace controller_fixture;
  Harness peer;
  if(!generating) {
   IdentityLease key;assert(peer.keys.acquire(key));uint8_t pem[512]{};size_t n=0;assert(key.key()->export_pem(pem,512,n));
   TeslaBleStorageAdapter store;assert(store.select_profile(0,A));assert(tesla_clean::KeyStorage::committed(store.save_transaction("private_key",pem,n)));
   assert(tesla_clean::KeyStorage::committed(store.save_transaction("vehicle_vin",reinterpret_cast<const uint8_t *>("5YJ3E1EA7KF000001"),17)));
  }
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;String error;
  m.find_profile(0)->get("ble_address_type")->updateUint(generating?1:0);
  m.find_profile(0)->get("discovery_revision")->updateUint(nimble_vin_scan(m.bluetooth,"5YJ3E1EA7KF000001",0,A,1,generating?1:0));
  assert(m.select_profile(0,false,error));auto *r=m.tesla->runtime_owner.get();
  std::vector<uint8_t> pending;std::vector<Sent> sent;
  on_gatt_bytes=[&](const uint8_t *bytes,size_t size) {
   const bool pairing_frame=sent.empty();
   if(scenario=="partial-pair") {m.process_module_events();assert(m.tesla->state.get("connected")->asBool());}
   pending.insert(pending.end(),bytes,bytes+size);if(pending.size()<2)return;
   size_t n=size_t(pending[0])*256+pending[1];assert(pending.size()<=n+2);
   if(pending.size()==n+2){Frame frame;frame.length=n;memcpy(frame.data.data(),pending.data()+2,n);sent.push_back(decode(frame));pending.clear();}
   // Deterministic ATT ACK latency, not an RF/vehicle recording. Production
   // framing+GATT+Vehicle must permit a valid multi-fragment request >500ms.
   if((delayed||scenario=="partial-pair") && pairing_frame) clock_ms.fetch_add(scenario=="partial-pair"?4000:120);
  };
  test_submit=nimble_auto_submit;
  const auto inject=[&](const Frame &frame) {
   std::vector<uint8_t> wire{uint8_t(frame.length>>8),uint8_t(frame.length)};wire.insert(wire.end(),frame.data.begin(),frame.data.begin()+frame.length);
   for(size_t at=0;at<wire.size();at+=7){os_mbuf mb;const auto n=std::min<size_t>(7,wire.size()-at);mb.bytes.assign(wire.begin()+at,wire.begin()+at+n);ble_gap_event e{};e.type=BLE_GAP_EVENT_NOTIFY_RX;e.notify_rx.conn_handle=4;e.notify_rx.attr_handle=20;e.notify_rx.om=&mb;e.notify_rx.indication=(host_rx_properties&ESP_GATT_CHAR_PROP_BIT_INDICATE);test_gap(&e,test_gap_arg);mb.bytes.clear();m.process_rx();}

  };
  bool tested=false;
  on_delay=[&] {
   if(tested || r->profile_pending.load())return;tested=true;m.loop();m.publish_bluetooth_state();
   assert(peer_open_calls==0 && !r->authorized.load());
   if(generating)assert(!LittleFS.files.count("/config/tesla_p00_112233445500_private_key"));
   m.tesla->profile_command.get("ev_index")->updateInt(0);error.clear();api.commands.at("tesla_vehicle/pair")(0,error);assert(error.empty());
   if(scenario=="connect-timeout") {
    const auto start=millis();bool callback=false;
    on_task_delay=[&](uint32_t) {
     if(!callback && millis()-start>=uint32_t(test_connect_timeout+50)) {
      callback=true;ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect.status=13;test_gap(&e,test_gap_arg);
     }
    };
    test_submit={};m.process_commands();on_task_delay={};m.process_module_events();
    assert(callback && !r->operation.retired() && !r->stop_requested.load());
    assert(!r->connected.load() && !m.tesla->state.get("pairing_pending")->asBool());
    assert(!m.tesla->state.get("restart_required")->asBool() && m.bluetooth.active());
    test_submit=nimble_auto_submit;r->stop_requested=true;return;
   }
   m.process_commands();r->vehicle->tick();m.process_module_events();
   if(scenario=="partial-pair") {
    m.publish_bluetooth_state();assert(sent.empty() && !r->connected.load() && !r->stop_requested.load());
    assert(!m.tesla->state.get("connected")->asBool() && "API must converge after local stream retirement");
    assert(m.tesla->state.get("pair_tx_acked_bytes")->asUint()>0 && !m.tesla->state.get("pair_tx_complete")->asBool());
    assert(m.tesla->state.get("pair_tx_acked_bytes")->asUint()<m.tesla->state.get("pair_tx_total_bytes")->asUint());
    assert(!r->authorized.load() && !m.tesla->state.get("pairing_pending")->asBool());
    const auto attempted=r->pair_tx_attempted.load();clock_ms.fetch_add(1000);r->vehicle->tick();assert(r->pair_tx_attempted.load()==attempted); // no framed retry
    r->stop_requested=true;return;
   }
   assert(!sent.empty() && sent.back().pair);
   assert(peer_open_calls==1 && peer_open_type==(generating?1:0));
   assert(host_cccd_value==((host_rx_properties&ESP_GATT_CHAR_PROP_BIT_INDICATE)?2u:1u));
   if(delayed) { std::printf("DELAYED_ACK elapsed>=600ms complete_pair_frames=%zu api_connected=%d actual_connected=%d pending=%d error=%s\n",sent.size(),m.tesla->state.get("connected")->asBool(),r->connected.load(),m.tesla->state.get("pairing_pending")->asBool(),m.tesla->state.get("last_error")->asString().c_str());std::fflush(stdout); }
   assert(m.tesla->state.get("pairing_pending")->asBool() && !r->authorized.load());
   assert(LittleFS.files.count("/config/tesla_p00_112233445500_private_key"));
   assert(!LittleFS.files.count("/config/tesla_p00_112233445500_authorized"));
   m.publish_bluetooth_state();
   assert(m.tesla->state.get("pair_tx_complete")->asBool());
   assert(m.tesla->state.get("pair_tx_acked_bytes")->asUint()==m.tesla->state.get("pair_tx_total_bytes")->asUint());
   assert(m.tesla->state.get("pair_tx_acked_bytes")->asUint()>20);
   assert(m.tesla->state.get("pair_result")->asInt()==int(tesla_clean::VehicleResultCode::PairRequestSent));
   assert(!m.tesla->state.get("paired")->asBool());
   if(scenario=="late-response") {
    const auto key=LittleFS.files.at("/config/tesla_p00_112233445500_private_key");
    peer.address=r->routing_address;clock_ms.fetch_add(1000);m.process_pair_verification();r->vehicle->tick();assert(sent.back().hello);
    inject(peer.hello(sent.back()));r->vehicle->tick();const auto old_status=sent.back();assert(!old_status.hello && !old_status.pair);
    const Frame late=peer.response(old_status,{awake,sizeof(awake)});
    clock_ms.fetch_add(60000);m.process_pair_verification();m.process_module_events();assert(!r->authorized.load() && !r->connected.load());
    inject(late);m.process_module_events();assert(!r->authorized.load());
    explicit_scan(m,0);error.clear();assert(m.select_profile(0,false,error));m.apply_profile();m.publish_profile();m.process_module_events();
    error.clear();api.commands.at("tesla_vehicle/pair")(0,error);assert(error.empty());m.process_commands();r->vehicle->tick();m.process_module_events();
    peer.address=r->routing_address;clock_ms.fetch_add(1000);m.process_pair_verification();r->vehicle->tick();
    if(sent.back().hello){inject(peer.hello(sent.back()));r->vehicle->tick();}
    const auto fresh=sent.back();assert(!fresh.hello && !fresh.pair && fresh.uuid!=old_status.uuid);
    inject(late);m.process_module_events();assert(!r->authorized.load() && !m.tesla->state.get("paired")->asBool());
    inject(peer.response(fresh,{awake,sizeof(awake)}));m.process_module_events();assert(r->authorized.load() && m.tesla->state.get("paired")->asBool());
    assert(LittleFS.files.at("/config/tesla_p00_112233445500_private_key")==key && ev.applied==0);r->stop_requested=true;return;
   }
   if(scenario=="no-card"||scenario=="decline"||scenario=="link-drop"||scenario=="profile-switch") {
    const auto material=LittleFS.files;const auto generation=r->physical_generation.load();
    error.clear();api.commands.at("tesla_vehicle/pair")(0,error);assert(!error.empty()); // duplicate consent
    if(scenario=="decline") {Frame denial;const uint8_t raw[]={0x22,0x06,0x08,0x02,0x1a,0x02,0x08,0x01};denial.length=sizeof(raw);memcpy(denial.data.data(),raw,sizeof(raw));inject(denial);assert(!r->authorized.load());}
    if(scenario=="link-drop") {nimble_force_disconnect();m.process_disconnect();}
    else if(scenario=="profile-switch") {++ev.generation;ev.index=1;ev.mac=B;m.refresh_live_session();m.update_active_vehicle();}
    else {clock_ms.fetch_add(60000);m.process_pair_verification();}
    m.process_module_events();m.publish_bluetooth_state();
    assert(!r->connected.load() && !r->authorized.load() && !m.tesla->state.get("paired")->asBool());
    assert(!m.tesla->state.get("pairing_pending")->asBool() && !r->stop_requested.load() && !r->operation.retired());
    assert(m.bluetooth.active() && !m.tesla->state.get("restart_required")->asBool());
    assert(LittleFS.files==material && r->vehicle->queued()==0);
    const auto opens=peer_open_calls;
    clock_ms.fetch_add(1000);m.process_commands();m.process_pair_verification();r->vehicle->tick();assert(peer_open_calls==opens);
    if(scenario!="profile-switch") {
     // Fresh explicit consent only: reuse retained key and reconnect exactly once.
     explicit_scan(m,0); // refresh same bound discovery; no automatic radio.
     error.clear();assert(m.select_profile(0,false,error));m.apply_profile();m.publish_profile();m.process_module_events();
     error.clear();api.commands.at("tesla_vehicle/pair")(0,error);assert(error.empty());
     m.process_commands();r->vehicle->tick();m.process_module_events();assert(r->connected.load() && peer_open_calls==opens+1);
     TeslaVehicle::gatt_callback({TeslaNimbleEvent::Disconnected,generation,4,0,0,nullptr,0});
     const uint8_t stale[]={0xff,0xff};TeslaVehicle::gatt_callback({TeslaNimbleEvent::Rx,generation,4,20,0,stale,sizeof(stale)});
     assert(r->connected.load() && !r->authorized.load());
     assert(LittleFS.files==material); // no key recreation/authorization on reconnect
    }
    r->stop_requested=true;return;
   }
   if(!generating) {
    // A predecessor-generation fragment must not poison current framing,
    // including the indication path. If consumed, this oversized frame fails.
    assert(r->physical_generation.load()>0);TeslaVehicle::RxFragment stale;
    stale.connection_generation=r->physical_generation.load()-1;stale.length=2;stale.data[0]=0xff;stale.data[1]=0xff;
    assert(xQueueSend(r->queues.rx(),&stale,0)==pdTRUE);m.process_rx();assert(r->connected.load());
    peer.address=r->routing_address;clock_ms.fetch_add(1000);m.process_pair_verification();r->vehicle->tick();assert(sent.back().hello);
    inject(peer.hello(sent.back()));r->vehicle->tick();assert(!sent.back().hello);
    auto status=sent.back();inject(peer.response(status,{awake,sizeof(awake)},1,0,true));m.process_module_events();
    assert(!r->authorized.load() && !LittleFS.files.count("/config/tesla_p00_112233445500_authorized"));
    inject(peer.response(status,{awake,sizeof(awake)}));m.process_module_events();
    assert(r->authorized.load() && m.tesla->state.get("paired")->asBool() && ev.applied==0);
    assert(LittleFS.files.at("/config/tesla_p00_112233445500_authorized")==std::vector<uint8_t>{1});
    const auto original=LittleFS.files;const auto opens=peer_open_calls;
    ++ev.generation;ev.index=1;ev.mac=B;m.loop();explicit_scan(m,1);error.clear();assert(m.select_profile(1,false,error));m.update_active_vehicle();m.apply_profile();m.publish_profile();
    assert(!r->authorized.load());
    const auto after_B=LittleFS.files;
    ++ev.generation;ev.index=0;ev.mac=A;m.loop();explicit_scan(m,0);error.clear();assert(m.select_profile(0,false,error));m.apply_profile();m.publish_profile();m.process_module_events();
    assert(r->authorized.load() && m.tesla->state.get("paired")->asBool() && LittleFS.files==after_B && peer_open_calls==opens);
    for(const auto &item:original)assert(LittleFS.files.at(item.first)==item.second);
    error.clear();api.commands.at("tesla_vehicle/pair")(0,error);assert(!error.empty());
   }
   r->stop_requested.store(true);
  };
  run_task(r->task_handle);on_delay={};assert(tested);m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();
  on_gatt_bytes={};test_submit={};
  std::printf("PROFILE_PAIRING %s PASS explicit first pairing, durable key, no approval from unauthenticated response, profile auth persistence\n",generating?"generate":"authenticated");
 }
 #ifdef TESLA_NIMBLE_REAL_DRAIN_HEADER
 #include "host/cancellation_drain_contracts.inc"
 #endif
 static void approval_flow(TeslaVehicle &m,const std::string &mode) {
  using namespace controller_fixture;
  Harness peer;IdentityLease key;assert(peer.keys.acquire(key));uint8_t pem[512]{};size_t n=0;assert(key.key()->export_pem(pem,sizeof(pem),n));
  TeslaBleStorageAdapter store;assert(store.select_profile(0,A));assert(tesla_clean::KeyStorage::committed(store.save_transaction("private_key",pem,n)));
  assert(tesla_clean::KeyStorage::committed(store.save_transaction("vehicle_vin",reinterpret_cast<const uint8_t *>("5YJ3E1EA7KF000001"),17)));
  const auto retained=LittleFS.files.at("/config/tesla_p00_112233445500_private_key");
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;explicit_scan(m,0);if(mode=="flow-aged-approval-59s"){TeslaBleDiscovery::Snapshot snap;assert(m.bluetooth.discovery_snapshot(snap));clock_ms.store(snap.completed_at_ms+119000);}String error;assert(m.select_profile(0,false,error));auto*r=m.tesla->runtime_owner.get();
  bool submitted=false,complete=false,finished=false,missing=false;uint32_t pair_at=0,ack_at=0;unsigned main_ticks=0,frames=0;std::vector<uint8_t> wire;
  WriteFn delayed_ack=nullptr;void*delayed_arg=nullptr;uint16_t delayed_handle=0;bool late_received=false;
  const bool quarantine=mode=="flow-missing-terminal"||mode=="flow-blocked-submit"||mode=="flow-late-ack-no-main";
  std::vector<Frame> replies;std::vector<uint8_t> pending_reply;size_t reply_at=0;bool invalid_seen=false;uint32_t status_counter=0;
  on_gatt_bytes=[&](const uint8_t*p,size_t count){
   wire.insert(wire.end(),p,p+count);
   if(wire.size()<2||wire.size()!=size_t(wire[0])*256+wire[1]+2)return;
   ++frames;Frame f;f.length=wire.size()-2;memcpy(f.data.data(),wire.data()+2,f.length);wire.clear();
   if(((mode=="flow-approval-50s"||mode=="flow-approval-59s"||mode=="flow-aged-approval-59s")||mode=="flow-denied-50s")&&complete&&millis()-pair_at>=(mode.find("59s")!=std::string::npos?59000U:50000U)){
    peer.address=r->routing_address;const auto sent=decode(f);
    if(sent.hello)replies.push_back(peer.hello(sent,mode=="flow-denied-50s"));
    else if(!sent.pair){replies.push_back(peer.response(sent,{awake,sizeof(awake)},++status_counter,0,true));replies.push_back(peer.response(sent,{awake,sizeof(awake)},status_counter));}
   }
  };
  test_submit=[&](int stage){
   if(stage==6 && (mode=="flow-missing-terminal"||mode=="flow-late-ack-no-main"))return;
   if(stage==5 && test_handle!=21 && complete && mode=="flow-late-ack-no-main"){
    delayed_ack=test_written;delayed_arg=test_proc_arg;delayed_handle=test_handle;ack_at=millis()+7500;return;
   }
   if(stage==5 && test_handle!=21 && complete && mode=="flow-blocked-submit"){
    clock_ms.fetch_add(10010);m.loop();assert(m.tesla->runtime_owner.get()==r && !r->worker_stopped.load() && !r->task_handle->deleted);return;
   }
   if(stage==5 && test_handle!=21 && mode=="flow-delayed-acks"){
    on_gatt_bytes(test_write.data(),test_write.size());delayed_ack=test_written;delayed_arg=test_proc_arg;delayed_handle=test_handle;ack_at=millis()+1200;return;
   }
   if(stage==5 && test_handle!=21 && complete && mode=="flow-link-drop-during-write"){nimble_force_disconnect();return;}
   if(stage==5 && test_handle!=21 && complete && (mode=="flow-missing-ack"||mode=="flow-missing-terminal")){missing=true;sdk_track_pending_write();return;}
   nimble_auto_submit(stage);
  };
  // Each OS yield dispatches main while the actual production worker remains
  // inside its current operation. No helper/manual process_commands/tick loop.
  on_task_delay=[&](uint32_t ms){
   if(delayed_ack && static_cast<int32_t>(millis()-ack_at)>=0){auto cb=delayed_ack;delayed_ack=nullptr;ble_gatt_error ok{};ble_gatt_attr attr{delayed_handle};cb(4,&ok,&attr,delayed_arg);late_received=true;}
   if(mode=="flow-late-ack-no-main"&&complete&&ms!=20)return;
   if(mode=="flow-late-ack-no-main"&&late_received)assert(r->operation.retired() && "late WRITE ACK cannot substitute for missing terminal callback even without main sampling");
   ++main_ticks;const auto before=std::chrono::steady_clock::now();m.loop();assert(std::chrono::steady_clock::now()-before<std::chrono::milliseconds(100));
   if(ms==20){
    if(pending_reply.empty()&&!replies.empty()){
     const Frame f=replies.front();replies.erase(replies.begin());
     pending_reply={uint8_t(f.length>>8),uint8_t(f.length)};pending_reply.insert(pending_reply.end(),f.data.begin(),f.data.begin()+f.length);reply_at=0;
     if(status_counter&&!invalid_seen){invalid_seen=true;assert(!r->authorized.load());}
    }
    if(!pending_reply.empty()){
     os_mbuf mb;const auto n=std::min<size_t>(20,pending_reply.size()-reply_at);mb.bytes.assign(pending_reply.begin()+reply_at,pending_reply.begin()+reply_at+n);ble_gap_event e{};e.type=BLE_GAP_EVENT_NOTIFY_RX;e.notify_rx.conn_handle=4;e.notify_rx.attr_handle=20;e.notify_rx.om=&mb;e.notify_rx.indication=1;test_gap(&e,test_gap_arg);reply_at+=n;
     if(reply_at==pending_reply.size())pending_reply.clear();
    }
   }
  };
  on_delay=[&]{
   if(!running_task || running_task->name!="tesla_ble" || r->profile_pending.load())return;
   if(!submitted){m.loop();m.tesla->profile_command.get("ev_index")->updateInt(0);api.commands.at("tesla_vehicle/pair")(0,error);assert(error.empty());submitted=true;return;}
   if(!complete && r->pair_result.load()==int(tesla_clean::VehicleResultCode::PairRequestSent)){
    complete=true;pair_at=millis();assert(r->pair_tx_total.load()==88 && r->pair_tx_acked.load()==88 && frames==1);assert(!r->authorized.load());
   }
   if(!complete)return;
   if(r->stop_requested.load()){
    if(quarantine){
     m.loop();
     assert(m.tesla->runtime_owner.get()==r && m.tesla->runtime_owner.restart_required());
     assert(m.tesla->state.get("timeout_cause")->asString()=="gatt_operation");
     assert(m.tesla->state.get("timeout_operation")->asUint()==unsigned(TeslaVehicle::Write));
     assert(m.tesla->state.get("timeout_elapsed_ms")->asUint()>10000);
     assert(!r->authorized.load() && !m.tesla->state.get("paired")->asBool());finished=true;return;
    }
    std::printf("APPROVAL_FLOW premature_stop after_pair_ms=%u missing_ack=%d frames=%u main_ticks=%u error=%s\n",millis()-pair_at,missing,frames,main_ticks,m.tesla->state.get("last_error")->asString().c_str());std::fflush(stdout);
    assert(!r->stop_requested.load() && "normal no-approval/nonresponsive peer must drain link, not permanently quarantine Bluetooth");
   }
   if((mode=="flow-approval-50s"||mode=="flow-approval-59s"||mode=="flow-aged-approval-59s")&&r->authorized.load()){
    assert(invalid_seen&&millis()-pair_at>=50000&&millis()-pair_at<60000);
    m.loop();assert(m.tesla->state.get("paired")->asBool()&&ev.applied==0&&!m.tesla->runtime_owner.restart_required());
    assert(LittleFS.files.at("/config/tesla_p00_112233445500_private_key")==retained);
    finished=true;r->stop_requested=true;return;
   }
   if(!r->pair_verifying && !r->pair_sending && !r->pair_status_active && !m.tesla->state.get("pairing_pending")->asBool()){
    if((mode=="flow-approval-50s"||mode=="flow-approval-59s"||mode=="flow-aged-approval-59s"))std::fprintf(stderr,"AUTH_FLOW frames=%u status=%u invalid=%d replies=%zu elapsed=%u error=%s\n",frames,status_counter,invalid_seen,replies.size(),millis()-pair_at,m.tesla->state.get("last_error")->asString().c_str());
    assert(!(mode=="flow-approval-50s"||mode=="flow-approval-59s"||mode=="flow-aged-approval-59s") && "valid late authenticated response must complete pairing");
    assert(millis()-pair_at<=61000 && !r->authorized.load() && !m.tesla->state.get("paired")->asBool());assert(!r->operation.retired() && !m.tesla->runtime_owner.restart_required() && m.bluetooth.active());
    assert(LittleFS.files.at("/config/tesla_p00_112233445500_private_key")==retained && !LittleFS.files.count("/config/tesla_p00_112233445500_authorized") && ev.applied==0);
    if(mode=="flow-no-approval")assert(millis()-pair_at>=60000 && frames>3);
    finished=true;r->stop_requested=true;
   }
   assert(millis()-pair_at<62000);
  };
  run_task(r->task_handle);on_delay={};on_task_delay={};test_submit=nimble_auto_submit;on_gatt_bytes={};assert(submitted&&complete&&finished&&main_ticks>40);
  // Quarantine was asserted while ownership was unresolved. Only now deliver
  // the synthetic terminal callback so the test-owned lifecycle can join.
  m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();test_submit={};std::puts("APPROVAL_FLOW PASS actual worker/main/queues/adapter/crypto full bounded no-approval flow; key retained, no false pairing/SoC");
 }
 #include "host/repeated_pair_contracts.inc"
 #include "host/pair_authority_contracts.inc"
 #include "host/connect_repeat_contracts.inc"
 #include "host/browser_bridge_contracts.inc"
 #include "host/pair_response_contracts.inc"
 static void run(const std::string &mode) {
  if(mode=="quiesce-error-delayed") {
   BluetoothLifecycle bt;controller_status=test_status=0;async_bluetooth=true;std::thread host;
   host_task_created=[&](TaskHandle_t t){if(t->name=="nimble_host")host=std::thread([t]{run_task(t);});};
   assert(bt.start(TeslaVehicle::gatt_callback));auto*t=tasks.back().get();std::thread lifecycle([t]{run_task(t);});
   const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
   while(!bt.active()&&std::chrono::steady_clock::now()<end)std::this_thread::yield();assert(bt.active());
   const uint8_t addr[]={1,2,3,4,5,6};assert(bt.central()->open(addr,0,1));
   test_submit=[](int stage){if(stage==7){test_api_rc=7;host_bt_delay_hook.store(+[](){
    host_bt_delay_hook=nullptr;test_api_rc=0;ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect.status=13;test_gap(&e,test_gap_arg);
   });}};
   bt.request_stop();lifecycle.join();if(bt.failed())test_run_host=false;if(host.joinable())host.join();bt.supervise();host_bt_delay_hook=nullptr;host_task_created={};async_bluetooth=false;
   assert(bt.stopped()&&!bt.failed()&&"cancel error is not missing terminal: allow bounded real drain");return;
  }
  if(mode=="quiesce-retains") {
   BluetoothLifecycle bt;HostNimbleDriver driver(bt,TeslaVehicle::gatt_callback);
   auto*c=bt.central();const uint8_t address[]={1,2,3,4,5,6};assert(c->open(address,0,1));
   test_api_rc=7;assert(!c->quiesce() && !c->idle() && !c->shutdown() && !c->deinitialize());
   test_api_rc=0;assert(c->quiesce() && !c->idle() && !c->shutdown() && !c->deinitialize());
   ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect.status=13;test_gap(&e,test_gap_arg);assert(c->idle());return;
  }
  if(mode=="quiesce-disconnect-race") {
   BluetoothLifecycle bt;HostNimbleDriver driver(bt,TeslaVehicle::gatt_callback);
   auto*c=bt.central();const uint8_t address[]={1,2,3,4,5,6};
   assert(c->open(address,0,1));nimble_complete_setup();
   test_submit=[](int stage){if(stage==6){nimble_force_disconnect();test_api_rc=7;}};
   assert(c->quiesce() && "terminal disconnect races terminate ENOTCONN: ownership drained, not failure");
   test_api_rc=0;return;
  }
  if(mode=="pending-connect-stop"||mode=="late-connect-stop") {
   BluetoothLifecycle bt;
   HostNimbleDriver driver(bt,TeslaVehicle::gatt_callback);
   auto *c=bt.central();const uint8_t address[]={1,2,3,4,5,6};
   assert(c && c->open(address,0,1) && !c->idle());
   // SDK stop may finish before a controller's cancelled CONNECT callback:
   // no established links are counted by ble_hs_stop. Do not stop host first.
   test_on_stop=[c] { assert(c->idle() && "pending CONNECT must drain before host stop"); };
   const bool late=mode=="late-connect-stop";
   test_submit=[late](int stage) {
    if(stage==7) { ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect.status=late?0:13;e.connect.conn_handle=4;test_gap(&e,test_gap_arg); }
    if(stage==6)nimble_force_disconnect();
   };
   bt.request_stop();
   // Driver joins real production lifecycle and asserts stopped, no quarantine.
   std::puts("PENDING_CONNECT_STOP awaiting lifecycle drain");return;
  }
  if(mode=="init-fail"||mode=="host-create-fail"||mode=="stop-fail"||mode=="foreign-stack"){
   BluetoothLifecycle bt;controller_status=test_status=(mode=="foreign-stack"?2:0);async_bluetooth=true;std::thread host;
   host_task_created=[&](TaskHandle_t t){if(t->name=="nimble_host")host=std::thread([t]{run_task(t);});};
   if(mode=="host-create-fail")fail_task="nimble_host";
   if(mode=="init-fail")test_init_rc=6;
   assert(bt.start(TeslaVehicle::gatt_callback));auto*t=tasks.back().get();std::thread lifecycle([t]{run_task(t);});
   auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!bt.active()&&!bt.failed()&&std::chrono::steady_clock::now()<end)std::this_thread::yield();
   if(mode=="stop-fail"){assert(bt.active());test_stop_rc=42;bt.request_stop();}
   lifecycle.join();bt.supervise();assert(bt.failed()&&!bt.stopped());
   if(mode=="init-fail"){assert(!host.joinable()&&!test_run_host&&controller_status==1);assert(!bt.start(TeslaVehicle::gatt_callback));}
   if(host.joinable()){assert(controller_status==2);test_run_host=false;host.join();}else assert(controller_status==(mode=="foreign-stack"?2:mode=="init-fail"?1:0));
   host_task_created={};async_bluetooth=false;std::puts("LIFECYCLE NimBLE error quarantine PASS");return;
  }
  TeslaVehicle m;m.pre_setup();m.setup();
  assert(peer_open_calls==0 && scan_start_calls==0 && !m.tesla);
  if(mode=="empty-legacy" || mode=="unbound-inert-legacy") {
   // Both fixtures are synthetic. The retained VIN must not seed any profile.
   const char *empty=mode=="empty-legacy"?
    "{\"enable\":false,\"vin\":\"\",\"ble_address\":\"\",\"ev_index\":-1,\"poll_interval\":300,\"wake_once\":false}":
    "{\"enable\":false,\"vin\":\"5YJ3E1EA7KF000009\",\"ble_address\":\"\",\"ev_index\":-1,\"poll_interval\":300,\"wake_once\":false}";
   LittleFS.files["/config/tesla_vehicle_config"]={empty,empty+strlen(empty)};
   const auto before=LittleFS.files;
   for(unsigned boot=0;boot<3;++boot) {
    // Fresh module state each boot; the legacy file is deliberately retained.
    TeslaVehicle fresh;fresh.pre_setup();fresh.setup();assert(fresh.prepare_tesla());fresh.register_urls();
    assert(!fresh.tesla->legacy_blocked && !fresh.tesla->state.get("legacy_blocked")->asBool());
    assert(api.states.count("tesla_vehicle/profiles") && !api.states.count("tesla_vehicle/config"));
    assert(fresh.tesla->profiles.get("entries")->count()==0);
    assert(fresh.tesla->profile.get("vin")->asString().empty());
    assert(fresh.tesla->profile.get("ble_address")->asString().empty());
    auto update=Config::Object({{"entries",Config::Array({},&fresh.tesla->profile,0,EV_MAX_EVS)}});
    auto entries=update.get("entries");assert(entries->setCount(2));entry(entries->get(size_t{0}),0);entry(entries->get(size_t{1}),1);
    for(size_t i=0;i<2;++i){entries->get(i)->get("enable")->updateBool(false);entries->get(i)->get("ble_address")->updateString("");}
    assert(fresh.validate_profiles(update,ConfigSource::API).empty());
    fresh.tesla->profiles=update;
    assert(fresh.find_profile(0) && fresh.find_profile(1));
    fresh.loop();
    assert(!fresh.tesla->runtime_owner.get() && !fresh.service_start_requested);
    assert(LittleFS.files==before && LittleFS.removals.empty() && peer_open_calls==0 && scan_start_calls==0 && task_launches==0);
   }
   std::puts("EMPTY_LEGACY_MODULE PASS 3 boots; new A/B profiles accepted; no runtime/radio/key generation/file mutation");return;
  }
  if(mode.rfind("legacy",0)==0 || mode=="corrupt-profiles") {
   const char *path=mode=="legacy-key"?"/config/tesla_ble_private_key.bak":mode=="legacy-session"?"/config/tesla_ble_session_info.tmp":
    mode=="legacy-bench"?"/tesla_ble_bench_pair_record.bak":mode=="corrupt-profiles"?"/config/tesla_vehicle_profiles":"/config/tesla_vehicle_config";
   LittleFS.files[path]={1,2,3};auto before=LittleFS.files;
   assert(m.prepare_tesla());m.register_urls();assert(m.tesla->legacy_blocked);
   assert(!m.discovery_allowed());String error;assert(!m.select_profile(0,false,error) && !error.empty());
   auto cfg=Config::Object({{"entries",Config::Array({},&m.tesla->profile,0,16)}});
   assert(!m.validate_profiles(cfg,ConfigSource::API).empty());
   for(auto name:{"tesla_vehicle/start","tesla_vehicle/pair","tesla_vehicle/poll","tesla_vehicle/forget_key","tesla_vehicle/scan"}) {
    error.clear();api.commands.at(name)(0,error);assert(!error.empty());
   }
   assert(LittleFS.files==before && peer_open_calls==0 && scan_start_calls==0);std::puts("LEGACY retained/blocked PASS");return;
  }
  m.bluetooth_boot_enabled=true;m.bluetooth_config.get("enable")->updateBool(true);
  assert(m.prepare_tesla());m.register_urls();
  assert(api.states.count("tesla_vehicle/profiles") && !api.states.count("tesla_vehicle/config"));
  auto *entries=m.tesla->profiles.get("entries");assert(entries->setCount(2));entry(entries->get(size_t{0}),0);entry(entries->get(size_t{1}),1);
  assert(m.validate_profiles(m.tesla->profiles,ConfigSource::File).empty());
  entries->get(1)->get("ev_index")->updateInt(0);assert(!m.validate_profiles(m.tesla->profiles,ConfigSource::File).empty());entry(entries->get(1),1);
  ev.profiles.erase(1);assert(!m.validate_profiles(m.tesla->profiles,ConfigSource::File).empty());ev.profiles[1]=B;
  if(mode=="idle-enabled") {
   seed(0);seed(1);const auto saved=LittleFS.files;
   HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);
   for(unsigned pass=0;pass<8;++pass) {m.loop();m.publish_bluetooth_state();}
   assert(m.bluetooth.active() && !m.tesla->runtime_owner.get() && !m.service_start_requested);
   assert(task_launches==2 && queues_live==0 && peer_open_calls==0 && scan_start_calls==0 && LittleFS.files==saved);
#if WARP_TESLA_MEMORY_PHASES
   using namespace bluetooth_memory;Sample snapshot;
   for(auto stage:{Stage::Boot,Stage::ContextBefore,Stage::ContextAfter,Stage::HostInit,Stage::HostEnabled,Stage::GattReady,Stage::BtActive})assert(read(stage,snapshot)&&snapshot.samples>0);
   assert(read(Stage::RuntimeBefore,snapshot)&&snapshot.samples==0);
#endif
   std::puts("IDLE_ENABLED PASS active Bluetooth with two stored Tesla profiles: no Tesla runtime/worker/queues/protocol, no scan/connection/storage changes");return;
  }
  if(mode.rfind("connect-repeat",0)==0){connect_flow(m,mode);return;}
  if(mode=="pair-response"){pair_response_flow(m);return;}
  if(mode=="browser-bridge"){browser_bridge(m);return;}
  if(mode.rfind("authority-",0)==0){authority_flow(m,mode);return;}
  if(mode=="flow-repeat"||mode=="flow-repeat-cleanup"){repeated_flow(m,mode=="flow-repeat-cleanup");return;}
#ifdef TESLA_NIMBLE_REAL_DRAIN_HEADER
  if(mode.rfind("followup-",0)==0){followup_flow(m,mode);return;}
#endif
  if(mode.rfind("flow-",0)==0){approval_flow(m,mode);return;}
  if(mode=="pairing"||mode=="generate"||mode=="notify-only"||mode=="indicate-and-notify"||mode=="delayed-pair"||mode=="connect-timeout"||mode=="no-card"||mode=="decline"||mode=="link-drop"||mode=="profile-switch"||mode=="partial-pair"||mode=="late-response"){
    host_rx_properties=mode=="notify-only"?16:mode=="indicate-and-notify"?48:32;pairing(m,mode=="generate",mode=="delayed-pair",mode);return;
  }
  if(mode=="delete-first") {
   seed(0);seed(1);const auto before=LittleFS.files;
   entries->get(1)->get("enable")->updateBool(false);entries->get(1)->get("vin")->updateString("");entries->get(1)->get("ble_address")->updateString("");
   HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);String error;
   assert(m.select_profile(1,true,error));auto *r=m.tesla->runtime_owner.get();bool done=false;
   on_delay=[&]{if(r->profile_pending.load())return;done=true;m.loop();assert(!r->vehicle && !r->profile_actionable.load());
    for(const auto &p:before)if(p.first.find("tesla_p00_")!=std::string::npos)assert(LittleFS.files.at(p.first)==p.second);
    for(const auto &p:LittleFS.files)assert(p.first.find("tesla_p01_")==std::string::npos);
    assert(!peer_open_calls && !scan_start_calls);r->stop_requested.store(true);};
   run_task(r->task_handle);on_delay={};assert(done);m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();
   std::puts("DELETE_FIRST PASS disabled/blank mapping, no protocol vehicle or radio, A preserved");return;
  }
  seed(0);seed(1);auto saved=LittleFS.files;
  assert(saved.at("/config/tesla_p00_112233445500_private_key")!=saved.at("/config/tesla_p01_112233445501_private_key"));
  TeslaBleStorageAdapter a,b;assert(a.select_profile(0,A)&&b.select_profile(1,B));
  uint8_t bytes[8]{};size_t size=0;assert(a.load_into("session_vcsec",bytes,8,size)==tesla_clean::LoadResult::Loaded&&bytes[0]==10);
  assert(b.load_into("session_vcsec",bytes,8,size)==tesla_clean::LoadResult::Loaded&&bytes[0]==11);
  assert(!a.select_profile(-1,A)&&!a.select_profile(16,A)&&!a.select_profile(0,0));
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);
  ev.index=0;ev.mac=A;explicit_scan(m,0);String error;assert(m.select_profile(0,false,error));auto *r=m.tesla->runtime_owner.get();assert(r);
  unsigned phase=0;on_delay=[&] {
   m.loop();m.publish_bluetooth_state();m.process_module_events();
   if(r->profile_pending.load())return;
   assert(r->profile_ready.load() && r->authorized.load());
   assert(m.tesla->state.get("paired")->asBool());
   assert(peer_open_calls==0 && scan_start_calls==phase+1); // Only explicit scans; never pairing or implicit radio work.
   native_roundtrip(m);
   if(phase==0) {
    assert(r->configured_ev_index==0 && m.live_profile(0));soc(m,31);assert(ev.applied==1);
    assert(ev.publications.back().index==0 && ev.publications.back().mac==A && ev.publications.back().soc==31 && ev.tesla_source_updates==1);
    m.tesla->profile_command.get("ev_index")->updateInt(1);error.clear();api.commands.at("tesla_vehicle/poll")(0,error);assert(!error.empty());
    ++ev.generation;ev.index=1;ev.mac=B;m.loop();explicit_scan(m,1);error.clear();assert(m.select_profile(1,false,error));++phase;
   } else if(phase==1) {
    assert(r->configured_ev_index==1 && m.live_profile(1));soc(m,62);assert(ev.applied==2);
    assert(ev.publications.back().index==1 && ev.publications.back().mac==B && ev.publications.back().soc==62 && ev.tesla_source_updates==2);
    ++ev.generation;ev.index=0;ev.mac=A;m.loop();explicit_scan(m,0);error.clear();assert(m.select_profile(0,false,error));++phase;
   } else {
    assert(r->configured_ev_index==0 && m.live_profile(0));soc(m,33);assert(ev.applied==3);
    assert(ev.publications.back().index==0 && ev.publications.back().mac==A && ev.publications.back().soc==33 && ev.tesla_source_updates==3);
    assert(LittleFS.files==saved && "A-B-A must not regenerate keys or pair");
    // Accepted poll, then SLAC changes before main has published it.
    m.tesla->profile_command.get("ev_index")->updateInt(0);error.clear();api.commands.at("tesla_vehicle/poll")(0,error);assert(error.empty());
    ++ev.generation;ev.mac=B;m.process_commands();assert(peer_open_calls==0 && !r->manual_poll_pending.load());
    // Late authenticated SoC is rejected by the final exact EV identity gate.
    auto old=TeslaBleIdentity{ev.generation-1,0,A};r->session.publish(old);r->session.bind(A);r->session.connect();
    auto req=r->session.arm(r->epoch,old,0);m.post_event(TeslaVehicle::EventType::Soc,nullptr,99,req,true,true);m.process_module_events();assert(ev.applied==3);
    ev.mac=A;++ev.generation;m.refresh_live_session();m.update_active_vehicle();m.publish_bluetooth_state();
    // Delete B while A is plugged in. This is local-only; no connection to B.
    error.clear();assert(m.select_profile(1,true,error));m.apply_profile();m.publish_profile();
    assert(!r->authorized.load());
    // No SoC action may be admitted after this profile's local material was erased.
    ev.index=1;ev.mac=B;++ev.generation;m.refresh_live_session();m.update_active_vehicle();m.publish_bluetooth_state();
    m.tesla->profile_command.get("ev_index")->updateInt(1);error.clear();api.commands.at("tesla_vehicle/poll")(0,error);
    assert(!error.empty() && !r->manual_poll_pending.load());
    for(const auto &item:saved)if(item.first.find("tesla_p00_")!=std::string::npos)assert(LittleFS.files.at(item.first)==item.second);
    for(const auto &item:LittleFS.files)assert(item.first.find("tesla_p01_")==std::string::npos);
    r->stop_requested.store(true);++phase;
   }
  };
  run_task(r->task_handle);on_delay={};assert(phase==3 && peer_open_calls==0 && ev.applied==3);
  m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();
#if WARP_TESLA_MEMORY_PHASES
  using namespace bluetooth_memory;Sample snapshot;
  // Deliberately after Runtime retirement; no handle to a deleted task needed.
  for(auto stage:{Stage::RuntimeBefore,Stage::QueuesReady,Stage::TaskCreated,Stage::WorkerBegin,Stage::ProtocolReady,Stage::ProfileApplied,Stage::WorkerStopped,Stage::TeardownStopped})assert(read(stage,snapshot)&&snapshot.samples>0);
#endif
  std::puts("MULTI_TESLA PASS 2 profiles/isolated keys+sessions/A-B-A no re-pair/delete B preserves A/duplicate rejection/live SLAC recheck/final SoC gate");
 }
};
int main(int argc,char **argv){TeslaBleModuleTest::run(argc>1?argv[1]:"multi");assert_charging_untouched();}
