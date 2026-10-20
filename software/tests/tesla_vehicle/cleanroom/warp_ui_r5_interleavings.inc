// SPDX-License-Identifier: Apache-2.0
// All schedules run actual module/worker/main/controller; only OS/SDK/storage
// boundaries are hosted. No key bytes are printed or preserved as evidence.
void ui_r5_main_pass() {
#ifdef TESLA_BLE_TARGET_SYNC_TEST
 target_main_zero_wait=true;
#endif
 module.loop();
#ifdef TESLA_BLE_TARGET_SYNC_TEST
 target_main_zero_wait=false;
#endif
}
TeslaBleRequestIdentity ui_r5_auth_queue(bool queued=true) {
 auto *r=module.tesla->runtime_owner.get(); module.request_soc(true); tick();
 if(last().hello) { inject(peer.hello(last())); tick(); }
 inject(peer.response(last(),{awake,sizeof(awake)},2)); tick();
 if(last().hello) { inject(peer.hello(last())); tick(); }
 const auto request=r->in_flight;
 inject(peer.response(last(),{soc_bytes(),sizeof(controller_fixture::soc)}));
 assert(r->session.matches(request)==queued); return request;
}
void ui_r5_finish_stop() {
 module.tesla->config.get("enable")->updateBool(false); ui_r5_main_pass();
 auto *r=module.tesla->runtime_owner.get(); assert(r && r->worker_stopped.load());
 assert(r->teardown_handle); run_task(r->teardown_handle); ui_r5_main_pass();
 assert(!module.tesla->runtime_owner.get() && !module.tesla->state.get("running")->asBool());
 assert(!module.tesla->state.get("stopping")->asBool());
}
void ui_r5_delete_delayed(bool disconnect_first, bool contend=false) {
 ui_with_worker_guard([&]() {
  auto *r=module.tesla->runtime_owner.get(); ui_r5_auth_queue(); module.register_urls();String error;
  api.commands.at("tesla_vehicle/forget_key")(0,error); assert(error.length()==0);
  unsigned passes=0; std::atomic<bool> held{false},release{false};std::thread holder;
  on_gatt_close=[&]() { if(contend) {
   holder=std::thread([&]() {tesla_warp::CheckedMutex::Guard lock(r->command_outcome.mutex);held=true;while(!release){std::this_thread::yield();}});
   while(!held){std::this_thread::yield();}
  }};
  on_delay=[&]() {
   ui_r5_main_pass(); ++passes;
   assert(ev.applied==0 && !module.tesla->state.get("restart_required")->asBool());
   assert(!r->deferred_soc && r->deferred_attempts==0);
   if(passes==12) {
    if(contend){release=true;holder.join();}
    esp_ble_gattc_cb_param_t p;p.close.conn_id=p.disconnect.conn_id=4;
    for(unsigned i=0;i<2;++i) {
     TeslaVehicle::gatt_callback(disconnect_first ? ESP_GATTC_DISCONNECT_EVT : ESP_GATTC_CLOSE_EVT,1,&p);
     TeslaVehicle::gatt_callback(disconnect_first ? ESP_GATTC_CLOSE_EVT : ESP_GATTC_DISCONNECT_EVT,1,&p);
    }
   }
  };
  module.process_commands(); on_delay={}; assert(passes>=12);
  ui_r5_main_pass(); assert(ev.applied==0 && !r->deferred_soc && r->deferred_attempts==0);
  assert(!r->pair_requested.load() && !r->poll_requested.load() && !r->forget_requested.load());
  assert(r->storage->private_key_presence()==TeslaBleStorageAdapter::KeyPresence::Absent);
  assert(module.tesla->state.get("key_presence")->asString()=="absent");
  assert(!r->stop_requested.load() && !module.tesla->state.get("restart_required")->asBool());
  assert(!r->command_outcome.forgetting() && !module.tesla->state.get("key_present")->asBool());
 });
 ui_r5_finish_stop(); assert(module.tesla->state.get("key_presence")->asString()=="absent" && ev.applied==0);
}
void ui_r5_bounded_stop(unsigned mode) {
 ui_with_worker_guard([&]() {
  auto *r=module.tesla->runtime_owner.get();
  // Cancellation has an actual command and storage probe, never synthetic state.
  if(mode==3){queue_pair();module.process_commands();tick();ui_approve_without_main();}
  else {ui_r5_auth_queue(); queue_pair();}
  const auto command=r->command_outcome.pair.command;
  std::atomic<bool> held{false},release{false},outcome_held{false},done{false};
  std::thread holder([&]() {
   if(mode==0) { tesla_warp::CheckedMutex::Guard lock(r->command_outcome.mutex); held=true; while(!release) { std::this_thread::yield(); } }
   else { tesla_warp::CheckedMutex::Guard lock(r->session.mutex); held=true; while(!release) { std::this_thread::yield(); } }
  });
  while(!held) { std::this_thread::yield(); }
  // A test watchdog releases locks so a broken baseline fails instead of hanging.
  std::thread watchdog([&]() { const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(700); while(!done && std::chrono::steady_clock::now()<until) { std::this_thread::yield(); } if(!done)release=true; });
  std::thread cancel;
  if(mode) {
   if(mode==2 || mode==3) {
    // Main observes outcome available while session is held (try outcome->session).
#ifdef TESLA_BLE_TARGET_SYNC_TEST
    target_main_zero_wait=true;
#endif
    module.publish_pairing_state(); module.process_module_events();
#ifdef TESLA_BLE_TARGET_SYNC_TEST
    target_main_zero_wait=false;
#endif
   }
   cancel=std::thread([&]() { module.cancel_pairing("R5 held-session cancellation",command); });
   while(!release) {
    tesla_warp::CheckedMutex::Guard probe(r->command_outcome.mutex,true);
    if(!probe) { outcome_held=true;break; }
    std::this_thread::yield();
   }
  }
  const auto start=std::chrono::steady_clock::now();
  if(mode==3)module.tesla->config.get("enable")->updateBool(false);
  for(unsigned i=0;i<10;++i) { ui_r5_main_pass(); }
  const bool before_release=!release.load();
  const auto elapsed=std::chrono::steady_clock::now()-start;
  done=true;release=true;holder.join();if(cancel.joinable())cancel.join();watchdog.join();
  assert(before_release && elapsed<std::chrono::milliseconds(350));
  std::printf("R5_MAIN_BOUNDED mode=%u passes=10 elapsed_us=%lld returned_before_release=true\n",mode,
      static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()));
  if(mode)assert(outcome_held);
  ui_r5_main_pass(); assert(ev.applied==0 && !r->deferred_soc);
  assert(module.tesla->state.get("restart_required")->asBool()==(mode!=3));
 });
 ui_r5_finish_stop(); assert(ev.applied==0 && !module.accepts_commands());
}
void ui_r5_final_presence(unsigned variant, bool consume_first) {
 ui_with_worker_guard([&]() {
  auto *r=module.tesla->runtime_owner.get(); module.process_module_events();
  assert(module.tesla->state.get("key_presence")->asString()=="present");
  // Prior authenticated display fact: completed erasure must retire it even
  // when the final storage probe itself returns unknown.
  module.tesla->state.get("paired")->updateBool(true);
  module.register_urls();String error;api.commands.at("tesla_vehicle/forget_key")(0,error);assert(error.length()==0);
  r->command_outcome.sequence=UINT64_MAX-1;
  module.process_commands();assert(r->storage->private_key_presence()==TeslaBleStorageAdapter::KeyPresence::Absent);
  assert(r->command_outcome.sequence==UINT64_MAX);
  if(consume_first){ui_r5_main_pass();assert(module.tesla->state.get("key_presence")->asString()=="absent");}
  // Final probe can differ from the last sequenced snapshot; it must remain
  // truthful even if no further publication identity can be allocated.
#ifdef TESLA_BLE_MODULE_POSIX_TEST
  TeslaBleStorageIO::Paths paths;assert(paths.initialize("/config/tesla_ble_private_key"));
  if(variant==1){const int fd=::open(paths.temporary,O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0);const uint8_t dummy=7;assert(::write(fd,&dummy,1)==1 && ::close(fd)==0);}
  if(variant==2){assert(::mkdir(paths.canonical,0700)==0);} // Real fstat/nonregular probe refusal.
#else
  if(variant==1)LittleFS.files["/config/tesla_ble_private_key.tmp"]={7};
  if(variant==2){LittleFS.files["/config/tesla_ble_private_key"]={7};fs_fault=[](const std::string &op){return op.rfind("open:",0)==0;};}
#endif
  r->stop_requested.store(true);
 });
 fs_fault={};auto *r=module.tesla->runtime_owner.get();assert(r->worker_stopped.load());
 ui_r5_main_pass();
 const char *expected=variant==1?"cleanup":variant==2?"unknown":"absent";
 assert(module.tesla->state.get("key_presence")->asString()==expected);
 assert(!module.tesla->state.get("key_present")->asBool());
 assert(!module.tesla->state.get("paired")->asBool());
 assert(module.tesla->state.get("restart_required")->asBool());
 assert(module.tesla->state.get("last_error")->asString().length()!=0);
 assert(r->command_outcome.sequence==UINT64_MAX && !module.accepts_commands());
 ui_r5_finish_stop();assert(module.tesla->state.get("key_presence")->asString()==expected);
 assert(module.tesla->state.get("restart_required")->asBool() && !module.accepts_commands());
}
void ui_r5_queue_recovery() {
 ui_with_worker_guard([&]() {
  auto *r=module.tesla->runtime_owner.get();const auto capacity=r->queues.events()->capacity;
  r->queues.events()->capacity=0; const auto old=ui_r5_auth_queue(false);
  assert(!r->session.matches(old));
  r->queues.events()->capacity=capacity;ui_r5_main_pass();assert(ev.applied==0);
  const auto fresh=ui_r5_auth_queue();assert(fresh.request!=old.request && fresh.request!=0);
  ui_r5_main_pass();assert(ev.applied==1 && !r->session.matches(fresh));
  assert(module.tesla->state.get("last_error")->asString().length()==0);
 });ui_r5_finish_stop();assert(ev.applied==1);
}
