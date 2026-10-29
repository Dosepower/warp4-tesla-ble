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
 unsigned wakes=0,wake_partial=0,statuses=0,statuses_after_wake=0,hellos=0,charges=0,frames=0,info_ignored=0;
 std::vector<uint32_t> after_wake_at;
 uint32_t committed=0,finished=0,update_unix=0,wake_tx_at=0;
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
 static const char *const WOKEN_NO_SOC="Tesla woke up but no SoC received in time; SoC not updated";
 constexpr uint32_t SPACING=TeslaBleSocPolicy::MIN_WAKE_SPACING_MS;
 const bool automatic_mode=mode.rfind("soc-auto",0)==0;
 // Manual scenario whose firmware post-wake follow-up must be able to reach the vehicle.
 const bool bound_mode=automatic_mode||mode=="soc-wake-followup";
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
  if(bound_mode){uint8_t transport[26]{};assert(profile_transport_record("5YJ3E1EA7KF000001","01:02:03:04:05:06",0,transport));
   assert(tesla_clean::KeyStorage::committed(store.save_transaction("ble_binding",transport,sizeof(transport))));}
 }
 host_rx_properties=32;
 HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;
 auto *p=m.find_profile(0);String error;
 // ---- synthetic vehicle sleep model ----
 bool car_awake=false,advertising=true;uint32_t wake_at=0,awake_after_wake=0;bool never_wakes=false;
 bool drop_on_status=false,drop_on_wake_fragment=false,drop_after_wake=false,next_is_wake=false,unplug_on_charge=false;
 unsigned wake_fragments=0;bool pending_drop=false;
 // SYNTHETIC model for the classified-failure path (live finding: a periodic poll
 // failed with a generic text; the real vehicle cause is unknown): the Infotainment
 // domain ignores every session request while info_silent is set.
 bool info_silent=false;
 // Synthetic waking infotainment (documented in TeslaBLE v6.2.0 include/vehicle.h:158-163; the
 // real duration is unknown): infotainment session requests are ignored for info_boot_ms after
 // the first one following a wake. 0 = answers at once (all pre-existing scenarios).
 uint32_t info_boot_ms=0,info_first=0;
 bool hold_on_hello=false,hold_replies=false; // test-only: keep a claimed automatic operation in flight
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
  if(s.hello){if(run)++run->hellos;
   if(s.domain==Domain::Infotainment&&info_silent){if(run)++run->info_ignored;return;} // synthetic silent infotainment session
   if(s.domain==Domain::Infotainment&&info_boot_ms&&wake_at){if(!info_first)info_first=millis();
    if(millis()-info_first<info_boot_ms){if(run)++run->info_ignored;return;}}
   replies.push_back(peer.hello(s));if(hold_on_hello&&run){hold_on_hello=false;hold_replies=true;}return;}
  if(s.pair){assert(false && "no pairing in SoC contracts");return;}
  if(s.domain==Domain::Infotainment){
   if(run)++run->charges;
   if(unplug_on_charge){unplug_on_charge=false;unplug();} // physical unplug while the SoC answer is in flight
   replies.push_back(peer.response(s,{controller_fixture::soc,sizeof(controller_fixture::soc)},++info));return;}
  if(s.plaintext.size()==sizeof(wake_plain)&&!memcmp(s.plaintext.data(),wake_plain,sizeof(wake_plain))){
   if(run){++run->wakes;run->wake_tx_at=millis();}wake_at=millis();if(drop_after_wake)pending_drop=true;
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
 bool charge_end_on_scan=false;  // test-only: EVSE reports charge end while the firmware's own scan runs
 int pulse_on=0;std::function<void()> pulse; // test-only: pause+resume while the own cycle scans (1) or selects (2)
 auto serve_auto=[&]{
  auto &ap=m.tesla->auto_poll;
  if(ap.phase==Phase::Scanning&&served_phase_ms!=ap.phase_ms){served_phase_ms=ap.phase_ms;scan_at.push_back(millis());
   if(mode=="soc-auto-charge-resume")soc_expect(!auto_run&&!(r&&(r->manual_poll_pending.load()||r->in_flight_started_ms!=0)),mode,
    "no scan while another SoC operation is in flight (no parallel poll)");
   if(pulse_on==1&&pulse){pulse_on=0;pulse();}
   if(charge_end_on_scan){charge_end_on_scan=false;host_evse.set_charger_state(CHARGER_STATE_READY_TO_CHARGE);}
   serve_scan(advertising);}
 };
 auto capture=[&](SocRun &x){
  x.finished=millis();
  x.error=m.tesla->state.get("last_error")->asString().c_str();x.soc=m.tesla->state.get("last_soc")->asFloat();
  x.update=m.tesla->state.get("last_update")->asUptime();x.update_unix=m.tesla->state.get("last_update_unix")->asUint();x.applied=ev.applied;
  x.outcome=m.tesla->state.get("poll_outcome")->asString().c_str();x.outcome_automatic=m.tesla->state.get("poll_outcome_automatic")->asBool();
  std::printf("SOC_RUN %s #%zu %s wakes=%u partial=%u statuses=%u after_wake=%u hellos=%u info_ignored=%u charges=%u applied=%d start=%u ms=%u outcome=%s auto_poll=%s error='%s'\n",mode.c_str(),runs.size(),
   x.automatic?"auto":"manual",x.wakes,x.wake_partial,x.statuses,x.statuses_after_wake,x.hellos,x.info_ignored,x.charges,x.applied,x.committed,x.finished-x.committed,x.outcome.c_str(),
   m.tesla->state.get("auto_poll")->asString().c_str(),x.error.c_str());std::fflush(stdout);
 };
 unsigned auto_settle=0;
 std::function<void()> before_main;
 size_t quiet_runs=0;uint32_t pulse_at=0;TeslaBleRequestIdentity approval{};
 int perturb=0;uint32_t perturb_seq0=0,recv_seen[4]{};uint32_t shorten_at=0,lengthen_at=0;size_t scans_mark=0;uint32_t charge_at[5]{};
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
  if(pulse_on==2&&m.tesla->auto_poll.phase==Phase::Selecting&&pulse){pulse_on=0;pulse();}
  if(!r||!r->connected.load()){replies.clear();drip.clear();drip_at=0;return;}
  if(ms!=20||hold_replies)return;
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
 auto sleep_again=[&]{car_awake=false;wake_at=0;next_is_wake=false;info_first=0;};
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
  p->get("poll_interval")->updateUint(300);soc_expect(save_profile(),mode,"interval save rejected"); // lowest selectable interval
  car_awake=true;
  until(runs_done(2));                                 // A1, A2: awake, no wake
  step([&]{sleep_again();});
  // No wake switch any more: the stored legacy wake_once=false must not block the
  // periodic request during active charging from waking (once, spaced).
  until(runs_done(3));                                 // A3: asleep while charging -> one wake -> SoC
  until([&]{if(auto_run||m.tesla->auto_poll.phase!=Phase::Idle||r->manual_poll_pending.load())return false;
   return save_profile();});                           // unrelated profile save keeps wake_once=false untouched
  step([&]{sleep_again();});
  until(runs_done(4));                                 // A4: asleep, 300 s after A3's wake (> spacing) -> wakes again
  step([&]{sleep_again();});
  until(runs_done(5));                                 // A5: asleep, >= spacing after A4's wake -> wakes again
  step([&]{sleep_again();awake_after_wake=0;});
  until([&]{return auto_run&&runs.size()==6&&run&&run->hellos>0;}); // A6 in flight
  step([&]{ // explicit actions while the background operation owns the worker
   m.tesla->profile_command.get("ev_index")->updateInt(0);error.clear();api.commands.at("tesla_vehicle/poll")(0,error);
   soc_expect(error.length()>0,mode,"manual poll accepted concurrently with automatic poll");
   ++request_id;soc_expect(!guided("prepare_poll").empty(),mode,"guided preparation accepted during automatic poll");
   auto &s=m.tesla->scan_command;s.get("vin")->updateString("5YJ3E1EA7KF000001");s.get("ev_index")->updateInt(0);s.get("ev_mac")->updateString("11:22:33:44:55:00");
   error.clear();api.commands.at("tesla_vehicle/scan")(0,error);soc_expect(error.length()>0,mode,"scan accepted during automatic poll");});
  until(runs_done(6));                                 // A6: 300 s after A5's wake -> wakes again (periodic never hits the spacing)
  step([&]{unplug();mark_frames=total_frames;});
  wait_ms(200000);
  step([&]{soc_expect(total_frames==mark_frames&&runs.size()==6,mode,"vehicle contact after unplug");
   soc_expect(m.tesla->state.get("auto_poll")->asString()=="off",mode,"scheduler not off after unplug");});
 } else if(mode=="soc-auto-stop") {      // not found, unplug mid-operation, profile switch, replug, unsynced clock
  p->get("poll_interval")->updateUint(300);soc_expect(save_profile(),mode,"interval save rejected");
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
 } else if(mode=="soc-auto-identity-origin") { // live review: retained BLE SoC/time bound to its identity, A -> B -> A
  rtc.base_seconds.store(1790000000);rtc.synced.store(true);
  p->get("poll_interval")->updateUint(300);soc_expect(save_profile(),mode,"interval save rejected");
  car_awake=true;
  until(runs_done(1));                                 // O1: A measured over BLE
  step([&]{soc_expect(std::isfinite(m.tesla->state.get("last_soc")->asFloat())&&m.tesla->state.get("last_update_unix")->asUint()>0&&
    m.tesla->state.get("profile_ev_index")->asInt()==0,mode,"A sample published with its receive time");
   ev.index=1;ev.mac=B;++ev.generation;mark_frames=total_frames; // profile B active, no B sample (shared SoC from another source)
   explicit_scan(m,1);String e;const bool selected=m.select_profile(1,false,e);if(!selected)std::printf("SOC_ORIGIN select error=%s\n",e.c_str());
   soc_expect(selected,mode,"user selection of profile B rejected");});
  wait_ms(150000);
  step([&]{std::printf("SOC_ORIGIN B profile=%d last_soc=%f unix=%u frames=%u\n",m.tesla->state.get("profile_ev_index")->asInt(),
    double(m.tesla->state.get("last_soc")->asFloat()),m.tesla->state.get("last_update_unix")->asUint(),total_frames-mark_frames);std::fflush(stdout);
   soc_expect(m.tesla->state.get("profile_ev_index")->asInt()==1,mode,"profile B published");
   soc_expect(std::isnan(m.tesla->state.get("last_soc")->asFloat())&&m.tesla->state.get("last_update_unix")->asUint()==0,mode,
    "A BLE value/receive time still published under profile B");
   soc_expect(m.tesla->state.get("last_update")->asUptime()==runs[0].update&&ev.applied==runs[0].applied&&total_frames==mark_frames,mode,
    "identity change re-dated the arrival mark, touched the shared SoC or contacted a vehicle");
   ev.index=0;ev.mac=A;++ev.generation;});             // back to A: published again, retained value not re-shown as fresh
  until([&]{return m.tesla->state.get("profile_ev_index")->asInt()==0;});
  step([&]{soc_expect(std::isnan(m.tesla->state.get("last_soc")->asFloat())&&m.tesla->state.get("last_update_unix")->asUint()==0,mode,
    "A -> B -> A re-showed the withdrawn value");});
  until(runs_done(2));                                 // O2: A's plug-in request still runs (mark not faked) and is shown again
 } else if(mode=="soc-auto-interval") {  // B1: a saved interval change applies to the pending wait
  // Periodic minimum 300 s: every API write below it is rejected through the production validator
  // and nothing is persisted; 300 s itself is accepted.
  {
   const size_t writes=api.written_configs.count("tesla_vehicle/profiles");
   for(const uint32_t below:{30u,60u,120u,299u}){p->get("poll_interval")->updateUint(below);
    soc_expect(!save_profile(),mode,"API interval below 300 s accepted");
    soc_expect(api.written_configs.count("tesla_vehicle/profiles")==writes,mode,"rejected interval persisted");}
   p->get("poll_interval")->updateUint(300);soc_expect(save_profile(),mode,"300 s interval rejected");
   // Legacy saved values below 300 s still restore (no legacy block, no data loss) and are
   // normalized to the effective 300 s minimum; every other field stays unchanged.
   // The host Config clone shares array-element children, so the probe runs the production
   // File (restore) validation on the loaded store itself, as the multi-profile restore test does.
   std::map<std::string,String> texts;std::map<std::string,uint32_t> nums;
   for(const char *k:{"ev_mac","vin","ble_address"})texts[k]=p->get(k)->asString();
   for(const char *k:{"ble_address_type","discovery_revision"})nums[k]=p->get(k)->asUint();
   const int ev_index=p->get("ev_index")->asInt();const bool enabled=p->get("enable")->asBool(),wake_once=p->get("wake_once")->asBool();
   for(const uint32_t legacy:{30u,60u,120u,299u,300u,301u,3600u}){
    p->get("poll_interval")->updateUint(legacy);
    p->get("wake_once")->updateBool(legacy==30u); // legacy flag carried unchanged
    soc_expect(m.validate_profiles(m.tesla->profiles,ConfigSource::File).length()==0,mode,"legacy interval restore rejected");
    soc_expect(p->get("poll_interval")->asUint()==(legacy<300u?300u:legacy),mode,"legacy interval not normalized to the 300 s minimum");
    for(const char *k:{"ev_mac","vin","ble_address"})soc_expect(p->get(k)->asString()==texts[k],mode,"restore changed another field");
    for(const char *k:{"ble_address_type","discovery_revision"})soc_expect(p->get(k)->asUint()==nums[k],mode,"restore changed another field");
    soc_expect(p->get("ev_index")->asInt()==ev_index&&p->get("enable")->asBool()==enabled&&
     p->get("wake_once")->asBool()==(legacy==30u),mode,"restore changed another field");
    soc_expect(!m.tesla->legacy_blocked,mode,"legacy interval blocked the profile store");
   }
   p->get("wake_once")->updateBool(wake_once);p->get("poll_interval")->updateUint(300);
  }
  p->get("poll_interval")->updateUint(3600);soc_expect(save_profile(),mode,"interval save rejected");
  car_awake=true;
  until(runs_done(1));                                 // I1 at the plug-in settle delay
  step([&]{p->get("poll_interval")->updateUint(300);soc_expect(save_profile(),mode,"shortened interval rejected");shorten_at=millis();});
  until(runs_done(2));                                 // I2 300 s after I1, not after the old 3600 s
  wait_ms(10000);
  step([&]{p->get("poll_interval")->updateUint(600);soc_expect(save_profile(),mode,"lengthened interval rejected");
   lengthen_at=millis();scans_mark=scan_at.size();});
  until(runs_done(3));                                 // I3 600 s after I2, not after the old 300 s
 } else if(mode=="soc-auto-recvtime") {  // B2: receive time fixed by the worker, not at delayed dispatch
  rtc.base_seconds.store(1790000000);rtc.synced.store(true);
  p->get("poll_interval")->updateUint(300);soc_expect(save_profile(),mode,"interval save rejected");
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
 } else if(mode=="soc-info-silent") {     // classified failure through the real path, then recovery
  request([&]{car_awake=true;});                      // S1 SoC
  request([&]{info_silent=true;});                    // S2 Infotainment never answers its session request
  step([&]{const std::string shown=m.tesla->state.get("last_error")->asString().c_str();
   soc_expect(shown.rfind("Tesla SoC request: session setup unanswered (info_auth",0)==0,mode,
    "S2 last_error names the actual terminal bound/stage, not the generic 'authenticated SoC request failed'");
   soc_expect(m.tesla->state.has("poll_failure")&&m.tesla->state.has("poll_stage"),mode,"structured poll_failure/poll_stage state published");
   soc_expect(m.tesla->state.get("poll_failure")->asString()=="challenge_cap"&&
    std::string(m.tesla->state.get("poll_stage")->asString().c_str()).rfind("info_auth",0)==0&&m.tesla->state.get("poll_vcsec_answered")->asBool()&&
    !m.tesla->state.get("poll_info_answered")->asBool()&&m.tesla->state.get("poll_elapsed_ms")->asUint()>0,mode,
    "structured classification published with the failed outcome");
   std::printf("SOC_CLASSIFIED failure=%s stage=%s elapsed=%u attempts=%u timeouts=%u frames=%u\n",m.tesla->state.get("poll_failure")->asString().c_str(),
    m.tesla->state.get("poll_stage")->asString().c_str(),m.tesla->state.get("poll_elapsed_ms")->asUint(),m.tesla->state.get("poll_attempts")->asUint(),
    m.tesla->state.get("poll_timeouts")->asUint(),m.tesla->state.get("poll_frames")->asUint());});
  request([&]{info_silent=false;});                   // S3 recovers
 } else if(mode=="soc-auto-post-pair") {  // one automatic request after a NEW authenticated pairing, idle
  rtc.base_seconds.store(1790000000);rtc.synced.store(true);
  host_evse.set_charger_state(CHARGER_STATE_READY_TO_CHARGE);
  p->get("poll_interval")->updateUint(300);soc_expect(save_profile(),mode,"interval save rejected");
  car_awake=true;
  until(runs_done(1));                                 // P1 plug-in request
  step([&]{mark_frames=total_frames;scans_mark=scan_at.size();
   // White-box precondition: the plug-in-time scheduler denial seen live before pairing.
   m.tesla->state.get("auto_poll")->updateString("not_paired");});
  wait_ms(200000);
  step([&]{soc_expect(runs.size()==1&&total_frames==mark_frames&&scan_at.size()==scans_mark,mode,"idle: nothing before the approval edge");
   soc_expect(m.tesla->state.get("auto_poll")->asString()=="not_paired",mode,"precondition kept while idle");
   // Authenticated approval as the pairing worker publishes it (TeslaBleCommandOutcome::publish_approval
   // with a request armed on the current session); main consumes it in the real Kind::PairApproved branch.
   approval=r->session.arm(r->epoch,TeslaBleIdentity{ev.generation,ev.index,ev.mac},ev.index);
   soc_expect(approval.request!=0,mode,"precondition: approval request armed on the connected current session");
   r->command_outcome.publish_approval(approval);pulse_at=millis();main_pass();
   soc_expect(m.tesla->state.get("paired")->asBool(),mode,"approval consumed by the production branch");});
  until([&]{if(runs_done(2)())return true;                // exactly one post-pairing request
   soc_expect(millis()-pulse_at<(TeslaBleDiscovery::SCAN_SECONDS+60)*1000,mode,"no automatic SoC request after the new authenticated pairing approval");
   return false;});
  step([&]{soc_expect(runs[1].automatic&&runs[1].committed-pulse_at<=(TeslaBleDiscovery::SCAN_SECONDS+5)*1000,mode,"post-pairing request promptly after the edge");
   mark_frames=total_frames;scans_mark=scan_at.size();});
  wait_ms(700000);                                     // idle: no periodic polling afterwards
  step([&]{soc_expect(runs.size()==2&&total_frames==mark_frames&&scan_at.size()==scans_mark,mode,"post-pairing request is one-shot; no idle polling");
   soc_expect(m.tesla->state.get("auto_poll")->asString()=="not_charging",mode,"idle state after the post-pairing request");
   // Replayed approval of the already consumed request (repeated paired state): never a request.
   r->command_outcome.publish_approval(approval);main_pass();
   // Foreign identity (approval armed for a replaced plug-in session): rejected by the session gate.
   soc_expect(!r->session.arm(r->epoch,TeslaBleIdentity{ev.generation+1,ev.index,ev.mac},ev.index).request,mode,"foreign identity cannot arm");});
  wait_ms(200000);
  step([&]{soc_expect(runs.size()==2&&total_frames==mark_frames&&scan_at.size()==scans_mark,mode,"replayed approval must not poll");
   // Plain repeated paired state without a new edge (reload): nothing.
   m.tesla->state.get("auto_poll")->updateString("not_paired");});
  request([&]{});                                      // M1 manual success supersedes a stale not_paired
  step([&]{soc_expect(m.tesla->state.get("auto_poll")->asString()!="not_paired",mode,"authenticated SoC withdraws stale not_paired");});
 } else if(mode=="soc-auto-charge-idle") { // plugged in, never charging: one plug-in request, then silence
  rtc.base_seconds.store(1790000000);rtc.synced.store(true);
  host_evse.set_charger_state(CHARGER_STATE_READY_TO_CHARGE);
  p->get("poll_interval")->updateUint(300);             // stored legacy wake_once stays false
  soc_expect(save_profile(),mode,"interval save rejected");
  sleep_again();awake_after_wake=0;                    // asleep at plug-in: the plug-in request may wake
  until(runs_done(1));                                 // P1: plug-in request (one permitted wake)
  step([&]{sleep_again();mark_frames=total_frames;scans_mark=scan_at.size();});
  wait_ms(600000);                                     // two intervals while plugged in, not charging
  step([&]{soc_expect(total_frames==mark_frames&&scan_at.size()==scans_mark&&runs.size()==1,mode,
    "idle: no automatic request, scan or wake after the plug-in request");
   soc_expect(m.tesla->state.get("auto_poll")->asString()=="not_charging",mode,"idle shown as not charging");
   soc_expect(m.tesla->state.get("last_update_unix")->asUint()==runs[0].update_unix&&
    m.tesla->state.get("last_update")->asUptime()==runs[0].update&&ev.applied==runs[0].applied,mode,"idle: last value and receive time unchanged");});
  request([&]{sleep_again();awake_after_wake=0;});     // M1 manual while idle: wakes on demand
  step([&]{mark_frames=total_frames;scans_mark=scan_at.size();});
  wait_ms(150000);
  step([&]{soc_expect(total_frames==mark_frames&&scan_at.size()==scans_mark&&runs.size()==2,mode,
    "manual idle request must not restart background polling");});
  request([&]{sleep_again();awake_after_wake=0;});     // M2 repeated manual, after the wake spacing
 } else if(mode=="soc-auto-charge-cycle") { // charge start, periodic, PV pause, resume, end, replug
  rtc.base_seconds.store(1790000000);rtc.synced.store(true);
  host_evse.set_charger_state(CHARGER_STATE_READY_TO_CHARGE); // plugged in, vehicle not requesting
  p->get("poll_interval")->updateUint(300);soc_expect(save_profile(),mode,"interval save rejected");
  car_awake=true;
  until(runs_done(1));                                 // C1 plug-in request
  wait_ms(200000);
  step([&]{soc_expect(runs.size()==1&&scan_at.size()==1&&total_frames==runs[0].frames,mode,"no request before charging starts");
   host_evse.set_charger_state(CHARGER_STATE_CHARGING);charge_at[0]=millis();});
  until(runs_done(2));                                 // C2 at the real charge start
  until(runs_done(3));until([&]{return auto_run&&runs.size()==4&&run&&run->hellos>0;}); // C3 periodic, C4 in flight
  step([&]{host_evse.set_charger_state(CHARGER_STATE_WAITING_FOR_RELEASE);charge_at[1]=millis();}); // PV pause mid-operation
  until(runs_done(4));                                 // C4 drains, publishes its real result
  step([&]{mark_frames=total_frames;scans_mark=scan_at.size();});
  wait_ms(300000);
  step([&]{soc_expect(total_frames==mark_frames&&scan_at.size()==scans_mark&&runs.size()==4,mode,"pause: no request, scan or wake");
   soc_expect(m.tesla->state.get("auto_poll")->asString()=="not_charging",mode,"pause shown as not charging");
   soc_expect(m.tesla->state.get("last_update_unix")->asUint()==runs[3].update_unix&&m.tesla->state.get("last_update")->asUptime()==runs[3].update&&
    ev.applied==runs[3].applied&&m.tesla->state.get("last_soc")->asFloat()==72.0f&&m.tesla->state.get("poll_outcome")->asString()=="ok",mode,
    "pause: last SoC kept with its original receive time, nothing re-stamped");
   host_evse.set_charger_state(CHARGER_STATE_CHARGING);charge_at[2]=millis();});
  until(runs_done(5));                                 // C5 at resume
  step([&]{host_evse.set_charger_state(CHARGER_STATE_WAITING_FOR_RELEASE);});
  wait_ms(10000);
  step([&]{host_evse.set_charger_state(CHARGER_STATE_CHARGING);charge_at[3]=millis();});
  until(runs_done(6));                                 // C6: resume shortly after C5, before the interval is due
  step([&]{charge_end_on_scan=true;mark_frames=total_frames;scans_mark=scan_at.size();});
  until([&]{return !charge_end_on_scan&&m.tesla->auto_poll.phase==Phase::Idle;}); // charge ends during the next scan
  wait_ms(300000);
  step([&]{soc_expect(total_frames==mark_frames&&scan_at.size()==scans_mark+1&&runs.size()==6,mode,
    "charge end during the scan: no vehicle contact, no further scan");
   soc_expect(m.tesla->state.get("auto_poll")->asString()=="not_charging",mode,"end shown as not charging");
   unplug();});
  wait_ms(2000);
  step([&]{ev.index=0;ev.mac=A;++ev.generation;charge_at[4]=millis();}); // replug: new physical session
  until(runs_done(7));                                 // C7 plug-in request of the new session
  step([&]{scans_mark=scan_at.size();mark_frames=total_frames;});
  wait_ms(200000);
  step([&]{soc_expect(total_frames==mark_frames&&scan_at.size()==scans_mark&&runs.size()==7,mode,"replug: exactly one plug-in request");});
 } else if(mode=="soc-auto-charge-resume") { // charge pause+resume edges while an automatic operation is already claimed
  rtc.base_seconds.store(1790000000);rtc.synced.store(true);
  host_evse.set_charger_state(CHARGER_STATE_READY_TO_CHARGE);
  p->get("poll_interval")->updateUint(3600);soc_expect(save_profile(),mode,"interval save rejected");
  car_awake=true;
  // Charger-state edges inside claimed operation n (worker already talking to the vehicle,
  // replies held), each observed by the main task before the next one.
  auto edges_in_flight=[&](size_t n,std::vector<bool> edges,bool release){
   until([&,n]{return hold_replies&&auto_run&&runs.size()==n;});
   for(const bool on:edges){
    step([&,n,on]{soc_expect(hold_replies&&auto_run&&runs.size()==n&&run&&run->charges==0&&m.tesla->auto_poll.phase==Phase::Polling,mode,
      "edge must hit the already claimed, unfinished operation");
     host_evse.set_charger_state(on?CHARGER_STATE_CHARGING:CHARGER_STATE_WAITING_FOR_RELEASE);});
    until([&,on]{return m.tesla->auto_poll.charging==on;});
   }
   if(release)step([&]{hold_replies=false;});
  };
  auto quiet=[&](uint32_t ms,const char *what){ // no scan, no frame, no new operation
   step([&]{mark_frames=total_frames;scans_mark=scan_at.size();quiet_runs=runs.size();});
   wait_ms(ms);
   step([&,what]{soc_expect(total_frames==mark_frames&&scan_at.size()==scans_mark&&runs.size()==quiet_runs&&!auto_run,mode,what);});
  };
  auto next_after=[&](size_t n,uint32_t lo,uint32_t hi,const char *what){ // operation n vs. end of n-1, bounded wait
   until([&,n,hi]{return runs_done(n)()||(runs.size()==n-1&&!auto_run&&!run&&millis()-runs[n-2].finished>hi+5000);});
   step([&,n,lo,hi,what]{const uint32_t gap=runs.size()>=n?runs[n-1].committed-runs[n-2].finished:0;
    std::printf("SOC_RESUME %s runs=%zu gap=%u\n",what,runs.size(),gap);std::fflush(stdout);
    soc_expect(runs.size()==n&&gap>=lo&&gap<=hi,mode,what);});
  };
  auto pause_idle=[&]{step([&]{host_evse.set_charger_state(CHARGER_STATE_WAITING_FOR_RELEASE);});
   until([&]{return !m.tesla->auto_poll.charging&&m.tesla->auto_poll.phase==Phase::Idle;});};
  pulse=[&]{ // pause, observed; resume, observed - inside the running cycle
   host_evse.set_charger_state(CHARGER_STATE_WAITING_FOR_RELEASE);main_pass();
   soc_expect(!m.tesla->auto_poll.charging,mode,"pause observed inside the cycle");
   host_evse.set_charger_state(CHARGER_STATE_CHARGING);main_pass();
   soc_expect(m.tesla->auto_poll.charging,mode,"resume observed inside the cycle");pulse_at=millis();};
  until(runs_done(1));                                 // R1 plug-in request, not charging
  // A (reported defect): charge start -> R2; PV pause AND resume both while R2 is claimed; R2 then fails.
  step([&]{hold_on_hello=true;drop_on_status=true;host_evse.set_charger_state(CHARGER_STATE_CHARGING);});
  edges_in_flight(2,{false,true},true);
  until(runs_done(2));
  step([&]{soc_expect(runs[1].outcome=="failed"&&runs[1].applied==runs[0].applied,mode,"A: R2 failed after the edges");});
  next_after(3,30000,36000,"A: resume inside a claimed operation that then failed: one new request after the minimum gap, not after 3600 s");
  step([&]{soc_expect(runs[2].outcome=="ok"&&runs[2].applied==runs[0].applied+1,mode,"A: resume request delivers a SoC");});
  quiet(300000,"A: resume demand consumed once, no re-trigger");
  // B: same edges, but the old operation (claimed before the resume) succeeds: it does not serve the resume.
  pause_idle();
  step([&]{hold_on_hello=true;host_evse.set_charger_state(CHARGER_STATE_CHARGING);charge_at[0]=millis();});
  edges_in_flight(4,{false,true},true);
  until(runs_done(4));
  step([&]{soc_expect(runs[3].committed-charge_at[0]>=5000&&runs[3].committed-charge_at[0]<=10000&&runs[3].outcome=="ok",mode,
    "B: prompt request at resume, succeeds");});
  next_after(5,30000,36000,"B: an operation claimed before the resume does not satisfy it: one request after the minimum gap");
  quiet(300000,"B: no re-trigger");
  // C: pause+resume while the cycle's own scan runs (before vehicle contact): the request handed over after them serves it.
  pause_idle();
  step([&]{pulse_on=1;host_evse.set_charger_state(CHARGER_STATE_CHARGING);charge_at[1]=millis();});
  until(runs_done(6));
  step([&]{soc_expect(pulse_on==0&&pulse_at>charge_at[1]&&runs[5].committed>=pulse_at&&runs[5].committed-charge_at[1]<=40000&&runs[5].outcome=="ok",mode,
    "C: edges during the scan: one request after them");});
  quiet(300000,"C: exactly one request for the resume, no re-trigger");
  // D: the same while the cycle selects the profile (before the request is handed to the worker).
  pause_idle();
  step([&]{pulse_on=2;host_evse.set_charger_state(CHARGER_STATE_CHARGING);charge_at[2]=millis();});
  until(runs_done(7));
  step([&]{soc_expect(pulse_on==0&&pulse_at>charge_at[2]&&runs[6].committed>=pulse_at&&runs[6].committed-charge_at[2]<=45000&&runs[6].outcome=="ok",mode,
    "D: edges during profile selection: one request after them");});
  quiet(300000,"D: exactly one request for the resume, no re-trigger");
  // E: several edges inside the claimed operation (ending charging), operation fails: exactly one new request.
  pause_idle();
  step([&]{hold_on_hello=true;drop_on_status=true;host_evse.set_charger_state(CHARGER_STATE_CHARGING);});
  edges_in_flight(8,{false,true,false,true},true);
  until(runs_done(8));
  step([&]{soc_expect(runs[7].outcome=="failed",mode,"E: R8 failed");});
  next_after(9,30000,36000,"E: several edges: exactly one new request after the minimum gap");
  quiet(300000,"E: no re-trigger after several edges");
  // F: pause, resume, pause again inside the claimed operation, which fails: nothing while paused.
  pause_idle();
  step([&]{hold_on_hello=true;drop_on_status=true;host_evse.set_charger_state(CHARGER_STATE_CHARGING);});
  edges_in_flight(10,{false,true,false},true);
  until(runs_done(10));
  step([&]{soc_expect(runs[9].outcome=="failed",mode,"F: R10 failed");});
  quiet(300000,"F: paused again: no request, scan or wake");
  step([&]{soc_expect(m.tesla->state.get("auto_poll")->asString()=="not_charging",mode,"F: shown as not charging");
   host_evse.set_charger_state(CHARGER_STATE_CHARGING);charge_at[3]=millis();});
  until(runs_done(11));
  step([&]{soc_expect(runs[10].committed-charge_at[3]>=5000&&runs[10].committed-charge_at[3]<=10000&&runs[10].outcome=="ok",mode,
    "F: next real resume requests promptly");});
  // G: pause+resume inside the claimed operation, then unplug: no further contact; replug = only the plug-in request.
  pause_idle();
  step([&]{hold_on_hello=true;host_evse.set_charger_state(CHARGER_STATE_CHARGING);});
  edges_in_flight(12,{false,true},false);
  step([&]{unplug();hold_replies=false;});
  until(runs_done(12));
  until([&]{return !r->manual_poll_pending.load()&&!r->auto_poll_requested.load()&&r->in_flight_started_ms==0;}); // retired operation drained
  quiet(100000,"G: unplugged: no pending resume request survives");
  step([&]{soc_expect(m.tesla->state.get("auto_poll")->asString()=="off"&&ev.applied==runs[10].applied,mode,"G: off, nothing published");
   ev.index=0;ev.mac=A;++ev.generation;charge_at[4]=millis();});
  until(runs_done(13));
  step([&]{std::printf("SOC_RESUME G replug=%u\n",runs[12].committed-charge_at[4]);
   soc_expect(runs[12].committed-charge_at[4]>=15000&&runs[12].committed-charge_at[4]<=25000&&runs[12].outcome=="ok",mode,
    "G: replug while charging: the plug-in request");});
  quiet(200000,"G: replug: exactly one plug-in request");
} else if(mode=="soc-wake-followup") { // manual wake, infotainment slower than one operation -> one no-wake follow-up
 host_evse.set_charger_state(CHARGER_STATE_READY_TO_CHARGE); // plugged in, idle: no periodic requests
 p->get("poll_interval")->updateUint(3600);
 request([&]{sleep_again();awake_after_wake=1000;info_boot_ms=60000;}); // M1: woken, infotainment silent for 60 s
 until(runs_done(2));                                 // F2: firmware follow-up without wake
 step([&]{mark_frames=total_frames;scans_mark=scan_at.size();quiet_runs=runs.size();});
 wait_ms(300000);
 step([&]{soc_expect(total_frames==mark_frames&&scan_at.size()==scans_mark&&runs.size()==quiet_runs,mode,
   "follow-up consumed once: no further request, scan or wake while idle");
  soc_expect(m.tesla->state.get("auto_poll")->asString()=="not_charging",mode,"idle after the follow-up shown as not charging");});
} else if(mode=="soc-auto-wake-followup") { // charging: woken periodic op -> follow-up; woken op then pause -> nothing
 rtc.base_seconds.store(1790000000);rtc.synced.store(true);
 p->get("poll_interval")->updateUint(600);soc_expect(save_profile(),mode,"interval save rejected"); // wake_once stays false
 sleep_again();awake_after_wake=1000;info_boot_ms=30000; // longer than one operation's session budget, shorter than the follow-up delay
 until(runs_done(1));                                 // W1 plug-in request while charging: wakes, infotainment silent
 until(runs_done(2));                                 // W2 follow-up: no wake, infotainment ready
 step([&]{mark_frames=total_frames;scans_mark=scan_at.size();quiet_runs=runs.size();});
 wait_ms(100000);
 step([&]{soc_expect(total_frames==mark_frames&&runs.size()==quiet_runs,mode,"no extra request after the follow-up");
  host_evse.set_charger_state(CHARGER_STATE_WAITING_FOR_RELEASE);});
 until([&]{return !m.tesla->auto_poll.charging&&m.tesla->auto_poll.phase==Phase::Idle;});
 step([&]{sleep_again();info_boot_ms=60000;host_evse.set_charger_state(CHARGER_STATE_CHARGING);charge_at[0]=millis();});
 until(runs_done(3));                                 // W3 at resume: wakes (spacing elapsed), infotainment silent
 step([&]{host_evse.set_charger_state(CHARGER_STATE_WAITING_FOR_RELEASE);}); // PV pause right after the woken op
 step([&]{mark_frames=total_frames;scans_mark=scan_at.size();quiet_runs=runs.size();});
 wait_ms(300000);
 step([&]{soc_expect(total_frames==mark_frames&&scan_at.size()==scans_mark&&runs.size()==quiet_runs,mode,
   "pause: no follow-up, request, scan or wake after an automatic woken operation");
  soc_expect(m.tesla->state.get("auto_poll")->asString()=="not_charging",mode,"pause shown as not charging");});
 } else if(mode=="soc-auto-initial-retry") {
  // Handled below without a worker: the vehicle is never found, so none starts.
 } else { assert(false && "unknown soc mode"); }
 if(mode=="soc-auto-initial-retry") { // bounded plug-in retries, then silence until charging starts
  host_evse.set_charger_state(CHARGER_STATE_READY_TO_CHARGE);advertising=false;
  p->get("poll_interval")->updateUint(300);soc_expect(save_profile(),mode,"interval save rejected");
  auto_t0=millis();
  auto run_main=[&](uint32_t ms){const uint32_t end=millis()+ms;while(int32_t(millis()-end)<0){clock_ms.fetch_add(20);m.loop();serve_auto();}};
  run_main(900000);
  std::printf("SOC_RETRY scans=%zu first=%u gap1=%u gap2=%u\n",scan_at.size(),scan_at.size()?scan_at[0]-auto_t0:0,
   scan_at.size()>1?scan_at[1]-scan_at[0]:0,scan_at.size()>2?scan_at[2]-scan_at[1]:0);
  soc_expect(scan_at.size()==3,mode,"plug-in request: exactly three bounded attempts, no endless retry");
  soc_expect(scan_at[0]-auto_t0>=15000&&scan_at[0]-auto_t0<=25000,mode,"first attempt after the plug-in settle delay");
  soc_expect(scan_at[1]-scan_at[0]>=30000&&scan_at[1]-scan_at[0]<=31000,mode,"first backoff 30 s");
  soc_expect(scan_at[2]-scan_at[1]>=120000&&scan_at[2]-scan_at[1]<=121000,mode,"second backoff 120 s");
  host_evse.set_charger_state(CHARGER_STATE_CHARGING);const uint32_t charge_on=millis();
  run_main(310000);                                     // charge start + one interval
  soc_expect(scan_at.size()==5&&scan_at[3]-charge_on>=5000&&scan_at[3]-charge_on<=6000&&scan_at[4]-scan_at[3]>=300000&&scan_at[4]-scan_at[3]<=301000,
   mode,"charging: prompt request at charge start (not delayed by the 300 s minimum), then the configured interval");
  soc_expect(total_frames==0&&runs.empty()&&!m.tesla->runtime_owner.get(),mode,"not found: no vehicle contact, no worker");
  assert_charging_untouched();std::printf("SOC_WAKE_PASS %s\n",mode.c_str());return;
 }
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
  expect_soc(runs[0],nullptr,"car awake 9 s after the wake must deliver SoC within its phase budgets (90 s absolute bound)");
  soc_expect(runs[0].finished-runs[0].committed<tesla_clean::production_soc_timing.total_ms+5000,mode,"bounded by the absolute request bound");
 } else if(mode=="soc-wake-never") {
  // R1's wake without SoC arms one firmware follow-up without wake (deterministic order in
 // this simulation: it runs when R2's explicit scan result expires, before R3).
 soc_expect(runs.size()==4&&runs[0].wakes==1&&runs[0].charges==0&&runs[0].applied==0,mode,"R1 one wake, no SoC");
 soc_expect(runs[2].automatic&&runs[2].wakes==0&&runs[2].wake_partial==0&&runs[2].outcome=="asleep_wake_disabled"&&runs[2].error.empty(),mode,
  "follow-up after an unconfirmed wake: never wakes, asleep reported without error banner");
 expect_no_new_soc(runs[2],runs[1],"follow-up: no new SoC while asleep");
 runs.erase(runs.begin()+2);
  soc_expect(runs[0].statuses_after_wake<=40,mode,"R1 bounded post-wake status polls");
  // Never confirmed awake: honest "wake sent", never "woken".
  soc_expect(runs[0].error==WAKE_NO_SOC&&runs[0].outcome=="wake_sent_no_soc",mode,"R1 exposed wake without awake evidence: 'wake sent' outcome");
  // Awake wait is its own 25 s phase from the wake exposure (reference
  // AUTH_RESPONSE_TIMEOUT, WakePollCap), inside the 90 s absolute bound. Boundary:
  // not cut before the phase (statuses keep coming up to its end), not extended
  // past it (ends within one 1 s transport tick + one main pass after it).
  {const uint32_t phase=tesla_clean::session_auth_phase_ms,end=runs[0].finished-runs[0].wake_tx_at,last=runs[0].after_wake_at.empty()?0:runs[0].after_wake_at.back()-runs[0].wake_tx_at;
   std::printf("SOC_WAKE_NEVER_PHASE wake_at=+%u end=wake+%u last_status=wake+%u statuses_after_wake=%u\n",runs[0].wake_tx_at-runs[0].committed,end,last,runs[0].statuses_after_wake);std::fflush(stdout);
   soc_expect(runs[0].wake_tx_at>runs[0].committed&&end+tesla_clean::Vehicle::transport_retry_interval_ms>=phase&&end<=phase+1500,mode,
    "R1 awake wait ends at its own 25 s phase after the wake (WakePollCap), not at a shared 40 s request deadline");
   soc_expect(last+2*tesla_clean::Vehicle::transport_retry_interval_ms>=phase&&last<phase,mode,"R1 post-wake status probes continue up to (not past) the awake phase end");
   soc_expect(runs[0].finished-runs[0].committed<tesla_clean::production_soc_timing.total_ms,mode,"R1 inside the 90 s absolute bound");}
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
  soc_expect(runs[0].outcome=="wake_sent_no_soc",mode,"R1 partial wake + link loss: visible exposed-wake outcome, no awake claim");
  soc_expect(runs[1].wakes==0&&runs[1].wake_partial==0,mode,"R2: partially sent wake counts as exposed; never resent within the spacing");
  soc_expect(runs[1].error==ASLEEP_LIMITED,mode,"R2 clear outcome");expect_no_new_soc(runs[1],runs[0],"R2 no new SoC");
 } else if(mode=="soc-wake-drop-waitwake") {
  soc_expect(runs.size()==2&&runs[0].wakes==1&&runs[0].applied==0,mode,"R1 one wake, no SoC");
  soc_expect(runs[0].outcome=="wake_sent_no_soc",mode,"R1 wake + link loss: visible exposed-wake outcome, no awake claim");
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
   soc_expect(gap>=300000&&gap<=300000+5000,mode,"configured 300 s profile interval between operations");}
  expect_soc(runs[0],nullptr,"A1 SoC");soc_expect(runs[0].wakes==0,mode,"A1 awake: no wake");
  soc_expect(runs[0].update_unix>=1790000000+runs[0].committed/1000&&runs[0].update_unix<=1790000000+runs[0].finished/1000,mode,"A1 receive time is the synchronized wall clock at reception");
  expect_soc(runs[1],&runs[0],"A2 SoC");soc_expect(runs[1].update_unix>runs[0].update_unix,mode,"A2 newer receive time");
  const auto &saved=api.written_configs.at("tesla_vehicle/profiles");
  soc_expect(!const_cast<Config&>(saved).get("entries")->get(size_t{0})->get("wake_once")->asBool()&&
   const_cast<Config&>(saved).get("entries")->get(size_t{0})->get("poll_interval")->asUint()==300,mode,"interval persisted, legacy wake_once left false (no rewrite)");
  soc_expect(runs[2].wakes==1,mode,"A3 periodic request while charging wakes despite stored wake_once=false");
  expect_soc(runs[2],&runs[1],"A3 SoC after wake");
  // The 300 s minimum exceeds the unchanged 120 s wake spacing: each periodic request while
  // charging may wake once again (spacing itself is covered by the manual soc-* modes).
  for(int i=3;i<6;++i){soc_expect(runs[i].wakes==1&&runs[i].error.empty(),mode,"A4-A6 periodic wake after the spacing, no error banner");
   expect_soc(runs[i],&runs[i-1],"A4-A6 SoC after wake");}
  for(const auto &x:runs)soc_expect(x.wakes+x.wake_partial<=1,mode,"at most one wake per operation");
 } else if(mode=="soc-auto-stop") {
  soc_expect(runs.size()==2&&not_found_at!=0,mode,"not-found cycle, then two automatic operations");
  // The not-found cycle was the plug-in request: bounded retry after the first 30 s backoff.
  soc_expect(runs[0].committed-not_found_at>=30000&&runs[0].committed-not_found_at<=35000,mode,"plug-in retry after not-found waits the 30 s backoff");
  soc_expect(runs[0].charges==1&&runs[0].applied==0&&runs[0].update==0&&runs[0].update_unix==0&&runs[0].outcome!="ok",mode,"S1 result after unplug never published as new");
  expect_soc(runs[1],nullptr,"S2 SoC after replug");
  soc_expect(runs[1].update>0&&runs[1].update_unix==0,mode,"S2 unsynchronized clock: no invented wall-clock time");
 } else if(mode=="soc-auto-identity-origin") {
  soc_expect(runs.size()==2,mode,"A, then A again after B");
  expect_soc(runs[0],nullptr,"O1 SoC");expect_soc(runs[1],&runs[0],"O2 SoC after A -> B -> A");
  soc_expect(std::isfinite(runs[1].soc)&&runs[1].update>runs[0].update&&runs[1].update_unix>=runs[0].update_unix&&runs[1].update_unix>0,mode,
   "new A sample shown with its own receive time");
 } else if(mode=="soc-auto-interval") {
  soc_expect(runs.size()==3,mode,"three automatic operations");
  for(const auto &x:runs)soc_expect(x.automatic&&x.wakes==0,mode,"firmware-initiated, awake, no wake");
  expect_soc(runs[0],nullptr,"I1 SoC");expect_soc(runs[1],&runs[0],"I2 SoC");expect_soc(runs[2],&runs[1],"I3 SoC");
  soc_expect(runs[0].committed-auto_t0>=15000&&runs[0].committed-auto_t0<=20000,mode,"first cycle keeps the plug-in settle delay");
  const auto gap1=runs[1].committed-runs[0].finished,gap2=runs[2].committed-runs[1].finished;
  std::printf("SOC_INTERVAL gap_after_shorten=%u gap_after_lengthen=%u scans=%zu\n",gap1,gap2,scan_at.size());
  soc_expect(shorten_at>=runs[0].finished&&gap1>=300000&&gap1<=300000+5000,mode,"shortened 3600->300 s must apply without waiting for the old 3600 s");
  soc_expect(lengthen_at>runs[1].finished&&gap2>=600000&&gap2<=600000+5000,mode,"lengthened 300->600 s must hold the new interval");
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
 } else if(mode=="soc-info-silent") {
  soc_expect(runs.size()==3,mode,"three manual requests");
  expect_soc(runs[0],nullptr,"S1 SoC");
  std::printf("SOC_CLASSIFIED S2 error='%s' ms=%u\n",runs[1].error.c_str(),runs[1].finished-runs[1].committed);
  soc_expect(runs[1].outcome=="failed"&&runs[1].charges==0&&runs[1].wakes==0&&runs[1].info_ignored>=1,mode,"S2 failed, no wake, no charge query");
  expect_no_new_soc(runs[1],runs[0],"S2 keeps S1's value and receive time");
  soc_expect(runs[1].error.rfind("Tesla SoC request: session setup unanswered (info_auth",0)==0,mode,
   "S2 reports the actual terminal bound/stage instead of the generic 'authenticated SoC request failed'");
  expect_soc(runs[2],&runs[0],"S3 recovers with a SoC");
 } else if(mode=="soc-auto-post-pair") {
  soc_expect(runs.size()==3&&runs[0].automatic&&runs[1].automatic&&!runs[2].automatic,mode,"plug-in, one post-pairing, one manual");
  expect_soc(runs[0],nullptr,"P1 SoC");expect_soc(runs[1],&runs[0],"post-pairing SoC");expect_soc(runs[2],&runs[1],"M1 SoC");
 } else if(mode=="soc-auto-charge-idle") {
  soc_expect(runs.size()==3&&runs[0].automatic&&!runs[1].automatic&&!runs[2].automatic,mode,"one plug-in request, two manual requests");
  soc_expect(runs[0].committed-auto_t0>=15000&&runs[0].committed-auto_t0<=20000,mode,"plug-in request after the settle delay");
  soc_expect(runs[0].wakes==1,mode,"plug-in request may wake once (stored wake_once=false does not block it)");expect_soc(runs[0],nullptr,"P1 SoC");
  soc_expect(runs[1].wakes==1,mode,"M1 manual idle request wakes on demand");expect_soc(runs[1],&runs[0],"M1 SoC");
  soc_expect(runs[2].wakes==1,mode,"M2 repeated manual request after the spacing wakes again");expect_soc(runs[2],&runs[1],"M2 SoC");
  soc_expect(runs[1].update>runs[0].update&&runs[2].update_unix>runs[1].update_unix,mode,"manual results carry newer receive times");
 } else if(mode=="soc-auto-charge-cycle") {
  soc_expect(runs.size()==7,mode,"seven automatic operations");
  for(const auto &x:runs)soc_expect(x.automatic&&x.wakes==0,mode,"firmware-initiated, awake, no wake");
  for(int i=0;i<7;++i)expect_soc(runs[i],i?&runs[i-1]:nullptr,"charge-cycle SoC");
  std::printf("SOC_CHARGE c1=%u c2=%u c3gap=%u c4gap=%u c5=%u c6gap=%u c7=%u\n",runs[0].committed-auto_t0,runs[1].committed-charge_at[0],
   runs[2].committed-runs[1].finished,runs[3].committed-runs[2].finished,runs[4].committed-charge_at[2],runs[5].committed-runs[4].finished,runs[6].committed-charge_at[4]);
  soc_expect(runs[0].committed-auto_t0>=15000&&runs[0].committed-auto_t0<=20000,mode,"C1 plug-in request after the settle delay");
  soc_expect(runs[1].committed-charge_at[0]>=5000&&runs[1].committed-charge_at[0]<=10000,mode,"C2 prompt at the real charge start");
  for(int i=2;i<4;++i){const auto gap=runs[i].committed-runs[i-1].finished;
   soc_expect(gap>=300000&&gap<=305000,mode,"configured 300 s interval during active charging");}
  soc_expect(charge_at[1]>runs[3].committed&&charge_at[1]<runs[3].finished,mode,"C4 paused while in flight and drained");
  soc_expect(runs[4].committed-charge_at[2]>=5000&&runs[4].committed-charge_at[2]<=10000,mode,"C5 prompt at resume");
  const auto c6=runs[5].committed-runs[4].finished;
  soc_expect(charge_at[3]>runs[4].finished&&c6>=30000&&c6<=35000,mode,"C6 resume before the 300 s interval, after the unchanged 30 s minimum gap");
  soc_expect(runs[6].committed-charge_at[4]>=15000&&runs[6].committed-charge_at[4]<=20000,mode,"C7 replug: new plug-in request");
 } else if(mode=="soc-auto-charge-resume") {
  soc_expect(runs.size()==13,mode,"thirteen automatic operations");
  for(size_t i=0;i<runs.size();++i){soc_expect(runs[i].automatic&&runs[i].wakes==0,mode,"firmware-initiated, awake, no wake");
   if(i)soc_expect(runs[i].committed>=runs[i-1].finished,mode,"operations strictly sequential");}
  } else if(mode=="soc-wake-followup") {
  soc_expect(runs.size()==2&&!runs[0].automatic&&runs[1].automatic,mode,"one manual request, one firmware follow-up");
  soc_expect(runs[0].wakes==1&&runs[0].info_ignored>=1&&runs[0].charges==0&&runs[0].outcome=="woken_no_soc"&&runs[0].error==WOKEN_NO_SOC,mode,
   "M1: confirmed awake (authenticated VCSEC), infotainment silent: 'woken, no SoC'");
  soc_expect(runs[1].wakes==0&&runs[1].wake_partial==0,mode,"F2 follow-up never wakes");
  expect_soc(runs[1],&runs[0],"F2 follow-up delivers the SoC");
  const auto gap=runs[1].committed-runs[0].finished;std::printf("SOC_FOLLOWUP gap=%u\n",gap);
  soc_expect(gap>=30000&&gap<=125000,mode,"F2 after the 30 s post-wake delay (and the existing foreign-scan guard), bounded");
  } else if(mode=="soc-auto-wake-followup") {
  soc_expect(runs.size()==3,mode,"plug-in request, one follow-up, resume request");
  for(const auto &x:runs)soc_expect(x.automatic&&x.wakes+x.wake_partial<=1,mode,"automatic, at most one wake per operation");
  soc_expect(runs[0].wakes==1&&runs[0].outcome=="woken_no_soc"&&runs[0].charges==0,mode,"W1 wakes while charging (stored wake_once=false), woken without SoC");
  soc_expect(runs[1].wakes==0,mode,"W2 follow-up never wakes");expect_soc(runs[1],&runs[0],"W2 follow-up SoC");
  const auto gap=runs[1].committed-runs[0].finished;std::printf("SOC_AUTO_FOLLOWUP gap=%u\n",gap);
  soc_expect(gap>=30000&&gap<=30000+(TeslaBleDiscovery::SCAN_SECONDS+10)*1000,mode,"W2 after the post-wake delay");
  soc_expect(runs[2].wakes==1&&runs[2].outcome=="woken_no_soc"&&runs[2].committed-charge_at[0]>=5000,mode,"W3 resume request wakes once");
  expect_no_new_soc(runs[2],runs[1],"W3 keeps W2's SoC and receive time");
  }
 std::printf("SOC_WAKE_PASS %s\n",mode.c_str());
}
static const uint8_t *awake_bytes(){return controller_fixture::awake;}
static const uint8_t *asleep_bytes(){return controller_fixture::asleep;}
