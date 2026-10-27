// Field incident 2026-10-27: PRESENT_KEY 88/88 ATT-acknowledged, pair_result=3
// (PairRequestSent), then "Pairing link/session lost; transmission may have
// occurred; approval not authenticated", connect_flags=51, no response seen.
// Each mode drives one distinct, source-confirmed loss mechanism through the
// actual production worker/main/queue/adapter/NimBLE-central path (no manual
// process_commands/tick loop) and then an immediate second explicit attempt on
// the same runtime/key that runs the full 60 s no-approval wait.
// Legacy signature (visible device fields) must be identical for every mode and
// equal to the field evidence; the first-cause trace must tell them apart.
// Synthetic host timing/radio only; no RF, vehicle or historical attribution.
 static std::string legacy_signature(TeslaVehicle &m) {
  auto &s=m.tesla->state;char text[320];
  snprintf(text,sizeof(text),"error=%s|outcome=%s|result=%d|tx=%u/%u/%u|complete=%d|seen=%d|whitelist=%d|flags=%u|submit=%d|cancel=%d|status=%d|restart=%d|busy=%d|key=%d/%s|paired=%d|pending=%d|sent=%d|timeout=%s",
   s.get("last_error")->asString().c_str(),s.get("pair_outcome")->asString().c_str(),s.get("pair_result")->asInt(),
   s.get("pair_tx_total_bytes")->asUint(),s.get("pair_tx_attempted_bytes")->asUint(),s.get("pair_tx_acked_bytes")->asUint(),
   s.get("pair_tx_complete")->asBool(),s.get("pair_response_seen")->asBool(),s.get("pair_response_whitelist")->asBool(),
   s.get("connect_flags")->asUint(),s.get("connect_submit_rc")->asInt(),s.get("connect_cancel_rc")->asInt(),s.get("connect_status")->asInt(),
   s.get("restart_required")->asBool(),s.get("action_busy")->asBool(),s.get("key_present")->asBool(),s.get("key_presence")->asString().c_str(),
   s.get("paired")->asBool(),s.get("pairing_pending")->asBool(),s.get("pair_request_sent")->asBool(),s.get("timeout_cause")->asString().c_str());
  return text;
 }
 static std::string trace_signature(TeslaVehicle &m) {
  auto &s=m.tesla->state;
  if(!s.has("pair_loss_cause"))return "MISSING";
  char text[256];
  snprintf(text,sizeof(text),"stage=%s|cause=%s|at=%s|detail=%d|disc=%d|elapsed=%u|polls=%u|denied=%u|rxn=%u|rxf=%u|pr=%u|sr=%u",
   s.get("pair_stage")->asString().c_str(),s.get("pair_loss_cause")->asString().c_str(),s.get("pair_loss_stage")->asString().c_str(),
   s.get("pair_loss_detail")->asInt(),s.get("pair_disconnect_reason")->asInt(),s.get("pair_loss_elapsed_ms")->asUint(),
   s.get("pair_verify_polls")->asUint(),s.get("pair_verify_denied")->asUint(),s.get("pair_rx_notifications")->asUint(),
   s.get("pair_rx_frames")->asUint(),s.get("pair_rx_pair_replies")->asUint(),s.get("pair_rx_session_replies")->asUint());
  return text;
 }
 static void trace_disconnect(int reason) {
  ble_gap_event e{};e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.conn_handle=4;e.disconnect.reason=reason;test_gap(&e,test_gap_arg);
 }
