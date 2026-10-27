// Card-confirmed pairing (corrected field sequence from Nils, 2026-10-27):
// explicit "Mit Tesla koppeln" -> key card presented FIRST -> only then the vehicle
// shows the add-key request -> user confirms on the vehicle. Native NimBLE adapter,
// RX queue, real worker task, real main loop and Config API; fixture crypto/keys.
// Mechanism under test: the MAIN task's zero-wait outcome publication critical
// section overlapping a worker WRITE gate (runtime_binding_matches) in the middle
// of a framed verification request. Deterministic interleaving model, not an RF
// or vehicle recording; no historical attribution of the field event.
//
// Modes: card-control (no contention), card-confirm (contention + delayed
// confirmation), card-repeat (contention without confirmation -> local expiry,
// then second explicit attempt on the same runtime with contention + delayed
// confirmation), card-hammer (concurrent thread running the real main-side
// zero-wait outcome reads during the whole verification).
 static Frame card_unknown_key_reply(const std::array<uint8_t,16> &address,const std::array<uint8_t,16> &request_uuid) {
  // Official universal_message.proto (fc5a1910): RoutableMessage{6 to{2 routing_address},
  // 7 from{1 DOMAIN_VEHICLE_SECURITY}, 12 MessageStatus{1 OPERATIONSTATUS_ERROR=2,
  // 2 MESSAGEFAULT_ERROR_UNKNOWN_KEY_ID=3}, 50 request_uuid, 51 uuid}. No session_info.
  tesla_clean::ProtoWriter<24> to,from;tesla_clean::ProtoWriter<8> status;tesla_clean::ProtoWriter<128> out;
  static const uint8_t uuid[16]={0x5a,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
  assert(to.block(2,{address.data(),16})&&from.integer(1,2)&&status.integer(1,2)&&status.integer(2,3));
  assert(out.block(6,to.bytes())&&out.block(7,from.bytes())&&out.block(12,status.bytes())&&out.block(50,{request_uuid.data(),16})&&out.block(51,{uuid,16}));
  Frame f;const auto wire=out.bytes();f.length=wire.size;memcpy(f.data.data(),wire.data,wire.size);return f;
 }
 static Frame card_raw(std::initializer_list<uint8_t> bytes){Frame f;f.length=bytes.size();std::copy(bytes.begin(),bytes.end(),f.data.begin());return f;}
 static void card_flow(TeslaVehicle &m,const std::string &mode) {
  using namespace controller_fixture;
  const bool contention=mode!="card-control",hammer=mode=="card-hammer",repeat=mode=="card-repeat";
  // Negative controls: genuine SLAC identity change or authority expiry while the worker waits for MAIN.
  const bool negative=mode=="card-identity-change"||mode=="card-authority-expired";
  const char *KEY="/config/tesla_p00_112233445500_private_key",*AUTH="/config/tesla_p00_112233445500_authorized";
  Harness peer;IdentityLease key;assert(peer.keys.acquire(key));uint8_t pem[512]{};size_t n=0;assert(key.key()->export_pem(pem,sizeof(pem),n));
  TeslaBleStorageAdapter store;assert(store.select_profile(0,A));assert(tesla_clean::KeyStorage::committed(store.save_transaction("private_key",pem,n)));
  assert(tesla_clean::KeyStorage::committed(store.save_transaction("vehicle_vin",reinterpret_cast<const uint8_t *>("5YJ3E1EA7KF000001"),17)));
  const auto retained=LittleFS.files.at(KEY);
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;explicit_scan(m,0);String error;assert(m.select_profile(0,false,error));auto*r=m.tesla->runtime_owner.get();
  host_pair_trace_log.clear();
  const unsigned attempts=repeat?2:1;unsigned attempt=0,stage=0,frames=0,holds=0,main_ticks=0,status_counter=0,unknown_key_replies=0;
  uint32_t pair_at=0,card_at=0,confirm_at=0;bool card_done=false,confirmed=false,in_main=false,finished=false,authenticated_seen=false,first_hello_kept=false;
  uint64_t previous_generation=0;Sent first_hello;
  std::vector<uint8_t> wire;std::deque<Frame> replies;std::vector<uint8_t> pending;size_t reply_at=0;bool deliver_guard=false;
  std::atomic<unsigned> fragments{0};std::atomic<int> hold_state{0};std::thread holder;unsigned probe_countdown=0;
  std::atomic<bool> hammer_run{false};std::atomic<unsigned long> hammer_reads{0};std::thread hammer_thread;
  const auto queue_reply=[&](const Frame &f){replies.push_back(f);};
  // Worker-context probe: the first profile-MAC read after the first fragment of a
  // verification frame was written is runtime_binding_matches() in the NEXT
  // fragment's gatt_operation pre-gate (tesla_profiles.inc:86), immediately before
  // its outcome-lock read; the cancel predicate's blocking read already returned.
  host_profile_mac_probe=[&]{
   if(in_main||!probe_countdown||!running_task||running_task->name!="tesla_ble")return;
   if(--probe_countdown)return;
   hold_state=0;const unsigned at=fragments.load();
   holder=std::thread([&,at]{card_confirm_access::lock(r->command_outcome);
    if(mode=="card-identity-change")++ev.generation; // new SLAC session, same profile/MAC
    if(mode=="card-authority-expired")clock_ms.fetch_add(TeslaBleCommandOutcome::PAIR_AUTHORITY_MS+1);
    hold_state=1;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    // The worker cannot have written another fragment while MAIN held the lock.
    hold_state=fragments.load()==at?2:3;card_confirm_access::unlock(r->command_outcome);});
   while(hold_state.load()==0)std::this_thread::yield();++holds;
  };
  on_gatt_bytes=[&](const uint8_t*p,size_t count){
   const bool start=wire.empty();++fragments;wire.insert(wire.end(),p,p+count);
   if(start&&contention&&!hammer&&card_done&&holds<attempt+1&&stage==2&&count==20)probe_countdown=1;
   if(wire.size()<2||wire.size()!=size_t(wire[0])*256+wire[1]+2)return;
   ++frames;Frame f;f.length=wire.size()-2;memcpy(f.data.data(),wire.data()+2,f.length);wire.clear();
   if(stage!=2)return;
   const auto sent=decode(f);if(sent.pair)return;
   peer.address=r->routing_address;
   if(sent.hello){
    if(!first_hello_kept){first_hello=sent;first_hello_kept=true;}
    if(!confirmed){queue_reply(card_unknown_key_reply(r->routing_address,sent.uuid));++unknown_key_replies;return;}
    // Controls before the valid reply: wrong peer, stale request, invalid MAC.
    auto wrong=peer.address;wrong[0]^=0x55;std::swap(peer.address,wrong);queue_reply(peer.hello(sent));std::swap(peer.address,wrong);
    if(first_hello.uuid!=sent.uuid)queue_reply(peer.hello(first_hello));
    queue_reply(peer.hello(sent,false,7,1234,true));
    queue_reply(peer.hello(sent));return;
   }
   if(confirmed){queue_reply(peer.response(sent,{awake,sizeof(awake)},++status_counter,0,true));queue_reply(peer.response(sent,{awake,sizeof(awake)},status_counter));}
  };
  test_submit=nimble_auto_submit;
  on_task_delay=[&](uint32_t){
   ++main_ticks;in_main=true;m.loop();in_main=false;
   if(deliver_guard)return;deliver_guard=true;
   if(pending.empty()&&!replies.empty()){
    const Frame f=replies.front();replies.pop_front();
    const auto valid_hello=f.length>0&&confirmed&&replies.empty();
    if(valid_hello)assert(!r->authorized.load()&&"no authorization before the valid authenticated sequence");
    pending={uint8_t(f.length>>8),uint8_t(f.length)};pending.insert(pending.end(),f.data.begin(),f.data.begin()+f.length);reply_at=0;
   }
   if(!pending.empty()){
    // Fragmented receive: <=20-byte indications interleaved with worker writes.
    os_mbuf mb;const auto k=std::min<size_t>(20,pending.size()-reply_at);mb.bytes.assign(pending.begin()+reply_at,pending.begin()+reply_at+k);
    ble_gap_event e{};e.type=BLE_GAP_EVENT_NOTIFY_RX;e.notify_rx.conn_handle=4;e.notify_rx.attr_handle=20;e.notify_rx.om=&mb;e.notify_rx.indication=1;test_gap(&e,test_gap_arg);reply_at+=k;
    if(reply_at==pending.size())pending.clear();
   }
   deliver_guard=false;
  };
  const auto submit=[&]{m.tesla->profile_command.get("ev_index")->updateInt(0);error.clear();api.commands.at("tesla_vehicle/pair")(0,error);
   if(!error.empty())std::fprintf(stderr,"CARD_SUBMIT attempt=%u error=%s\n",attempt+1,error.c_str());assert(error.empty());};
  on_delay=[&]{
   if(!running_task||running_task->name!="tesla_ble"||r->profile_pending.load())return;
   in_main=true;m.loop();in_main=false;assert(!m.tesla->runtime_owner.restart_required());
   if(stage==0){submit();stage=1;return;}
   if(stage==1&&m.tesla->state.get("pair_request_sent")->asBool()){
    assert(r->pair_tx_total.load()==88&&r->pair_tx_acked.load()==88&&!r->authorized.load());
    pair_at=millis();card_at=pair_at+1500;confirm_at=(repeat&&attempt==0)?0:pair_at+8000;card_done=confirmed=false;first_hello_kept=false;
    // A predecessor-generation callback cannot reach the successor attempt.
    if(previous_generation){const uint8_t stale[]={0,4,0x22,2,8,1};TeslaVehicle::gatt_callback({TeslaNimbleEvent::Rx,previous_generation,4,20,0,stale,sizeof(stale)});}
    previous_generation=r->physical_generation.load();stage=2;return;
   }
   if(stage!=2)return;
   if(!card_done&&millis()>=card_at){
    // Card first: vehicle-side NFC read. card-confirm models the field counters
    // (no recognized reply): only an unsolicited plaintext vehicle status. The
    // other modes add Tesla's official async whitelist WAIT. Neither may approve.
    if(mode!="card-confirm")queue_reply(card_raw({0x22,0x02,0x08,0x01}));
    queue_reply(card_raw({0x0a,2,0x18,1}));card_done=true;
    if(hammer&&!hammer_run.exchange(true))hammer_thread=std::thread([&]{while(hammer_run.load()){(void)r->command_outcome.forward_state();(void)r->command_outcome.stop_state();++hammer_reads;}});
   }
   if(confirm_at&&!confirmed&&millis()>=confirm_at)confirmed=true; // user presses confirm on the vehicle screen
   if(r->authorized.load())authenticated_seen=true;
   if(m.tesla->state.get("pairing_pending")->asBool()){assert(millis()-pair_at<62000);return;}
   if(hammer&&hammer_run.exchange(false)&&hammer_thread.joinable())hammer_thread.join();
   if(holder.joinable())holder.join();
   in_main=true;m.loop();m.publish_bluetooth_state();in_main=false;
   const auto outcome=m.tesla->state.get("pair_outcome")->asString(),cause=m.tesla->state.get("pair_loss_cause")->asString();
   const auto line=std::string("attempt=")+std::to_string(attempt+1)+" outcome="+outcome.c_str()+" stage="+m.tesla->state.get("pair_stage")->asString().c_str()+
    " loss="+cause.c_str()+"/"+m.tesla->state.get("pair_loss_stage")->asString().c_str()+"/"+std::to_string(m.tesla->state.get("pair_loss_detail")->asInt())+
    " disc="+std::to_string(m.tesla->state.get("pair_disconnect_reason")->asInt())+" polls="+std::to_string(m.tesla->state.get("pair_verify_polls")->asUint())+
    " rx="+std::to_string(m.tesla->state.get("pair_rx_notifications")->asUint())+"/"+std::to_string(m.tesla->state.get("pair_rx_frames")->asUint())+"/"+
    std::to_string(m.tesla->state.get("pair_rx_pair_replies")->asUint())+"/"+std::to_string(m.tesla->state.get("pair_rx_session_replies")->asUint())+
    " holds="+std::to_string(holds)+" hold_state="+std::to_string(hold_state.load())+" hammer_reads="+std::to_string(hammer_reads.load())+
    " unknown_key="+std::to_string(unknown_key_replies)+" elapsed="+std::to_string(millis()-pair_at)+" error="+m.tesla->state.get("last_error")->asString().c_str();
   std::printf("CARD_ATTEMPT mode=%s %s\n",mode.c_str(),line.c_str());std::fflush(stdout);
   if(negative){
    // Fail-safe after the wait: no approval, no persistence, retained key, no quarantine.
    assert(holds==1&&outcome!="approved"&&!r->authorized.load()&&!m.tesla->state.get("paired")->asBool()&&!LittleFS.files.count(AUTH));
    assert(LittleFS.files.at(KEY)==retained&&ev.applied==0&&!m.tesla->runtime_owner.restart_required()&&!r->operation.retired());
    std::printf("CARD_NEGATIVE_PASS mode=%s outcome=%s\n",mode.c_str(),outcome.c_str());std::fflush(stdout);
    finished=true;r->stop_requested=true;stage=9;return;
   }
   if(outcome=="link_lost"&&cause=="frame_refused"){
    // Baseline mechanism: a local write gate refused a fragment mid-frame.
    std::printf("CARD_CONFIRM_RED mode=%s detail=%d stage=%s\n",mode.c_str(),int(m.tesla->state.get("pair_loss_detail")->asInt()),m.tesla->state.get("pair_loss_stage")->asString().c_str());std::fflush(stdout);
    std::_Exit(3);
   }
   assert(!r->operation.retired()&&!r->stop_requested.load()&&m.bluetooth.active()&&!m.tesla->runtime_owner.restart_required());
   assert(LittleFS.files.at(KEY)==retained&&ev.applied==0);
   if(contention&&!hammer)assert(holds==attempt+1&&hold_state.load()==2&&"MAIN publication overlapped the worker WRITE gate");
   if(confirm_at){
    assert(outcome=="approved"&&r->authorized.load()&&m.tesla->state.get("paired")->asBool()&&authenticated_seen);
    assert(LittleFS.files.at(AUTH)==std::vector<uint8_t>{1}&&millis()-pair_at>=8000&&millis()-pair_at<60000);
    assert(m.tesla->state.get("pair_loss_cause")->asString()=="none");
   } else {
    assert(outcome=="local_expiry"&&!r->authorized.load()&&!m.tesla->state.get("paired")->asBool()&&!LittleFS.files.count(AUTH));
    assert(m.tesla->state.get("last_error")->asString().find("Approval deadline expired;")==0&&millis()-pair_at>=60000);
   }
   assert(!unknown_key_replies||m.tesla->state.get("pair_rx_frames")->asUint()>=unknown_key_replies);
   std::printf("CARD_ATTEMPT_PASS mode=%s attempt=%u outcome=%s\n",mode.c_str(),attempt+1,outcome.c_str());std::fflush(stdout);
   ++attempt;unknown_key_replies=0;replies.clear();pending.clear();
   if(attempt==attempts){finished=true;r->stop_requested=true;stage=9;return;}
   stage=0;
  };
  run_task(r->task_handle);on_delay={};on_task_delay={};on_gatt_bytes={};host_profile_mac_probe={};test_submit=nimble_auto_submit;
  hammer_run=false;if(hammer_thread.joinable())hammer_thread.join();if(holder.joinable())holder.join();
  assert(finished&&main_ticks>40);
  unsigned approved=0,refused=0,sent_lines=0;for(const auto &l:host_pair_trace_log){approved+=l.find(": approved")!=std::string::npos;refused+=l.find("frame_refused")!=std::string::npos;sent_lines+=l.find(": request sent")!=std::string::npos;}
  for(const auto &l:host_pair_trace_log)std::printf("CARD_TRACE %s\n",l.c_str());
  assert(negative?approved==0:(approved==1&&refused==0&&sent_lines==attempts));
  m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();test_submit={};
  std::printf("CARD_CONFIRM_PASS mode=%s attempts=%u holds=%u hammer_reads=%lu\n",mode.c_str(),attempts,holds,hammer_reads.load());
 }
