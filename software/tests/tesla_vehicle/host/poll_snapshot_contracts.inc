// Coherent SoC outcome/diagnostic hand-over through the real production bodies:
// worker writer set_poll_diagnostics() + TeslaVehicle::set_poll_outcome(), main
// reader TeslaVehicle::auto_poll() publication block, published device state.
// Included inside TeslaBleModuleTest (test_nimble_module.cpp). The test thread
// plays the single worker writer and the main task in a deterministic order
// (no scheduler, no sleeps). Version-neutral: only production functions and the
// published state are used, so the same file is the RED probe against the
// pre-fix bodies (field-wise atomics, sequence published last) and GREEN for the
// candidate (single-writer seqlock snapshot). Synthetic diagnostics only.
// Modes: snapshot-interleave
static std::string snapshot_signature(TeslaVehicle &m) {
 auto &s=m.tesla->state;char text[900];
 snprintf(text,sizeof(text),"outcome=%s|auto=%d|failure=%s|stage=%s|elapsed=%u|att=%u|to=%u|fr=%u|rej=%u|fault=%u|reused=%d|vc=%d|info=%d|trace=%s",
  s.get("poll_outcome")->asString().c_str(),s.get("poll_outcome_automatic")->asBool(),s.get("poll_failure")->asString().c_str(),
  s.get("poll_stage")->asString().c_str(),s.get("poll_elapsed_ms")->asUint(),s.get("poll_attempts")->asUint(),
  s.get("poll_timeouts")->asUint(),s.get("poll_frames")->asUint(),s.get("poll_rejected")->asUint(),s.get("poll_fault")->asUint(),
  s.get("poll_session_reused")->asBool(),s.get("poll_vcsec_answered")->asBool(),s.get("poll_info_answered")->asBool(),
  s.get("poll_trace")->asString().c_str());
 return text;
}
// Distinct synthetic classification per operation k (every field encodes k).
static tesla_clean::VehicleDiagnostics snapshot_diag(unsigned k,tesla_clean::VehicleFailure failure,tesla_clean::VehicleStage stage) {
 tesla_clean::VehicleDiagnostics d;
 d.failure=failure;d.stage=stage;d.elapsed_ms=1111u*k;d.fault=11u*k;
 d.attempts=uint8_t(k);d.response_timeouts=uint8_t(k+1);d.frames=uint8_t(k+2);d.rejected_replies=uint8_t(k+3);
 d.vcsec_reused=(k&1)!=0;d.vcsec_answered=(k&1)!=0;d.info_answered=(k&2)!=0;
 d.unrelated_frames=uint8_t(k);d.late_session_replies=uint8_t(k);
 d.trace_total=3;
 for(unsigned i=0;i<3;++i)d.trace[i]=(1111u*k+i)<<15|uint32_t(5+i)<<10|uint32_t(stage)<<6|k;
 return d;
}
static std::string snapshot_expected(const char *outcome,bool automatic,const char *failure,const tesla_clean::VehicleDiagnostics *d) {
 char trace[544]="";
 if(d)assert(tesla_clean::format_vehicle_trace(*d,trace,sizeof(trace)));
 char text[900];
 snprintf(text,sizeof(text),"outcome=%s|auto=%d|failure=%s|stage=%s|elapsed=%u|att=%u|to=%u|fr=%u|rej=%u|fault=%u|reused=%d|vc=%d|info=%d|trace=%s",
  outcome,automatic,failure,d?tesla_clean::vehicle_stage_name(d->stage):"",d?unsigned(d->elapsed_ms):0u,d?unsigned(d->attempts):0u,
  d?unsigned(d->response_timeouts):0u,d?unsigned(d->frames):0u,d?unsigned(d->rejected_replies):0u,d?unsigned(d->fault):0u,
  d?int(d->vcsec_reused||d->info_reused):0,d?int(d->vcsec_answered):0,d?int(d->info_answered):0,trace);
 return text;
}
// TESLA_SNAPSHOT_ALL=1: report every RED scenario and exit 1 at the end (RED evidence);
// default: stop at the first RED.
static unsigned &snapshot_reds() { static unsigned n=0; return n; }
static void snapshot_red_exit() {
 ++snapshot_reds();std::fflush(stdout);
 if(!std::getenv("TESLA_SNAPSHOT_ALL"))std::_Exit(1);
}
static void snapshot_check(const std::string &got,const std::string &want,const char *what) {
 if(got==want){std::printf("GREEN: %s\n",what);return;}
 std::printf("RED: %s\n  published %s\n  expected  %s\n",what,got.c_str(),want.c_str());snapshot_red_exit();
}
static void snapshot_flow(TeslaVehicle &m,const std::string &mode) {
 using tesla_clean::VehicleFailure;using tesla_clean::VehicleStage;using PO=TeslaVehicle::PollOutcome;
 assert(mode=="snapshot-interleave");
 using RT=std::remove_pointer_t<decltype(m.tesla->runtime_owner.get())>;
 // Runtime without a launched worker: the test thread is the only writer.
 assert(m.tesla->runtime_owner.start(true,[&m]{return std::make_unique<RT>(&m);},[](auto &){return true;}));
 auto *r=m.tesla->runtime_owner.get();assert(r);
 ev.index=-1;ev.mac=0; // no live identity: auto_poll() publishes, then reports "off"
 m.auto_poll(); // nothing published yet
 const auto d1=snapshot_diag(1,VehicleFailure::ChallengeCap,VehicleStage::WaitInfoAuth);
 const auto d2=snapshot_diag(2,VehicleFailure::Disconnected,VehicleStage::WaitStatus);
 const auto d3=snapshot_diag(3,VehicleFailure::StatusUnusable,VehicleStage::WaitWake);
 const auto d4=snapshot_diag(4,VehicleFailure::WakePollCap,VehicleStage::WaitWake);
 const auto d5=snapshot_diag(5,VehicleFailure::Deadline,VehicleStage::WaitCharge);
 // S1: worker finished operation N, then already classified N+1 (its outcome not
 // yet published) before the main task's next pass: main must show N alone.
 set_poll_diagnostics(r,uint8_t(d1.failure),&d1);TeslaVehicle::set_poll_outcome(r,PO::Failed,true);
 set_poll_diagnostics(r,uint8_t(d2.failure),&d2);
 m.auto_poll();
 snapshot_check(snapshot_signature(m),snapshot_expected("failed",true,"challenge_cap",&d1),
  "S1 main reads outcome N while worker writes N+1 classification: N coherent");
 TeslaVehicle::set_poll_outcome(r,PO::WokenNoSoc,false);m.auto_poll();
 snapshot_check(snapshot_signature(m),snapshot_expected("woken_no_soc",false,"disconnected",&d2),"S1 N+1 published coherently");
 // S2: an outcome without its own classification (Busy) never carries N+1's.
 TeslaVehicle::set_poll_outcome(r,PO::Busy,true);m.auto_poll();
 snapshot_check(snapshot_signature(m),snapshot_expected("busy",true,"none",nullptr),"S2 Busy carries no earlier classification/trace");
 // S3: main pass while the worker is inside a publication (odd marker written,
 // header of N+3 stored, trace not yet): bounded reader gives up, nothing seen.
 const std::string before=snapshot_signature(m);std::string during;unsigned fired=0;
 set_poll_diagnostics(r,uint8_t(d3.failure),&d3);
 host_snapshot_write_hook()=[&]{++fired;m.auto_poll();during=snapshot_signature(m);};
 TeslaVehicle::set_poll_outcome(r,PO::Failed,true);
 host_snapshot_write_hook()=nullptr;
 if(fired!=1){std::printf("RED: S3 production writer has no write-begin interleaving point (hook fired %u)\n",fired);snapshot_red_exit();}
 snapshot_check(during,before,"S3 main pass during publication N+3 publishes nothing (no half-written N+3)");
 m.auto_poll();
 snapshot_check(snapshot_signature(m),snapshot_expected("failed",true,"status_unusable",&d3),"S3 next pass publishes N+3 coherently");
 // S4: worker publishes N+5 after the reader saw a stable version of N+4 and
 // before it copied the payload: the reader must retry and show N+5 alone.
 set_poll_diagnostics(r,uint8_t(d4.failure),&d4);TeslaVehicle::set_poll_outcome(r,PO::AsleepWakeLimited,true);
 fired=0;
 host_snapshot_read_hook()=[&]{++fired;set_poll_diagnostics(r,uint8_t(d5.failure),&d5);TeslaVehicle::set_poll_outcome(r,PO::WakeSentNoSoc,false);};
 m.auto_poll();
 host_snapshot_read_hook()=nullptr;
 if(fired!=1){std::printf("RED: S4 production reader has no copy interleaving point (hook fired %u)\n",fired);snapshot_red_exit();}
 snapshot_check(snapshot_signature(m),snapshot_expected("wake_sent_no_soc",false,"deadline",&d5),"S4 publication during copy: reader retries, N+5 coherent");
 const std::string settled=snapshot_signature(m);m.auto_poll();
 snapshot_check(snapshot_signature(m),settled,"S4 no republication of a seen sequence");
 if(snapshot_reds()){std::printf("SNAPSHOT_RED %s: %u scenario checks RED\n",mode.c_str(),snapshot_reds());std::fflush(stdout);std::_Exit(1);}
 std::printf("SNAPSHOT_PASS %s\n",mode.c_str());
}