#ifdef TESLA_BLE_PAIR_TRACE
 // First-cause freeze under real concurrency: several callback-like writers race
 // trace_cause() while a main-like reader samples. Any observed cause must carry
 // its own writer's detail/stage/time (no torn mix), exactly one writer wins,
 // and after the terminal entry is queued late events cannot overwrite it.
 static void trace_freeze(TeslaVehicle &m) {
  using RT=TeslaVehicle::Runtime;
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;
  explicit_scan(m,0);String error;assert(m.select_profile(0,false,error));auto*r=m.tesla->runtime_owner.get();assert(r);
  constexpr unsigned ROUNDS=20000,WRITERS=3;unsigned torn=0,observed=0;
  const RT::TraceCause causes[WRITERS]={RT::TraceCause::RemoteDisconnect,RT::TraceCause::Security,RT::TraceCause::RxOverflow};
  for(unsigned round=0;round<ROUNDS;++round){
   r->trace_arm();r->trace_stage(RT::TraceStage::Verifying);
   std::atomic<unsigned> go{0};std::atomic<bool> done{false};std::vector<std::thread> writers;
   for(unsigned w=0;w<WRITERS;++w)writers.emplace_back([&,w]{while(go.load()==0){}r->trace_cause(causes[w],int32_t(1000+uint32_t(causes[w])));});
   std::thread reader([&]{while(go.load()==0){}
    do{const uint32_t c=r->trace.loss_cause.load(std::memory_order_acquire);if(!c)continue;++observed;
     if(r->trace.loss_detail.load()!=int32_t(1000+c)||r->trace.loss_stage.load()!=uint32_t(RT::TraceStage::Verifying)||!r->trace.loss_at.load())++torn;
     break;}while(!done.load());});
   go=1;for(auto&t:writers)t.join();done=true;reader.join();
   const uint32_t c=r->trace.loss_cause.load();assert(c && r->trace.loss_detail.load()==int32_t(1000+c));
  }
  std::printf("TRACE_FREEZE rounds=%u observed=%u torn=%u\n",ROUNDS,observed,torn);std::fflush(stdout);
  assert(torn==0);
  // Terminal entry freezes the attempt: later secondary events are ignored.
  r->trace_arm();r->trace_stage(RT::TraceStage::Verifying);r->trace_cause(RT::TraceCause::RemoteDisconnect,0x213);
  r->trace.disconnect_reason.store(0x213);m.queue_pair_trace(7,"link_lost");assert(!r->trace.armed());
  r->trace_cause(RT::TraceCause::Security,0x50b);r->trace_cause(RT::TraceCause::Stop,0);r->trace_stage(RT::TraceStage::Approved);
  assert(r->trace.loss_cause.load()==uint32_t(RT::TraceCause::RemoteDisconnect)&&r->trace.loss_detail.load()==0x213&&r->trace.stage.load()==uint32_t(RT::TraceStage::Verifying));
  const size_t before=host_pair_trace_log.size();m.flush_pair_trace();assert(host_pair_trace_log.size()==before+1);
  assert(host_pair_trace_log.back().find("#7: link_lost stage=verifying loss=remote_disconnect/verifying/531 ")!=std::string::npos);
  // Next explicit attempt starts clean.
  r->trace_arm();assert(!r->trace.loss_cause.load()&&!r->trace.loss_detail.load()&&r->trace.disconnect_reason.load()==-1&&r->trace.armed());
  m.loop();m.publish_bluetooth_state();
  std::puts("PAIR_TRACE_PASS mode=trace-freeze cause=frozen_first_writer; no torn first-cause snapshot under concurrent writers, late events ignored after terminal entry");
 }
 // Deterministic reset/reuse coherence (B1) through the production PairTrace,
 // snapshot(), reset_pair_diagnostics()/trace_arm(), queue/flush and the real
 // Main publish_bluetooth_state(). TESLA_PAIR_TRACE_POINT pauses exactly one
 // non-test thread: the Main reader right after its cause load (1), or a cause
 // writer after admission (2) / after its claim (3). No sleeps; no stress.
 struct TracePause {
  std::mutex mu;std::condition_variable cv;int point=0;bool hit=false,go=false;std::thread::id test_thread=std::this_thread::get_id();
  void arm(int p){std::lock_guard<std::mutex> l(mu);point=p;hit=false;go=false;}
  void at(int p){std::unique_lock<std::mutex> l(mu);if(p!=point||hit||std::this_thread::get_id()==test_thread)return;hit=true;cv.notify_all();cv.wait(l,[&]{return go;});}
  void wait_hit(){std::unique_lock<std::mutex> l(mu);cv.wait(l,[&]{return hit;});}
  void release(){std::lock_guard<std::mutex> l(mu);go=true;point=0;cv.notify_all();}
 };
 static std::string trace_core(TeslaVehicle &m) {
  auto &s=m.tesla->state;char text[160];
  snprintf(text,sizeof(text),"%s|%s|%s|%d|%d|%u",s.get("pair_loss_cause")->asString().c_str(),s.get("pair_stage")->asString().c_str(),
   s.get("pair_loss_stage")->asString().c_str(),s.get("pair_loss_detail")->asInt(),s.get("pair_disconnect_reason")->asInt(),s.get("pair_loss_elapsed_ms")->asUint());
  return text;
 }
 static void trace_reset(TeslaVehicle &m) {
  using RT=TeslaVehicle::Runtime;
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;
  explicit_scan(m,0);String error;assert(m.select_profile(0,false,error));auto*r=m.tesla->runtime_owner.get();assert(r);
  static_assert(RT::PairTrace::EPOCH_ONE<=(1u<<12),"epoch must keep >=20 bits against practical rollover alias");
  TracePause pause;host_pair_trace_point=[&](int p){pause.at(p);};
  const std::string OLD="remote_disconnect|verifying|verifying|531|531|3020",RESET="none|none|none|0|-1|0";
  const std::string NEXT="security|connected|connected|1291|534|1000",THIRD="frame_refused|verifying|verifying|271|-1|500";
  const std::string UNAVAILABLE="unavailable|none|none|0|-1|0";
  auto old_attempt=[&]{r->trace_arm();r->trace_stage(RT::TraceStage::Verifying);clock_ms=1000;r->trace.mark_sent(millis());
   clock_ms=4020;r->trace.note_disconnect(0x213);r->trace_cause(RT::TraceCause::RemoteDisconnect,0x213);};
  auto next_attempt=[&]{r->trace_arm();r->trace_stage(RT::TraceStage::Connected);clock_ms=10000;r->trace.mark_sent(millis());
   clock_ms=11000;r->trace.note_disconnect(0x216);r->trace_cause(RT::TraceCause::Security,0x50b);};
  auto third_attempt=[&]{r->trace_arm();r->trace_stage(RT::TraceStage::Verifying);clock_ms=20000;r->trace.mark_sent(millis());
   clock_ms=20500;r->trace_cause(RT::TraceCause::FrameRefused,0x10f);};
  // Main paused after loading the OLD cause while the worker resets/reuses the record.
  auto paused_reader=[&](const char *name,const std::function<void()> &worker,const std::string &after){
   old_attempt();m.publish_bluetooth_state();assert(trace_core(m)==OLD);
   pause.arm(1);std::thread reader([&]{m.publish_bluetooth_state();});pause.wait_hit();
   worker();pause.release();reader.join();
   const auto overlapped=trace_core(m);
   std::printf("TRACE_RESET scenario=%s overlapped=%s\n",name,overlapped.c_str());
   assert(overlapped==OLD); // discarded: the previous coherent snapshot stays, no mix
   m.publish_bluetooth_state();const auto fresh=trace_core(m);
   std::printf("TRACE_RESET scenario=%s republished=%s\n",name,fresh.c_str());assert(fresh==after);
  };
  paused_reader("reset-only",[&]{r->reset_pair_diagnostics();},RESET);
  paused_reader("next-attempt",[&]{r->reset_pair_diagnostics();next_attempt();},NEXT);
  paused_reader("two-resets",[&]{r->reset_pair_diagnostics();next_attempt();r->reset_pair_diagnostics();third_attempt();},THIRD);
  // Profile reload uses the same sequence as apply_profile(): ProfileReload cause, then reset_pair_diagnostics().
  paused_reader("profile-reload",[&]{r->trace_cause(RT::TraceCause::ProfileReload,0);r->reset_pair_diagnostics();},RESET);
  // Admitted cause writer paused (2 = admitted, 3 = claimed) across reset + next arm:
  // the stale write lands only in a DIRTY record, reported unavailable, never as the
  // successor's cause; the first clean arm after it leaves is coherent again.
  for(int point:{2,3}){
   r->trace_arm();r->trace_stage(RT::TraceStage::Verifying);m.publish_bluetooth_state();
   pause.arm(point);std::thread writer([&]{r->trace_cause(RT::TraceCause::Security,0x50b);});pause.wait_hit();
   r->reset_pair_diagnostics();r->trace_arm();assert(!r->trace.armed());
   r->trace_cause(RT::TraceCause::FrameRefused,0x10f);r->trace.note_rx();
   m.publish_bluetooth_state();const auto during=trace_core(m);
   pause.release();writer.join();
   m.publish_bluetooth_state();const auto after=trace_core(m);
   std::printf("TRACE_RESET scenario=writer-across-reset point=%d during=%s after=%s\n",point,during.c_str(),after.c_str());
   assert(during==UNAVAILABLE && after==UNAVAILABLE && (r->trace.gate.load()&RT::PairTrace::WRITERS)==0);
   third_attempt();assert(r->trace.armed());m.publish_bluetooth_state();assert(trace_core(m)==THIRD);
  }
  // Admitted writer paused across the TERMINAL freeze: log line and Main agree (unavailable).
  {
   r->trace_arm();r->trace_stage(RT::TraceStage::Verifying);
   pause.arm(3);std::thread writer([&]{r->trace_cause(RT::TraceCause::RemoteDisconnect,0x213);});pause.wait_hit();
   m.queue_pair_trace(9,"link_lost");assert(!r->trace.armed());
   pause.release();writer.join();
   m.publish_bluetooth_state();const auto main_view=trace_core(m);
   const size_t before=host_pair_trace_log.size();m.flush_pair_trace();assert(host_pair_trace_log.size()==before+1);
   const auto &line=host_pair_trace_log.back();
   std::printf("TRACE_RESET scenario=writer-across-freeze main=%s log=%s\n",main_view.c_str(),line.c_str());
   assert(main_view==UNAVAILABLE && line.find("#9: link_lost stage=none loss=unavailable/none/0 t=0 disc=-1 ")!=std::string::npos);
  }
  // Ordinary terminal freeze: frozen first cause identical in log and Main; stale/late
  // callbacks after the freeze are not admitted (writer count stays 0).
  {
   old_attempt();m.queue_pair_trace(10,"link_lost");
   r->trace_cause(RT::TraceCause::Security,0x50b);r->trace.note_disconnect(0x216);r->trace.note_rx();r->trace.count_poll();r->trace_stage(RT::TraceStage::Approved);
   assert((r->trace.gate.load()&RT::PairTrace::WRITERS)==0);
   m.publish_bluetooth_state();const auto main_view=trace_core(m);
   const size_t before=host_pair_trace_log.size();m.flush_pair_trace();assert(host_pair_trace_log.size()==before+1);
   const auto &line=host_pair_trace_log.back();
   std::printf("TRACE_RESET scenario=terminal-frozen main=%s log=%s\n",main_view.c_str(),line.c_str());
   assert(main_view==OLD && line.find("#10: link_lost stage=verifying loss=remote_disconnect/verifying/531 t=3020 disc=531 polls=0 ")!=std::string::npos);
  }
  host_pair_trace_point={};
  r->trace_arm();m.loop();m.publish_bluetooth_state();
  std::puts("PAIR_TRACE_PASS mode=trace-reset cause=coherent_or_discarded; reader paused across reset/next/two resets/profile reload discards; admitted writers across reset/freeze report unavailable, never mixed");
 }
 // B2: terminal-log lifecycle of a DIRTY successor attempt. Real queue_pair_trace/
 // flush_pair_trace and Main publish_bluetooth_state; the worker reset/arm pair is
 // the exact claim sequence (reset_pair_diagnostics + trace_arm), the profile
 // reload/inactive case is reset_pair_diagnostics alone (as apply_profile()).
 // An explicit claimed attempt logs sent/terminal exactly once and command-
 // correlated, as unavailable while DIRTY; inactive/already-terminal attempts log
 // nothing; a DIRTY record never admits a writer.
 static void trace_dirty_log(TeslaVehicle &m) {
  using RT=TeslaVehicle::Runtime;
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;
  explicit_scan(m,0);String error;assert(m.select_profile(0,false,error));auto*r=m.tesla->runtime_owner.get();assert(r);
  TracePause pause;host_pair_trace_point=[&](int p){pause.at(p);};
  const std::string UNAVAILABLE="unavailable|none|none|0|-1|0";
  const auto writers=[&]{return unsigned(r->trace.gate.load()&RT::PairTrace::WRITERS);};
  const auto dirty=[&]{return (r->trace.gate.load()&RT::PairTrace::DIRTY)!=0;};
  // queue + flush through the real worker functions; returns the new lines.
  const auto emit=[&](uint64_t command,const char *outcome){const size_t before=host_pair_trace_log.size();m.queue_pair_trace(command,outcome);m.flush_pair_trace();
   return std::vector<std::string>(host_pair_trace_log.begin()+long(before),host_pair_trace_log.end());};
  const auto has=[](const std::vector<std::string> &lines,const std::string &needle){return lines.size()==1&&lines[0].find(needle)!=std::string::npos;};
  const auto main_cause=[&]{m.publish_bluetooth_state();return m.tesla->state.get("pair_loss_cause")->asString();};
  // A late writer on a DIRTY/unarmed record must not be admitted or change any field
  // (raw fields may hold a stale admitted write; they must stay byte-identical).
  const auto raw=[&]{return std::vector<int64_t>{r->trace.loss_cause.load(),r->trace.loss_claim.load(),r->trace.loss_detail.load(),r->trace.loss_stage.load(),
   r->trace.loss_at.load(),r->trace.rx_notifications.load(),r->trace.polls.load(),r->trace.denied.load(),r->trace.disconnect_reason.load(),r->trace.stage.load(),r->trace.sent_at.load()};};
  const auto stale_writes_refused=[&]{const unsigned w=writers();const auto before=raw();const auto g=r->trace.gate.load();
   r->trace_cause(RT::TraceCause::FrameRefused,0x10f);r->trace.note_rx();r->trace.count_poll();r->trace.count_denied();
   r->trace.note_disconnect(0x216);r->trace.mark_sent(12345);r->trace_stage(RT::TraceStage::Approved);
   assert(writers()==w&&raw()==before&&r->trace.gate.load()==g);};
  // 0. No attempt on a freshly selected profile: nothing is logged.
  assert(emit(60,"link_lost").empty()&&emit(60,"sent").empty());
  // 1. Clean control: one terminal line with the frozen cause; duplicate terminal / late sent are silent.
  r->trace_arm();r->trace_stage(RT::TraceStage::Verifying);r->trace_cause(RT::TraceCause::RemoteDisconnect,531);
  assert(has(emit(61,"sent"),"#61: request sent, "));
  auto lines=emit(61,"link_lost");assert(has(lines,"#61: link_lost stage=verifying loss=remote_disconnect/verifying/531 "));
  assert(main_cause()=="remote_disconnect");
  assert(emit(61,"link_lost").empty()&&emit(61,"timeout").empty()&&emit(61,"sent").empty());
  std::puts("TRACE_DIRTY control=clean terminal=1 duplicate=0 late_sent=0");
  // 2. DIRTY successor, writer still admitted at sent (points 2/3) and at terminal,
  //    and a variant whose sent+terminal follow the writer's leave.
  for(int point:{2,3})for(int variant=0;variant<3;++variant){
   const uint64_t cmd=uint64_t(70+point*10+variant);char tag[24];snprintf(tag,sizeof(tag),"#%u: ",unsigned(cmd));
   r->trace_arm();r->trace_stage(RT::TraceStage::Verifying);
   pause.arm(point);std::thread writer([&]{r->trace_cause(RT::TraceCause::Security,1291);});pause.wait_hit();
   r->reset_pair_diagnostics();r->trace_arm(); // successor claim while the old writer is admitted
   assert(!r->trace.armed()&&dirty()&&writers()==1&&main_cause()=="unavailable");
   stale_writes_refused();
   std::vector<std::string> sent,term;
   if(variant==0){sent=emit(cmd,"sent");pause.release();writer.join();term=emit(cmd,"link_lost");}      // sent during, terminal after leave
   else if(variant==1){sent=emit(cmd,"sent");term=emit(cmd,"link_lost");pause.release();writer.join();} // both while admitted
   else {pause.release();writer.join();sent=emit(cmd,"sent");term=emit(cmd,"link_lost");}              // both after leave, still DIRTY
   assert(writers()==0&&dirty()&&!r->trace.armed());
   m.publish_bluetooth_state();assert(trace_core(m)==UNAVAILABLE);
   std::printf("TRACE_DIRTY point=%d variant=%d sent=%zu terminal=%zu main=%s log=%s\n",point,variant,sent.size(),term.size(),trace_core(m).c_str(),term.empty()?"-":term[0].c_str());
   assert(has(sent,std::string(tag)+"request sent, "));
   assert(has(term,std::string(tag)+"link_lost stage=none loss=unavailable/none/0 t=0 disc=-1 polls=0 denied=0 rx=0/0/0/0 "));
   // exactly once: duplicate terminal, other terminal outcome and late sent are silent
   assert(emit(cmd,"link_lost").empty()&&emit(cmd,"timeout").empty()&&emit(cmd,"sent").empty());
   stale_writes_refused();
   // future attempt: writers left, so the next claim is clean, armed and logs its real cause
   r->reset_pair_diagnostics();r->trace_arm();assert(r->trace.armed()&&!dirty());
   r->trace_stage(RT::TraceStage::Verifying);r->trace_cause(RT::TraceCause::FrameRefused,0x10f);
   lines=emit(cmd+100,"link_lost");assert(has(lines,"#"+std::to_string(cmd+100)+": link_lost stage=verifying loss=frame_refused/verifying/271 "));
   assert(main_cause()=="frame_refused"&&emit(cmd+100,"link_lost").empty());
  }
  // 3. Reset DIRTY before vs after the writer leaves, without a successor claim
  //    (profile reload / inactive): nothing is logged and no writer is admitted.
  for(int point:{2,3}){
   r->trace_arm();r->trace_stage(RT::TraceStage::Verifying);
   pause.arm(point);std::thread writer([&]{r->trace_cause(RT::TraceCause::Security,1291);});pause.wait_hit();
   r->trace_cause(RT::TraceCause::ProfileReload,0);r->reset_pair_diagnostics(); // apply_profile() order
   assert(dirty()&&!r->trace.armed()&&main_cause()=="unavailable");
   assert(emit(200,"link_lost").empty()&&emit(200,"sent").empty());stale_writes_refused();
   pause.release();writer.join();assert(writers()==0&&dirty());
   assert(emit(201,"link_lost").empty()&&emit(201,"sent").empty());stale_writes_refused();assert(main_cause()=="unavailable");
   // reset after the writer left: clean, still inactive (no claim) => silent
   r->reset_pair_diagnostics();assert(!dirty()&&!r->trace.armed());
   assert(emit(202,"link_lost").empty()&&main_cause()=="none");
   std::printf("TRACE_DIRTY inactive point=%d before_leave=0 after_leave=0 clean_reset=0 lines\n",point);
  }
  // 4. DIRTY via terminal freeze (writer admitted at freeze) stays a single line.
  {
   r->trace_arm();r->trace_stage(RT::TraceStage::Verifying);
   pause.arm(3);std::thread writer([&]{r->trace_cause(RT::TraceCause::RemoteDisconnect,0x213);});pause.wait_hit();
   lines=emit(210,"link_lost");pause.release();writer.join();
   assert(has(lines,"#210: link_lost stage=none loss=unavailable/none/0 t=0 disc=-1 ")&&main_cause()=="unavailable");
   assert(emit(210,"link_lost").empty()&&emit(210,"sent").empty()&&writers()==0);stale_writes_refused();
  }
  host_pair_trace_point={};
  r->reset_pair_diagnostics();r->trace_arm();assert(r->trace.armed());m.loop();m.publish_bluetooth_state();
  std::puts("PAIR_TRACE_PASS mode=trace-dirty-log cause=unavailable_once; dirty successor logs sent+terminal once, command-correlated, equal to Main; inactive/reload/terminal duplicates silent; no writer admitted while dirty");
 }
