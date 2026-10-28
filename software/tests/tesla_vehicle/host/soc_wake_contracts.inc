// SoC wake/poll contracts through the real production paths. Included inside
// TeslaBleModuleTest (test_nimble_module.cpp). Synthetic VIN/MAC and the public
// fixture key (scalar 1) only; the "vehicle" below is a deterministic sleep model,
// never a recording of a real car.
// Manual chain per request (what guided_vehicle.tsx does):
//   API scan -> profile save (validate_profiles, API source) -> guided prepare_poll ->
//   worker apply_profile -> guidance.ready -> guided commit -> real worker request_soc ->
//   TeslaBleSocPolicy -> tesla_clean::Vehicle -> NimBLE adapter/framing/crypto -> main publication.
// Automatic chain (no browser, no API call): TeslaVehicle::loop() -> auto_poll() -> own
//   VIN-scoped discovery -> fresh_binding -> select_profile(scan revision) -> worker
//   request_soc(false) -> same policy/controller/transport -> main publication.
// Wake policy under test (user decision): at most one wake per operation, repeated wakes
// across operations allowed, spaced by TeslaBleSocPolicy::MIN_WAKE_SPACING_MS.
// RED/GREEN lines are printed; a violated expectation exits 1 with "RED: <reason>".
struct SocRun {
 unsigned wakes=0,wake_partial=0,statuses=0,statuses_after_wake=0,hellos=0,charges=0,frames=0;
 std::vector<uint32_t> after_wake_at;
 uint32_t committed=0,finished=0,update_unix=0;
 bool automatic=false,outcome_automatic=false;
 std::string error,outcome;float soc=NAN;uint64_t update=0;int applied=0;
};
static void soc_expect(bool ok,const std::string &mode,const char *what){
 if(ok)return;std::printf("RED: %s: %s\n",mode.c_str(),what);std::fflush(stdout);std::_Exit(1);
}
static void soc_wake_flow(TeslaVehicle &m,const std::string &mode) {
 using namespace controller_fixture;
 using Phase=TeslaVehicle::Tesla::AutoPoll::Phase;
 static const char *const ASLEEP_LIMITED="Tesla asleep; wake skipped, last wake less than 2 min ago; SoC not updated";
 static const char *const WAKE_NO_SOC="Tesla wake sent but no SoC received in time; SoC not updated";
 constexpr uint32_t SPACING=TeslaBleSocPolicy::MIN_WAKE_SPACING_MS;
 const bool automatic_mode=mode.rfind("soc-auto",0)==0;
 Harness peer;
 {
  // Already paired profile 0 (fixture key) with its verified transport binding,
  // as after the user's confirmed pairing.
  IdentityLease key;assert(peer.keys.acquire(key));uint8_t pem[512]{};size_t n=0;assert(key.key()->export_pem(pem,sizeof(pem),n));
  TeslaBleStorageAdapter store;assert(store.select_profile(0,A));
  const uint8_t auth=1;const auto mac=encode_vehicle_mac(A);
  assert(tesla_clean::KeyStorage::committed(store.save_transaction("private_key",pem,n)));
  assert(tesla_clean::KeyStorage::committed(store.save_transaction("vehicle_vin",reinterpret_cast<const uint8_t *>("5YJ3E1EA7KF000001"),17)));
  assert(tesla_clean::KeyStorage::committed(store.save_transaction("warp_ev_mac",mac.data(),mac.size())));
  assert(tesla_clean::KeyStorage::committed(store.save_transaction("authorized",&auth,1)));
  if(automatic_mode){uint8_t transport[26]{};assert(profile_transport_record("5YJ3E1EA7KF000001","01:02:03:04:05:06",0,transport));
   assert(tesla_clean::KeyStorage::committed(store.save_transaction("ble_binding",transport,sizeof(transport))));}
 }
 host_rx_properties=32;
 HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;
 auto *p=m.find_profile(0);String error;
 // ---- synthetic vehicle sleep model ----
 bool car_awake=false,advertising=true;uint32_t wake_at=0,awake_after_wake=0;bool never_wakes=false;
 bool drop_on_status=false,drop_on_wake_fragment=false,drop_after_wake=false,next_is_wake=false,unplug_on_charge=false;
 unsigned wake_fragments=0;bool pending_drop=false;
 auto vcsec_awake=[&]{return car_awake||(wake_at&&!never_wakes&&millis()-wake_at>=awake_after_wake);};
 std::vector<SocRun> runs;SocRun *run=nullptr;bool auto_run=false;
 std::vector<uint8_t> wire;std::deque<Frame> replies;std::vector<uint8_t> drip;size_t drip_at=0;uint32_t vc=0,info=0;
 auto *r=static_cast<decltype(m.tesla->runtime_owner.get())>(nullptr);
 const uint8_t wake_plain[]={0x10,30};const uint8_t status_plain[]={0x0a,0};
 unsigned total_frames=0,mark_frames=0;uint32_t auto_t0=0,not_found_at=0;
 auto unplug=[&]{ev.index=-1;ev.mac=0;++ev.generation;};
 auto on_frame=[&](const Frame &f){
  const auto s=decode(f);peer.address=r->routing_address;
  ++total_frames;if(run)++run->frames;
  if(s.hello){if(run)++run->hellos;replies.push_back(peer.hello(s));return;}
  if(s.pair){assert(false && "no pairing in SoC contracts");return;}
  if(s.domain==Domain::Infotainment){
   if(run)++run->charges;
   if(unplug_on_charge){unplug_on_charge=false;unplug();} // physical unplug while the SoC answer is in flight
   replies.push_back(peer.response(s,{controller_fixture::soc,sizeof(controller_fixture::soc)},++info));return;}
  if(s.plaintext.size()==sizeof(wake_plain)&&!memcmp(s.plaintext.data(),wake_plain,sizeof(wake_plain))){
   if(run)++run->wakes;wake_at=millis();if(drop_after_wake)pending_drop=true;
   return; // RKE wake: no decodable vehicle-status reply (authentic replies may only carry commandStatus).
  }
  assert(s.plaintext.size()==sizeof(status_plain)&&!memcmp(s.plaintext.data(),status_plain,sizeof(status_plain)));
  if(run){++run->statuses;if(wake_at){++run->statuses_after_wake;run->after_wake_at.push_back(millis());}}
  const bool awake=vcsec_awake();
  if(!awake&&!wake_at)next_is_wake=true;
  replies.push_back(peer.response(s,{awake?awake_bytes():asleep_bytes(),sizeof(controller_fixture::awake)},++vc));
 };
 auto feed=[&](const uint8_t *b,size_t count)->bool{ // true: frame completed by this write
  wire.insert(wire.end(),b,b+count);
  if(wire.size()<2||wire.size()!=size_t(wire[0])*256+wire[1]+2)return false;
  Frame f;f.length=wire.size()-2;memcpy(f.data.data(),wire.data()+2,f.length);wire.clear();on_frame(f);return true;
 };
 test_submit=[&](int stage){
  if(stage!=5||test_handle==21){nimble_auto_submit(stage);return;}
  if(next_is_wake&&drop_on_wake_fragment&&!wire.size()){wake_fragments=0;}
  const bool wake_frame=next_is_wake&&drop_on_wake_fragment;
  if(wake_frame&&++wake_fragments==2){
   // Partial wake exposure: the first fragment was ATT-acknowledged, then the link drops.
   if(run)++run->wake_partial;next_is_wake=false;wire.clear();nimble_force_disconnect();return;
  }
  const size_t before=replies.size();
  const bool complete=feed(test_write.data(),test_write.size());
  if(complete&&drop_on_status&&replies.size()==before+1&&run&&run->statuses&&!run->wakes){
   // Link drops while the first status request's last fragment awaits ATT: no wake on the wire.
   replies.clear();drop_on_status=false;nimble_force_disconnect();return;
  }
  if(complete&&run&&run->wakes)next_is_wake=false;
  ble_gatt_error ok{};ble_gatt_attr a{test_handle};test_written(4,&ok,&a,test_proc_arg);
 };
 // ---- radio scan answer (serves API scans and the firmware's own scans alike) ----
 auto serve_scan=[&](bool advertise){
  auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!test_scanning&&std::chrono::steady_clock::now()<end)std::this_thread::yield();
  soc_expect(test_scanning,mode,"requested discovery never started scanning");
  const auto yielded=host_bt_yields.load(std::memory_order_acquire);while(host_bt_yields.load(std::memory_order_acquire)==yielded&&std::chrono::steady_clock::now()<end)std::this_thread::yield();
  TeslaBleDiscovery::Scope scope{};assert(tesla_ble_vehicle_name("5YJ3E1EA7KF000001",scope.name));
  uint8_t data[20]={19,9};memcpy(data+2,scope.name,18);ble_gap_event e{};e.type=BLE_GAP_EVENT_DISC;e.disc.addr.val[5]=1;for(int i=0;i<5;++i)e.disc.addr.val[i]=6-i;
  e.disc.addr.type=0;e.disc.data=data;e.disc.length_data=20;e.disc.event_type=0;if(advertise)test_scan(&e,test_scan_arg);
  e.type=BLE_GAP_EVENT_DISC_COMPLETE;test_scanning=false;test_scan(&e,test_scan_arg);
  TeslaBleDiscovery::Snapshot snap{};while(std::chrono::steady_clock::now()<end){if(m.bluetooth.discovery_snapshot(snap)&&snap.status==TeslaBleDiscovery::Status::Complete&&!snap.busy)break;std::this_thread::yield();}
  assert(snap.status==TeslaBleDiscovery::Status::Complete&&snap.count==(advertise?1U:0U));
  return snap.revision;
 };
 uint32_t served_phase_ms=UINT32_MAX;
 std::vector<uint32_t> scan_at; // firmware-initiated scans (radio) by simulated time
 auto serve_auto=[&]{
  auto &ap=m.tesla->auto_poll;
  if(ap.phase==Phase::Scanning&&served_phase_ms!=ap.phase_ms){served_phase_ms=ap.phase_ms;scan_at.push_back(millis());serve_scan(advertising);}
 };
 auto capture=[&](SocRun &x){
  x.finished=millis();
  x.error=m.tesla->state.get("last_error")->asString().c_str();x.soc=m.tesla->state.get("last_soc")->asFloat();
  x.update=m.tesla->state.get("last_update")->asUptime();x.update_unix=m.tesla->state.get("last_update_unix")->asUint();x.applied=ev.applied;
  x.outcome=m.tesla->state.get("poll_outcome")->asString().c_str();x.outcome_automatic=m.tesla->state.get("poll_outcome_automatic")->asBool();
  std::printf("SOC_RUN %s #%zu %s wakes=%u partial=%u statuses=%u after_wake=%u hellos=%u charges=%u applied=%d start=%u ms=%u outcome=%s error='%s'\n",mode.c_str(),runs.size(),
   x.automatic?"auto":"manual",x.wakes,x.wake_partial,x.statuses,x.statuses_after_wake,x.hellos,x.charges,x.applied,x.committed,x.finished-x.committed,x.outcome.c_str(),x.error.c_str());std::fflush(stdout);
 };
 unsigned auto_settle=0;
 std::function<void()> before_main;
 int perturb=0;uint32_t perturb_seq0=0,recv_seen[4]{};uint32_t shorten_at=0,lengthen_at=0;size_t scans_mark=0;
 auto main_pass=[&]{auto*worker=running_task;running_task=nullptr;m.loop();running_task=worker;};
 auto track_auto=[&]{ // Observe firmware-initiated operations (phase Polling) without influencing them.
  const auto phase=m.tesla->auto_poll.phase;
  if(!auto_run&&phase==Phase::Polling){soc_expect(!run,mode,"automatic poll overlapped a manual request");runs.emplace_back();run=&runs.back();run->automatic=true;run->committed=millis();auto_run=true;auto_settle=0;}
  if(auto_run&&phase!=Phase::Polling&&++auto_settle>=3){capture(*run);run=nullptr;auto_run=false;}
 };
 on_task_delay=[&](uint32_t ms){
  if(before_main)before_main(); // test-only: perturb time/identity before the main task dispatches
  if(pending_drop&&ms==20){pending_drop=false;replies.clear();drip.clear();nimble_force_disconnect();}
  main_pass();serve_auto();track_auto();
  if(!r||!r->connected.load()){replies.clear();drip.clear();drip_at=0;return;}
  if(ms!=20)return;
  if(drip.empty()&&!replies.empty()){
   const Frame f=replies.front();replies.pop_front();
   drip={uint8_t(f.length>>8),uint8_t(f.length)};drip.insert(drip.end(),f.data.begin(),f.data.begin()+f.length);drip_at=0;
  }
  if(!drip.empty()){
   os_mbuf mb;const auto n=std::min<size_t>(20,drip.size()-drip_at);mb.bytes.assign(drip.begin()+drip_at,drip.begin()+drip_at+n);
   ble_gap_event e{};e.type=BLE_GAP_EVENT_NOTIFY_RX;e.notify_rx.conn_handle=4;e.notify_rx.attr_handle=20;e.notify_rx.om=&mb;e.notify_rx.indication=1;test_gap(&e,test_gap_arg);
   drip_at+=n;if(drip_at==drip.size()){drip.clear();drip_at=0;}
  }
 };
 // ---- production API boundary helpers (what guided_vehicle.tsx sends) ----
 unsigned request_id=0;
 auto guided=[&](const char *phase)->std::string{
  auto &c=m.tesla->guided_command;auto *q=c.get("profile");entry(q,0);
  for(const char *k:{"ble_address_type","discovery_revision","poll_interval"})q->get(k)->updateUint(p->get(k)->asUint());
  q->get("wake_once")->updateBool(p->get("wake_once")->asBool());
  char request[33];snprintf(request,sizeof(request),"%032x",request_id);
  c.get("phase")->updateString(phase);c.get("request")->updateString(request);
  c.get("generation")->updateString(std::to_string(ev.generation));c.get("revision")->updateUint(m.tesla->config_revision);
  error.clear();api.commands.at("tesla_vehicle/guided")(0,error);return error.c_str();
 };
 auto save_profile=[&]()->bool{ // UI profile save (persistent config) through the production validator.
  const auto rejected=m.validate_profiles(m.tesla->profiles,ConfigSource::API);
  if(rejected.length())return false;
  api.writeConfig("tesla_vehicle/profiles",&m.tesla->profiles);return true;
 };
 auto api_scan_and_save=[&]{
  auto &s=m.tesla->scan_command;s.get("vin")->updateString("5YJ3E1EA7KF000001");s.get("ev_index")->updateInt(0);s.get("ev_mac")->updateString("11:22:33:44:55:00");
  error.clear();api.commands.at("tesla_vehicle/scan")(0,error);soc_expect(error.length()==0,mode,"explicit API scan refused");
  const auto revision=serve_scan(true);
  main_pass();
  // UI saves exactly the adopted fresh revision (guided_vehicle.tsx command()).
  p->get("discovery_revision")->updateUint(revision);
  soc_expect(save_profile(),mode,"profile save after fresh scan rejected");
 };
 // ---- step machine executed from the real worker's OS yields ----
 std::deque<std::function<bool()>> steps;uint32_t prepared_at=0;
 auto request=[&](std::function<void()> setup){
  steps.push_back([&,setup]{
   // A user click while a background operation runs would be refused; wait for idle.
   if(auto_run||m.tesla->auto_poll.phase!=Phase::Idle)return false;
   if(r&&(r->profile_pending.load()||r->manual_poll_pending.load()))return false;
   setup();runs.emplace_back();run=&runs.back();api_scan_and_save();
   ++request_id;const auto e=guided("prepare_poll");soc_expect(e.empty(),mode,"prepare_poll refused");prepared_at=millis();return true;});
  steps.push_back([&]{
   main_pass();if(r->profile_pending.load())return false;
   if(!(m.tesla->guidance.get("ready")->asBool()&&m.tesla->guidance.get("request")->asString().length()))return false;
   soc_expect(millis()-prepared_at<12000,mode,"preparation not ready within UI deadline");
   const auto e=guided("commit");soc_expect(e.empty(),mode,"commit refused");run->committed=millis();return true;});
  steps.push_back([&]{
   if(r->manual_poll_pending.load()||r->poll_requested.load()||r->in_flight_started_ms!=0)return false;
   soc_expect(millis()-run->committed<60000,mode,"request did not finish within 60 s");
   for(int i=0;i<3;++i)main_pass();
   capture(*run);run=nullptr;return true;});
 };
 auto wait_ms=[&](uint32_t ms){steps.push_back([&,ms,until=uint32_t(0)]() mutable {if(!until)until=millis()+ms;return int32_t(millis()-until)>=0;});};
 auto until=[&](std::function<bool()> condition){steps.push_back([condition]{return condition();});};
 auto step=[&](std::function<void()> action){steps.push_back([action]{action();return true;});};
 auto runs_done=[&](size_t n){return [&,n]{return runs.size()>=n&&!auto_run&&!run;};};
 auto sleep_again=[&]{car_awake=false;wake_at=0;next_is_wake=false;};
 // ---- scenarios ----
 if(mode=="soc-reservation") {           // manual button repeatedly usable; wake on demand, spaced
  request([&]{car_awake=true;});                      // R1 awake: no wake on the wire
  request([&]{sleep_again();awake_after_wake=0;});    // R2 asleep: R1's unused reservation was released
  request([&]{sleep_again();});                       // R3 asleep right after a wake: spacing, no wake
  wait_ms(SPACING);
  request([&]{sleep_again();awake_after_wake=0;});    // R4 same plug-in session after spacing: wakes again
 } else if(mode=="soc-wake-slow") {
  request([&]{sleep_again();awake_after_wake=9000;}); // R1 asleep, VCSEC awake 9 s after the wake
 } else if(mode=="soc-wake-never") {
  request([&]{sleep_again();never_wakes=true;});      // R1 wake never confirmed
  request([&]{sleep_again();never_wakes=true;});      // R2 immediately: no second wake
  wait_ms(SPACING);
  request([&]{sleep_again();never_wakes=false;awake_after_wake=0;}); // R3 after spacing: one new wake
 } else if(mode=="soc-cancel-before-wake") {
  request([&]{sleep_again();drop_on_status=true;});   // R1 link lost before any wake TX
  request([&]{sleep_again();awake_after_wake=0;});    // R2 reconnect: unused reservation released
 } else if(mode=="soc-wake-tx-fail") {
  request([&]{sleep_again();drop_on_wake_fragment=true;}); // R1 wake partially written, link drops
  request([&]{sleep_again();drop_on_wake_fragment=false;awake_after_wake=0;}); // R2 reconnect: never resent
 } else if(mode=="soc-wake-drop-waitwake") {
  request([&]{sleep_again();never_wakes=true;drop_after_wake=true;}); // R1 full wake, link lost while waiting
  request([&]{sleep_again();drop_after_wake=false;never_wakes=false;awake_after_wake=0;}); // R2 reconnect: never resent
 } else if(mode=="soc-guided-timeout") {
  steps.push_back([&]{runs.emplace_back();run=&runs.back();api_scan_and_save();++request_id;soc_expect(guided("prepare_poll").empty(),mode,"prepare refused");prepared_at=millis();return true;});
  steps.push_back([&]{main_pass();if(r->profile_pending.load()||millis()-prepared_at<15100)return false;
   soc_expect(!guided("commit").empty(),mode,"expired preparation committed");main_pass();
   soc_expect(!r->poll_requested.load()&&!r->manual_poll_pending.load()&&runs.back().frames==0,mode,"expired commit caused radio/poll");
   run=nullptr;return true;});
  request([&]{car_awake=true;});
 } else if(mode=="soc-auto") {           // autonomous background polling, no browser/API
  rtc.base_seconds.store(1790000000);rtc.synced.store(true);
  p->get("poll_interval")->updateUint(60);soc_expect(save_profile(),mode,"interval save rejected");
  car_awake=true;
  until(runs_done(2));                                 // A1, A2: awake, no wake
  step([&]{sleep_again();});
  until(runs_done(3));                                 // A3: asleep, wake not permitted
  until([&]{if(auto_run||m.tesla->auto_poll.phase!=Phase::Idle||r->manual_poll_pending.load())return false;
   p->get("wake_once")->updateBool(true);return save_profile();}); // user enables wake in the UI
  step([&]{sleep_again();awake_after_wake=0;});
  until(runs_done(4));                                 // A4: asleep -> one wake -> SoC
  step([&]{sleep_again();});
  until(runs_done(5));                                 // A5: asleep, 60 s after the wake -> spaced, no wake
  step([&]{sleep_again();awake_after_wake=0;});
  until([&]{return auto_run&&runs.size()==6&&run&&run->hellos>0;}); // A6 in flight
  step([&]{ // explicit actions while the background operation owns the worker
   m.tesla->profile_command.get("ev_index")->updateInt(0);error.clear();api.commands.at("tesla_vehicle/poll")(0,error);
   soc_expect(error.length()>0,mode,"manual poll accepted concurrently with automatic poll");
   ++request_id;soc_expect(!guided("prepare_poll").empty(),mode,"guided preparation accepted during automatic poll");
   auto &s=m.tesla->scan_command;s.get("vin")->updateString("5YJ3E1EA7KF000001");s.get("ev_index")->updateInt(0);s.get("ev_mac")->updateString("11:22:33:44:55:00");
   error.clear();api.commands.at("tesla_vehicle/scan")(0,error);soc_expect(error.length()>0,mode,"scan accepted during automatic poll");});
  until(runs_done(6));                                 // A6: >= spacing after A4 -> wakes again
  step([&]{unplug();mark_frames=total_frames;});
  wait_ms(200000);
  step([&]{soc_expect(total_frames==mark_frames&&runs.size()==6,mode,"vehicle contact after unplug");
   soc_expect(m.tesla->state.get("auto_poll")->asString()=="off",mode,"scheduler not off after unplug");});
 } else if(mode=="soc-auto-stop") {      // not found, unplug mid-operation, profile switch, replug, unsynced clock
  p->get("poll_interval")->updateUint(60);soc_expect(save_profile(),mode,"interval save rejected");
  car_awake=true;advertising=false;
  // The not-found cycle runs before any worker exists (see the pre-worker loop below).
  until(runs_done(1));                                 // S1: unplugged while the SoC answer is in flight
  step([&]{mark_frames=total_frames;});
  wait_ms(150000);
  step([&]{soc_expect(total_frames==mark_frames,mode,"vehicle contact after unplug");
   ev.index=1;ev.mac=B;++ev.generation;mark_frames=total_frames;}); // a different WARP EV profile plugs in
  wait_ms(150000);
  step([&]{const std::string state=m.tesla->state.get("auto_poll")->asString().c_str();
   std::printf("SOC_STOP foreign-profile auto_poll=%s frames=%u\n",state.c_str(),total_frames-mark_frames);
   // Profile 1 has a mapping but no key: the scheduler may only report it, never scan/contact.
   soc_expect(total_frames==mark_frames&&(state=="off"||state=="not_paired"),mode,"foreign EV profile polled");
   ev.index=0;ev.mac=A;++ev.generation;});            // the Tesla is plugged in again
  until(runs_done(2));                                 // S2: normal SoC, clock unsynchronized
 } else if(mode=="soc-auto-interval") {  // B1: a saved interval change applies to the pending wait
  p->get("poll_interval")->updateUint(3600);soc_expect(save_profile(),mode,"interval save rejected");
  car_awake=true;
  until(runs_done(1));                                 // I1 at the plug-in settle delay
  step([&]{p->get("poll_interval")->updateUint(30);soc_expect(save_profile(),mode,"shortened interval rejected");shorten_at=millis();});
  until(runs_done(2));                                 // I2 30 s after I1, not after the old 3600 s
  wait_ms(10000);
  step([&]{p->get("poll_interval")->updateUint(600);soc_expect(save_profile(),mode,"lengthened interval rejected");
   lengthen_at=millis();scans_mark=scan_at.size();});
  until(runs_done(3));                                 // I3 600 s after I2, not after the old 30 s
 } else if(mode=="soc-auto-recvtime") {  // B2: receive time fixed by the worker, not at delayed dispatch
  rtc.base_seconds.store(1790000000);rtc.synced.store(true);
  p->get("poll_interval")->updateUint(30);soc_expect(save_profile(),mode,"interval save rejected");
  car_awake=true;
  // Once the worker has posted its terminal outcome (accepted SoC event queued, not yet
  // dispatched): delay main dispatch by 2 s and change wall-clock/identity in between.
  before_main=[&]{
   if(!perturb||!r||r->poll_outcome_sequence.load()==perturb_seq0)return;
   const int v=perturb;perturb=0;recv_seen[v]=millis();clock_ms.fetch_add(2000);
   if(v==1)rtc.base_seconds.fetch_add(1000);           // NTP step before dispatch
   if(v==2)rtc.synced.store(true);                     // first sync before dispatch
   if(v==3)++ev.generation;                            // identity retired while queued
  };
  auto arm=[&](int v){perturb_seq0=r->poll_outcome_sequence.load();perturb=v;};
  step([&,arm]{arm(1);});
  until(runs_done(1));                                 // V1 synced, delayed + clock step
  step([&,arm]{rtc.synced.store(false);arm(2);});
  until(runs_done(2));                                 // V2 unsynced at reception, synced at dispatch
  step([&,arm]{arm(3);});
  until(runs_done(3));                                 // V3 authenticated but stale generation
 } else { assert(false && "unknown soc mode"); }
 // Manual modes: the first preparation from the API thread starts the worker.
 // Automatic modes: only the main loop runs until the firmware itself starts the worker.
 if(!automatic_mode) {
  const auto first=std::move(steps.front());steps.pop_front();assert(first());
 } else {
  auto_t0=millis();
  for(unsigned guard=0;!m.tesla->runtime_owner.get();++guard){
   soc_expect(guard<20000,mode,"firmware never started its own poll worker");
   clock_ms.fetch_add(20);m.loop();serve_auto();
   if(mode=="soc-auto-stop"&&!advertising&&m.tesla->state.get("auto_poll")->asString()=="not_found"){
    soc_expect(total_frames==0&&runs.empty()&&!m.tesla->runtime_owner.get(),mode,"not-found cycle contacted the vehicle or started a worker");
    not_found_at=millis();advertising=true;unplug_on_charge=true;
   }
  }
  soc_expect(millis()-auto_t0>=15000,mode,"first automatic cycle before the plug-in settle delay");
 }
 r=m.tesla->runtime_owner.get();assert(r);
 bool done=false;
 on_delay=[&]{
  if(!running_task||running_task->name!="tesla_ble"||done)return;
  if(steps.empty()){done=true;r->stop_requested.store(true);return;}
  soc_expect(millis()<3600000,mode,"scenario exceeded one simulated hour");
  if(steps.front()())steps.pop_front();
 };
 run_task(r->task_handle);on_delay={};on_task_delay={};
 test_submit=nimble_auto_submit;
 main_pass();if(r->teardown_handle)run_task(r->teardown_handle);main_pass();test_submit={};
 soc_expect(done&&steps.empty(),mode,"scenario did not complete");
 assert_charging_untouched();
 auto expect_soc=[&](const SocRun &x,const SocRun *prev,const char *what){
  soc_expect(x.error.empty()&&x.charges>=1&&x.applied==(prev?prev->applied:0)+1&&x.soc==72.0f&&x.outcome=="ok"&&x.outcome_automatic==x.automatic,mode,what);};
 auto expect_no_new_soc=[&](const SocRun &x,const SocRun &prev,const char *what){
  soc_expect(x.applied==prev.applied&&x.update==prev.update&&x.update_unix==prev.update_unix&&
   (std::isnan(x.soc)?std::isnan(prev.soc):x.soc==prev.soc)&&x.charges==0,mode,what);};
 if(mode=="soc-reservation") {
  soc_expect(runs.size()==4,mode,"four manual requests in one plug-in session");
  expect_soc(runs[0],nullptr,"R1 awake SoC");soc_expect(runs[0].wakes==0,mode,"R1 awake car must not get a wake frame");
  soc_expect(runs[1].wakes==1,mode,"R2: awake R1 reserved but never used the wake; asleep follow-up must be able to wake");
  expect_soc(runs[1],&runs[0],"R2 woken car SoC");
  soc_expect(runs[2].wakes==0,mode,"R3: wake within the spacing");
  expect_no_new_soc(runs[2],runs[1],"R3 old SoC must not be presented as new");
  soc_expect(runs[2].error==ASLEEP_LIMITED&&runs[2].outcome=="asleep_wake_limited",mode,"R3 clear asleep/spacing outcome");
  soc_expect(runs[3].wakes==1,mode,"R4: same plug-in session after spacing must wake again (repeated wake policy)");
  expect_soc(runs[3],&runs[1],"R4 SoC");
 } else if(mode=="soc-wake-slow") {
  soc_expect(runs.size()==1&&runs[0].wakes==1,mode,"exactly one wake");
  for(size_t i=1;i<runs[0].after_wake_at.size();++i)soc_expect(runs[0].after_wake_at[i]-runs[0].after_wake_at[i-1]>=900,mode,"post-wake status polls must be paced (>=0.9 s)");
  expect_soc(runs[0],nullptr,"car awake 9 s after the wake must still deliver SoC within the unchanged 40 s request deadline");
  soc_expect(runs[0].finished-runs[0].committed<40000+5000,mode,"bounded by request deadline");
 } else if(mode=="soc-wake-never") {
  soc_expect(runs.size()==3&&runs[0].wakes==1&&runs[0].charges==0&&runs[0].applied==0,mode,"R1 one wake, no SoC");
  soc_expect(runs[0].statuses_after_wake<=40,mode,"R1 bounded post-wake status polls");
  soc_expect(runs[0].error==WAKE_NO_SOC&&runs[0].outcome=="woken_no_soc",mode,"R1 exposed wake without SoC needs a clear outcome");
  soc_expect(runs[0].finished-runs[0].committed>=39000&&runs[0].finished-runs[0].committed<=46000,mode,"R1 ends at the unchanged 40 s request deadline");
  soc_expect(runs[1].wakes==0&&runs[1].error==ASLEEP_LIMITED,mode,"R2 immediately after: no second wake, clear outcome");
  expect_no_new_soc(runs[1],runs[0],"R2 no new SoC");
  soc_expect(runs[2].wakes==1,mode,"R3 after spacing: one new wake");expect_soc(runs[2],nullptr,"R3 SoC");
 } else if(mode=="soc-cancel-before-wake") {
  soc_expect(runs.size()==2&&runs[0].wakes==0&&runs[0].wake_partial==0&&runs[0].applied==0,mode,"R1 lost before wake TX, no SoC");
  soc_expect(runs[0].outcome=="failed",mode,"R1 link loss before wake: visible failed outcome");
  soc_expect(runs[1].wakes==1,mode,"R2: definitely unused reservation must be released after link loss");
  expect_soc(runs[1],&runs[0],"R2 SoC");
 } else if(mode=="soc-wake-tx-fail") {
  soc_expect(runs.size()==2&&runs[0].wake_partial==1&&runs[0].wakes==0&&runs[0].applied==0,mode,"R1 partial wake then link loss, no SoC");
  soc_expect(runs[0].outcome=="woken_no_soc",mode,"R1 partial wake + link loss: visible exposed-wake outcome");
  soc_expect(runs[1].wakes==0&&runs[1].wake_partial==0,mode,"R2: partially sent wake counts as exposed; never resent within the spacing");
  soc_expect(runs[1].error==ASLEEP_LIMITED,mode,"R2 clear outcome");expect_no_new_soc(runs[1],runs[0],"R2 no new SoC");
 } else if(mode=="soc-wake-drop-waitwake") {
  soc_expect(runs.size()==2&&runs[0].wakes==1&&runs[0].applied==0,mode,"R1 one wake, no SoC");
  soc_expect(runs[0].outcome=="woken_no_soc",mode,"R1 wake + link loss: visible exposed-wake outcome");
  soc_expect(runs[1].wakes==0,mode,"R2: exposed wake then link loss must not allow an immediate second wake");
  soc_expect(runs[1].error==ASLEEP_LIMITED,mode,"R2 clear outcome");
 } else if(mode=="soc-guided-timeout") {
  soc_expect(runs[0].frames==0,mode,"expired preparation: no radio");
  soc_expect(!runs.back().automatic,mode,"last run is the explicit request");
  expect_soc(runs.back(),nullptr,"fresh request SoC");
 } else if(mode=="soc-auto") {
  soc_expect(runs.size()==6,mode,"six automatic operations");
  for(const auto &x:runs)soc_expect(x.automatic&&x.outcome_automatic,mode,"all operations firmware-initiated");
  soc_expect(runs[0].committed-auto_t0>=15000&&runs[0].committed-auto_t0<=15000+5000,mode,"first automatic poll after the plug-in settle delay");
  for(int i=1;i<6;++i){const auto gap=runs[i].committed-runs[i-1].finished;
   soc_expect(gap>=60000&&gap<=60000+5000,mode,"configured 60 s profile interval between operations");}
  expect_soc(runs[0],nullptr,"A1 SoC");soc_expect(runs[0].wakes==0,mode,"A1 awake: no wake");
  soc_expect(runs[0].update_unix>=1790000000+runs[0].committed/1000&&runs[0].update_unix<=1790000000+runs[0].finished/1000,mode,"A1 receive time is the synchronized wall clock at reception");
  expect_soc(runs[1],&runs[0],"A2 SoC");soc_expect(runs[1].update_unix>runs[0].update_unix,mode,"A2 newer receive time");
  soc_expect(runs[2].wakes==0&&runs[2].outcome=="asleep_wake_disabled"&&runs[2].error.empty(),mode,"A3 asleep, wake not permitted: no wake, no error banner");
  expect_no_new_soc(runs[2],runs[1],"A3 old SoC not new");
  const auto &saved=api.written_configs.at("tesla_vehicle/profiles");
  soc_expect(const_cast<Config&>(saved).get("entries")->get(size_t{0})->get("wake_once")->asBool()&&
   const_cast<Config&>(saved).get("entries")->get(size_t{0})->get("poll_interval")->asUint()==60,mode,"interval/wake permission persisted");
  soc_expect(runs[3].wakes==1,mode,"A4 permitted wake");expect_soc(runs[3],&runs[1],"A4 SoC after wake");
  soc_expect(runs[4].wakes==0&&runs[4].outcome=="asleep_wake_limited"&&runs[4].error.empty(),mode,"A5 spaced: no wake");
  expect_no_new_soc(runs[4],runs[3],"A5 old SoC not new");
  soc_expect(runs[5].wakes==1,mode,"A6 repeated on-demand wake in the same plug-in session");
  expect_soc(runs[5],&runs[3],"A6 SoC");
  for(const auto &x:runs)soc_expect(x.wakes+x.wake_partial<=1,mode,"at most one wake per operation");
 } else if(mode=="soc-auto-stop") {
  soc_expect(runs.size()==2&&not_found_at!=0,mode,"not-found cycle, then two automatic operations");
  soc_expect(runs[0].committed-not_found_at>=60000&&runs[0].committed-not_found_at<=65000,mode,"retry after not-found waits one interval");
  soc_expect(runs[0].charges==1&&runs[0].applied==0&&runs[0].update==0&&runs[0].update_unix==0&&runs[0].outcome!="ok",mode,"S1 result after unplug never published as new");
  expect_soc(runs[1],nullptr,"S2 SoC after replug");
  soc_expect(runs[1].update>0&&runs[1].update_unix==0,mode,"S2 unsynchronized clock: no invented wall-clock time");
 } else if(mode=="soc-auto-interval") {
  soc_expect(runs.size()==3,mode,"three automatic operations");
  for(const auto &x:runs)soc_expect(x.automatic&&x.wakes==0,mode,"firmware-initiated, awake, no wake");
  expect_soc(runs[0],nullptr,"I1 SoC");expect_soc(runs[1],&runs[0],"I2 SoC");expect_soc(runs[2],&runs[1],"I3 SoC");
  soc_expect(runs[0].committed-auto_t0>=15000&&runs[0].committed-auto_t0<=20000,mode,"first cycle keeps the plug-in settle delay");
  const auto gap1=runs[1].committed-runs[0].finished,gap2=runs[2].committed-runs[1].finished;
  std::printf("SOC_INTERVAL gap_after_shorten=%u gap_after_lengthen=%u scans=%zu\n",gap1,gap2,scan_at.size());
  soc_expect(shorten_at>=runs[0].finished&&gap1>=30000&&gap1<=30000+5000,mode,"shortened 3600->30 s must apply without waiting for the old 3600 s");
  soc_expect(lengthen_at>runs[1].finished&&gap2>=600000&&gap2<=600000+5000,mode,"lengthened 30->600 s must hold the new interval");
  for(size_t i=scans_mark;i<scan_at.size();++i)
   soc_expect(scan_at[i]>=runs[1].finished+600000-1000,mode,"no radio scan before the lengthened interval is due");
  soc_expect(scan_at.size()==3,mode,"exactly one scan per cycle, no extra radio contact");
 } else if(mode=="soc-auto-recvtime") {
  soc_expect(runs.size()==3&&recv_seen[1]&&recv_seen[2]&&recv_seen[3],mode,"three perturbed automatic operations");
  expect_soc(runs[0],nullptr,"V1 SoC");expect_soc(runs[1],&runs[0],"V2 SoC");
  const auto in_window=[&](const SocRun &x,uint32_t seen){return x.update>=uint64_t(x.committed)*1000&&x.update<=uint64_t(seen)*1000;};
  std::printf("SOC_RECV v1 update=%llu seen=%u unix=%u v2 update=%llu seen=%u unix=%u\n",(unsigned long long)runs[0].update,recv_seen[1],runs[0].update_unix,
   (unsigned long long)runs[1].update,recv_seen[2],runs[1].update_unix);
  soc_expect(in_window(runs[0],recv_seen[1]),mode,"V1 uptime is the worker's reception, not the delayed dispatch");
  soc_expect(runs[0].update_unix>=1790000000+runs[0].committed/1000&&runs[0].update_unix<=1790000000+recv_seen[1]/1000,mode,
   "V1 wall clock at reception, not after a later clock step");
  soc_expect(in_window(runs[1],recv_seen[2]),mode,"V2 uptime is the worker's reception");
  soc_expect(runs[1].update_unix==0,mode,"V2 unsynchronized at reception stays 0 even if synced before dispatch");
  soc_expect(runs[2].charges==1&&runs[2].applied==runs[1].applied&&runs[2].update==runs[1].update&&runs[2].update_unix==runs[1].update_unix&&
   runs[2].soc==runs[1].soc,mode,"V3 authenticated stale-generation event must publish nothing");
 }
 std::printf("SOC_WAKE_PASS %s\n",mode.c_str());
}
static const uint8_t *awake_bytes(){return controller_fixture::awake;}
static const uint8_t *asleep_bytes(){return controller_fixture::asleep;}
