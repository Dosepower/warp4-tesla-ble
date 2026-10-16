// SPDX-License-Identifier: Apache-2.0
void ui_r4_shutdown(bool fresh) {
 ui_with_worker_guard([&]() {
  fs_fault=[](const std::string &s) { return s=="open:/config/tesla_ble_warp_ev_mac.tmp:w"; };
  queue_pair(); module.process_commands(); fs_fault={}; module.process_module_events();
  module.request_soc(true); tick();
  if(last().hello) { inject(peer.hello(last())); tick(); }
  inject(peer.response(last(),{awake,sizeof(awake)},2)); tick();
  inject(peer.hello(last())); tick(); inject(peer.response(last(),{soc_bytes(),sizeof(controller_fixture::soc)}));
  module.process_module_events(); assert(ev.applied==1 && module.state.get("last_error")->asString().length()==0);
  if(fresh) { module.post_event(TeslaVehicle::EventType::Error,"new shutdown failure"); }
 });
 auto *r=module.runtime_owner.get();
 module.config.get("enable")->updateBool(false);
 assert(r->worker_stopped.load());
 module.stop_runtime();
 assert(module.state.get("last_error")->asString()==(fresh ? "new shutdown failure" : ""));
 assert(module.state.get("key_present")->asBool());
}
void ui_r4_session_contention(bool stale, bool permanent) {
 ui_with_worker_guard([&]() {
  auto *r=module.runtime_owner.get(); module.request_soc(true); tick();
  if(last().hello) { inject(peer.hello(last())); tick(); }
  inject(peer.response(last(),{awake,sizeof(awake)},2)); tick();
  inject(peer.hello(last())); tick(); inject(peer.response(last(),{soc_bytes(),sizeof(controller_fixture::soc)}));
  std::atomic<bool> held{false},release{false};
  std::thread worker([&]() { tesla_warp::CheckedMutex::Guard lock(r->session.mutex); held=true; while(!release.load()) { std::this_thread::yield(); } });
  while(!held.load()) { std::this_thread::yield(); }
  module.process_module_events();
  if(permanent) { for(unsigned i=0;i<12;++i) { module.process_module_events(); } }
  release=true; worker.join();
  if(stale) { ++ev.generation; module.loop(); }
  module.process_module_events(); module.process_module_events();
  if(permanent) { assert(module.state.get("last_error")->asString().length()!=0); assert(ev.applied==0); }
  else { assert(ev.applied==(stale ? 0 : 1)); }
 });
}
void ui_r4_queue_full() {
 auto *r=module.runtime_owner.get();
 TeslaVehicle::ModuleEvent filler; filler.runtime_epoch=r->epoch;
 while(xQueueSend(r->queues.events(),&filler,0)==pdTRUE) {}
 const auto id=r->session.arm(r->epoch,*r->session.identity(),r->configured_ev_index);
 module.post_event(TeslaVehicle::EventType::Soc,nullptr,50,id,true,true);
 module.process_module_events();
 assert(ev.applied==0 && module.state.get("last_error")->asString().length()!=0);
 assert(!r->session.matches(id));
}
void ui_r4_exhaust(unsigned which) {
 auto *r=module.runtime_owner.get();
 if(which==0) { r->session.sequence=UINT64_MAX; module.request_soc(true); }
 if(which==1) { module.register_urls(); r->command_outcome.pair_sequence=UINT64_MAX; String error; api.commands.at("tesla_vehicle/pair")(0,error); }
 if(which==2) { module.runtime_owner.runtime_epoch=UINT64_MAX; module.runtime_owner.runtime.reset(); module.start_runtime(); }
 if(which==3) { r->vehicle.get()->*r4_member(R4Operation{})=UINT64_MAX; module.request_soc(true); }
 if(which==4) { auto &receiver=r->vehicle.get()->*r4_member(R4Receiver{}); receiver.revision_=UINT64_MAX; receiver.reset(1); module.process_rx(); }
 module.loop(); assert(module.state.get("restart_required")->asBool());
 if(auto *live=module.runtime_owner.get()) { assert(live->stop_requested.load()); }
 assert(!module.accepts_commands());
}
void ui_r4_final_shutdown() {
 ui_r3_reverse_publication(true,true);
 module.config.get("enable")->updateBool(false); module.stop_runtime();
 assert(module.state.get("last_error")->asString().length()==0);
 assert(module.state.get("paired")->asBool() && module.state.get("key_presence")->asString()=="present");
}
void ui_r4_counter_boundaries() {
 static_assert(std::is_same_v<decltype(TeslaBleRuntimeOwner<int>::reboot_required),std::atomic<bool>>);
 const TeslaBleIdentity identity{1,2,1};
 TeslaBleCommandOutcome out; out.pair_sequence=UINT64_MAX-1;
 auto id=out.accept_pair(identity); assert(id==UINT64_MAX); assert(out.claim_pair());
 out.cancel_pair(id,"done",true,true,false,[](){}); assert(out.take_pair_state([](auto){return true;})->completed);
 for(unsigned i=0;i<3;++i) { assert(!out.accept_pair(identity) && out.publication_exhausted() && out.pair_sequence==UINT64_MAX); }
 TeslaBleSession session; session.publish(identity); session.bind(1); session.connect(); session.sequence=UINT64_MAX-1;
 assert(session.arm(1,identity,2).request==UINT64_MAX && !session.exhausted());
 for(unsigned i=0;i<3;++i) { assert(!session.arm(1,identity,2).request && session.exhausted() && session.sequence==UINT64_MAX); }
 session.current.connection=UINT64_MAX-1; assert(session.disconnect()==UINT64_MAX);
 for(unsigned i=0;i<3;++i) { assert(session.disconnect()==UINT64_MAX); session.connect(); assert(!session.connected); }
 TeslaBleRuntimeOwner<int> owner; owner.runtime_epoch=UINT64_MAX-1; unsigned allocations=0;
 assert(owner.start(true,[&](){++allocations; return std::make_unique<int>(0);},[](int &){return true;}));
 assert(owner.epoch()==UINT64_MAX); owner.runtime.reset();
 TeslaBleRuntimeOwner<int> failed; failed.runtime_epoch=UINT64_MAX-1;
 assert(!failed.start(true,[](){return std::unique_ptr<int>{};},[](int &){return true;}));
 assert(failed.restart_required() && failed.epoch()==UINT64_MAX);
 for(unsigned i=0;i<3;++i) { assert(!owner.start(true,[&](){++allocations; return std::make_unique<int>(0);},[](int &){return true;})); assert(owner.restart_required() && owner.epoch()==UINT64_MAX && allocations==1); }
 tesla_clean::FrameReceiver receiver; receiver.reset(1); receiver.revision_=UINT64_MAX-1;
 assert(receiver.receive({},0,1,[](void *,const Frame &){},nullptr));
 for(unsigned i=0;i<3;++i) { assert(!receiver.receive({},0,1,[](void *,const Frame &){},nullptr)); receiver.reset(1); assert(receiver.exhausted() && receiver.revision_==UINT64_MAX); }
 auto *r=module.runtime_owner.get(); r->connection_generation.store(UINT64_MAX-1); assert(r->advance_connection());
 for(unsigned i=0;i<3;++i) { assert(!r->advance_connection() && r->connection_generation.load()==UINT64_MAX); }
 module.loop(); assert(module.state.get("restart_required")->asBool());
}
void ui_r4_ev_exhaustion() {
 ev.generation=UINT64_MAX-1; ev.exhausted=false;
 assert(ev_advance_session(ev.generation,ev.exhausted,ev.index,ev.mac)); assert(ev.generation==UINT64_MAX);
 for(unsigned i=0;i<3;++i) { ev.index=2; ev.mac=1; assert(!ev_advance_session(ev.generation,ev.exhausted,ev.index,ev.mac)); assert(ev.index==-1 && ev.mac==0 && ev.generation==UINT64_MAX); }
 module.loop(); assert(module.state.get("restart_required")->asBool() && !module.accepts_commands());
 TeslaVehicle disabled; disabled.pre_setup(); const auto count=task_launches;
 disabled.setup(); disabled.loop();
 assert(!disabled.runtime_owner.get() && task_launches==count && disabled.state.get("restart_required")->asBool());
}
void ui_r4_operation_last() {
 auto *r=module.runtime_owner.get(); r->vehicle.get()->*r4_member(R4Operation{})=UINT64_MAX-1;
 module.request_soc(true); tick();
 if(last().hello) { inject(peer.hello(last())); tick(); }
 inject(peer.response(last(),{awake,sizeof(awake)},2)); tick();
 inject(peer.hello(last())); tick(); inject(peer.response(last(),{soc_bytes(),sizeof(controller_fixture::soc)}));
 module.process_module_events(); assert(ev.applied==1);
 assert((r->vehicle.get()->*r4_member(R4Operation{}))==UINT64_MAX);
 for(unsigned i=0;i<3;++i) { module.request_soc(true); }
 module.loop(); assert(module.state.get("restart_required")->asBool() && r->stop_requested.load());
 assert((r->vehicle.get()->*r4_member(R4Operation{}))==UINT64_MAX);
}
void ui_r4_serial_last() {
 auto *r=module.runtime_owner.get(); r->vehicle.get()->*r4_member(R4Serial{})=UINT64_MAX-1;
 r->vehicle->close(r->connection_generation.load());
 for(unsigned i=0;i<3;++i) { assert(!r->vehicle->open(r->connection_generation.load())); }
 assert((r->vehicle.get()->*r4_member(R4Serial{}))==UINT64_MAX);
 module.process_rx(); module.loop(); assert(module.state.get("restart_required")->asBool());
}