#endif
 static void trace_flow(TeslaVehicle &m,const std::string &mode) {
#ifdef TESLA_BLE_PAIR_TRACE
  if(mode=="trace-freeze"){trace_freeze(m);return;}
  if(mode=="trace-reset"){trace_reset(m);return;}
  if(mode=="trace-dirty-log"){trace_dirty_log(m);return;}
#endif
  using namespace controller_fixture;
  constexpr int REMOTE_USER_TERMINATED=0x213,LOCAL_HOST_TERMINATED=0x216,ATT_INSUFFICIENT_ENCRYPTION=0x10f,SM_FAILURE=0x50b;
  Harness peer;IdentityLease key;assert(peer.keys.acquire(key));uint8_t pem[512]{};size_t n=0;assert(key.key()->export_pem(pem,sizeof(pem),n));
  TeslaBleStorageAdapter store;assert(store.select_profile(0,A));assert(tesla_clean::KeyStorage::committed(store.save_transaction("private_key",pem,n)));
  assert(tesla_clean::KeyStorage::committed(store.save_transaction("vehicle_vin",reinterpret_cast<const uint8_t *>("5YJ3E1EA7KF000001"),17)));
  const auto retained=LittleFS.files.at("/config/tesla_p00_112233445500_private_key");
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;explicit_scan(m,0);String error;assert(m.select_profile(0,false,error));
  auto*r=m.tesla->runtime_owner.get();
  unsigned stage=0,frames=0,main_ticks=0,verify_writes=0;uint32_t pair_at=0;bool injected=false;std::vector<uint8_t> wire;
  std::string legacy1,trace1,legacy2,trace2;[[maybe_unused]] size_t log_after_first=0;  // unused without TESLA_BLE_PAIR_TRACE (original production)
  on_gatt_bytes=[&](const uint8_t*p,size_t count){
   wire.insert(wire.end(),p,p+count);
   if(wire.size()>=2&&wire.size()==size_t(wire[0])*256+wire[1]+2){++frames;wire.clear();}
  };
  test_submit=[&](int sdk){
   // Our own LE terminate is reported by the controller as LOCAL_HOST_TERMINATED.
   if(sdk==6){trace_disconnect(LOCAL_HOST_TERMINATED);return;}
   if(sdk==5 && test_handle!=21 && stage==2 && mode=="trace-write-error" && ++verify_writes==1){
    // First verification (session-info) write is refused by the peer's ATT layer.
    ble_gatt_error failed{ATT_INSUFFICIENT_ENCRYPTION};ble_gatt_attr attr{test_handle};test_written(4,&failed,&attr,test_proc_arg);return;
   }
   nimble_auto_submit(sdk);
  };
  on_task_delay=[&](uint32_t){
   ++main_ticks;m.loop();
   if(stage!=2||injected||millis()-pair_at<3000)return;
   if(mode=="trace-remote"){injected=true;trace_disconnect(REMOTE_USER_TERMINATED);}
   else if(mode=="trace-security"){injected=true;ble_gap_event e{};e.type=BLE_GAP_EVENT_ENC_CHANGE;e.enc_change.conn_handle=4;e.enc_change.status=SM_FAILURE;test_gap(&e,test_gap_arg);}
   else if(mode=="trace-overflow"){
    // Bounded burst beyond the guaranteed intake while the worker sleeps.
    injected=true;uint8_t chunk[20]{};
    for(unsigned i=0;i<200 && r->rx_overflows.load()==0;++i){os_mbuf mb;mb.bytes.assign(chunk,chunk+sizeof(chunk));ble_gap_event e{};e.type=BLE_GAP_EVENT_NOTIFY_RX;e.notify_rx.conn_handle=4;e.notify_rx.attr_handle=20;e.notify_rx.om=&mb;test_gap(&e,test_gap_arg);}
    assert(r->rx_overflows.load()>0);
   }
  };
  on_delay=[&]{
   if(!running_task||running_task->name!="tesla_ble"||r->profile_pending.load())return;
   assert(!m.tesla->runtime_owner.restart_required() && !r->stop_requested.load());
   if(stage==0||stage==3){
    if(stage==3){
     m.loop();const auto fresh=trace_signature(m);
     if(fresh!="MISSING")assert(m.tesla->state.get("pair_loss_cause")->asString()=="none" && m.tesla->state.get("pair_stage")->asString()=="none");
    }
    m.loop();m.tesla->profile_command.get("ev_index")->updateInt(0);error.clear();api.commands.at("tesla_vehicle/pair")(0,error);
    if(!error.empty())std::fprintf(stderr,"TRACE pair rejected stage=%u error=%s\n",stage,error.c_str());assert(error.empty());
    frames=0;stage=stage==0?1:4;return;
   }
   if((stage==1||stage==4)&&r->pair_result.load()==int(tesla_clean::VehicleResultCode::PairRequestSent)){
    assert(r->pair_tx_total.load()==88 && r->pair_tx_acked.load()==88);pair_at=millis();stage=stage==1?2:5;return;
   }
   if((stage==2||stage==5)&&!r->pair_verifying&&!r->pair_sending&&!r->pair_status_active&&!m.tesla->state.get("pairing_pending")->asBool()){
    m.loop();m.publish_bluetooth_state();
    assert(!r->authorized.load() && !m.tesla->state.get("paired")->asBool() && !r->operation.retired() && m.bluetooth.active());
    assert(LittleFS.files.at("/config/tesla_p00_112233445500_private_key")==retained && !LittleFS.files.count("/config/tesla_p00_112233445500_authorized"));
    if(stage==2){
     legacy1=legacy_signature(m);trace1=trace_signature(m);
#ifdef TESLA_BLE_PAIR_TRACE
     log_after_first=host_pair_trace_log.size();
#endif
     // Next explicit consent only (same runtime/key); no automatic retry.
     explicit_scan(m,0);error.clear();assert(m.select_profile(0,false,error));stage=3;return;
    }
    assert(millis()-pair_at>=60000 && millis()-pair_at<=61500);
    legacy2=legacy_signature(m);trace2=trace_signature(m);stage=9;r->stop_requested=true;return;
   }
   if(stage==2||stage==5)assert(millis()-pair_at<62000);
  };
  run_task(r->task_handle);on_delay={};on_task_delay={};test_submit=nimble_auto_submit;on_gatt_bytes={};
  m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();test_submit={};
  assert(stage==9 && main_ticks>100 && ev.applied==0);
  std::printf("TRACE_LEGACY mode=%s %s\n",mode.c_str(),legacy1.c_str());
  std::printf("TRACE_FIRST mode=%s %s\n",mode.c_str(),trace1.c_str());
  std::printf("TRACE_SECOND mode=%s legacy=%s trace=%s\n",mode.c_str(),legacy2.c_str(),trace2.c_str());
  std::fflush(stdout);
  // Second explicit attempt: ordinary no-card expiry, distinct from link loss.
  assert(legacy2.find("error=Approval deadline expired;")==0 && legacy2.find("|outcome=local_expiry|")!=std::string::npos);
  if(trace1=="MISSING"){
   std::puts("PAIR_TRACE_RED first loss cause/stage/disconnect reason are not exposed; field signature cannot discriminate mechanisms");
   std::fflush(stdout);std::_Exit(3);
  }
  auto field=[](const std::string &sig,const char *name){const auto at=sig.find(std::string(name)+"=");assert(at!=std::string::npos);const auto end=sig.find('|',at);return sig.substr(at+strlen(name)+1,end==std::string::npos?std::string::npos:end-at-strlen(name)-1);};
  const std::string cause=field(trace1,"cause"),at=field(trace1,"at"),detail=field(trace1,"detail"),disc=field(trace1,"disc");
  assert(field(trace1,"stage")=="verifying" && at=="verifying" && std::stoul(field(trace1,"elapsed"))>=1000);
  if(mode=="trace-remote")assert(cause=="remote_disconnect" && detail==std::to_string(REMOTE_USER_TERMINATED) && disc==std::to_string(REMOTE_USER_TERMINATED));
  if(mode=="trace-security")assert(cause=="security" && detail==std::to_string(SM_FAILURE) && disc==std::to_string(LOCAL_HOST_TERMINATED));
  if(mode=="trace-overflow")assert(cause=="rx_overflow" && detail=="20" && disc==std::to_string(LOCAL_HOST_TERMINATED) && std::stoul(field(trace1,"rxn"))>=100);
  if(mode=="trace-write-error")assert(cause=="frame_refused" && detail==std::to_string(ATT_INSUFFICIENT_ENCRYPTION) && disc==std::to_string(LOCAL_HOST_TERMINATED) && std::stoul(field(trace1,"elapsed"))<3000);
  if(mode!="trace-write-error")assert(std::stoul(field(trace1,"polls"))>=1 && std::stoul(field(trace1,"elapsed"))>=3000 && std::stoul(field(trace1,"elapsed"))<4000);
  // The second attempt starts from a reset trace; ordinary expiry has no loss cause.
  assert(trace2.find("cause=none|")!=std::string::npos && trace2.find("stage=verifying|")==0 && field(trace2,"disc")==std::to_string(LOCAL_HOST_TERMINATED));
  assert(std::stoul(field(trace2,"polls"))>=10);
#ifdef TESLA_BLE_PAIR_TRACE
  // Event-log chronology survives the next prepare: two lines per attempt, numbers only.
  assert(log_after_first>=2 && host_pair_trace_log.size()>=log_after_first+2);
  const auto &sent1=host_pair_trace_log[log_after_first-2],&end1=host_pair_trace_log[log_after_first-1];
  assert(sent1.find("request sent, 88/88 bytes ATT-acknowledged")!=std::string::npos);
  assert(end1.find(": link_lost stage=verifying loss="+cause+"/verifying/"+detail+" ")!=std::string::npos);
  assert(end1.find(" disc="+disc+" ")!=std::string::npos && end1.find(" tx=88/88")!=std::string::npos);
  bool expiry=false;for(size_t i=log_after_first;i<host_pair_trace_log.size();++i)expiry|=host_pair_trace_log[i].find(": local_expiry stage=verifying loss=none/none/0 ")!=std::string::npos;
  assert(expiry);
  for(const auto &line:host_pair_trace_log){
   // Privacy: no MAC/address/VIN fragments in any trace line.
   assert(line.find("11:22:33")==std::string::npos && line.find("01:02:03")==std::string::npos && line.find("5YJ3")==std::string::npos);
   std::printf("TRACE_LOG %s\n",line.c_str());
  }
#endif
  std::printf("PAIR_TRACE_PASS mode=%s cause=%s detail=%s disc=%s; same legacy field signature, distinct first cause; second explicit attempt expired normally\n",
   mode.c_str(),cause.c_str(),detail.c_str(),disc.c_str());
 }
