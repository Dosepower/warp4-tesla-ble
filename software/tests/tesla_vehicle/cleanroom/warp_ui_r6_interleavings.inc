// SPDX-License-Identifier: Apache-2.0
// Runs task_entry -> ordinary worker loop. Only OS/SDK notifications, clock,
// mutex scheduling and filesystem failures are hosted. No production bodies
// are extracted; no direct request_soc/process_rx/ready/init-body driver.
void ui_r6_command(const char *name) {
 String error;api.commands.at(name)(0,error);assert(error.length()==0);
}
void ui_r6_notify(const Frame &frame) {
 auto *r=module.tesla->runtime_owner.get();std::vector<uint8_t> bytes{uint8_t(frame.length>>8),uint8_t(frame.length)};
 bytes.insert(bytes.end(),frame.data.begin(),frame.data.begin()+frame.length);
 assert(bytes.size()<=sizeof(TeslaVehicle::RxFragment::data));
 esp_ble_gattc_cb_param_t p;p.notify.conn_id=4;p.notify.handle=20;
 std::memcpy(p.notify.remote_bda,r->remote_address,6);p.notify.value=bytes.data();p.notify.value_len=bytes.size();
 TeslaVehicle::gatt_callback(ESP_GATTC_NOTIFY_EVT,1,&p);
}
void ui_r6_run(unsigned scenario) {
 // 0/1 absent ordinary/MAX, 2/3 cleanup ordinary/MAX; 4 late timeout;
 // 5 real event-queue-full recovery; 6 one defer then disable; 7/8 split close;
 // 9/10 authoritative present/unknown controls; 11 teardown-only timeout
 // after exhaustion (same diagnostic-precedence rule, no worker-watch failure).
 auto *r=module.tesla->runtime_owner.get();host_gatt_success=true;module.register_urls();
 assert(!sdk_init && !r->vehicle && !r->connected.load());
 ui_r6_command("tesla_vehicle/poll");assert(r->poll_requested.load());
 unsigned iterations=0,handled=0,completions=0;uint32_t vc=0,info=0;
 uint64_t completed_request=0;TeslaBleRequestIdentity refused{};
 bool action=false,fill=false,retained=false;unsigned remove_calls=0;
 std::string authoritative_error;
 std::thread holder;std::atomic<bool> held{false},release{false};
 auto removal=[&](const std::string &operation) {
  if(operation.find("remove:")!=0 && operation.find("unlink:")!=0)return false;
  ++remove_calls;
  if(scenario==4 && !retained) {
   // Actual worker is inside storage erasure under worker_watch. Main runs at
   // the stalled syscall boundary; no manually manufactured timeout outcome.
   clock_ms.fetch_add(31001);ui_r5_main_pass();
   assert(r->supervision_failed && module.tesla->runtime_owner.get()==r);
   authoritative_error=module.tesla->state.get("last_error")->asString();
   assert(authoritative_error.find("timed out")!=std::string::npos);
   clock_ms.fetch_add(31001);ui_r5_main_pass();retained=true;
   r->command_outcome.sequence=UINT64_MAX-1;
  }
  return (scenario==2 || scenario==3) && operation.find("session_vcsec")!=std::string::npos;
 };
 on_task_delay=[&](uint32_t ms) {
  assert(++iterations<500);if(ms!=20)return;
  peer.address=r->routing_address;
  if(scenario==5 && !handled && !sent.empty() && !fill){ui_r5_main_pass();fill=true;}
  if(handled<sent.size()) {
   const auto request=sent[handled++];assert(!request.pair);
   Frame response;
   if(request.hello)response=peer.hello(request);
   else if(request.domain==Domain::Vcsec)response=peer.response(request,{awake,sizeof(awake)},++vc);
   else response=peer.response(request,{soc_bytes(),sizeof(controller_fixture::soc)},++info);
   ui_r6_notify(response);return; // Worker, not fixture, drains RX next iteration.
  }
  if(r->in_flight.request && r->in_flight_started_ms==0 && completed_request!=r->in_flight.request) {
   completed_request=r->in_flight.request;++completions;
   if(scenario==5 && fill) {
    assert(ev.applied==0);
    if(completions<9){ui_r6_command("tesla_vehicle/poll");return;}
    refused=r->in_flight;assert(!r->session.matches(refused));
    assert(r->queues.events()->capacity==8 && r->queues.events()->data.size()==8);
    ui_r5_main_pass();assert(ev.applied==0);
    assert(module.tesla->state.get("last_error")->asString().find("event queue full")!=std::string::npos);
    fill=false;ui_r6_command("tesla_vehicle/poll");return;
   }
   if(scenario==6) {
    holder=std::thread([&](){tesla_warp::CheckedMutex::Guard lock(r->session.mutex);held=true;while(!release){std::this_thread::yield();}});
    while(!held){std::this_thread::yield();}
    ui_r5_main_pass();assert(r->deferred_soc && r->deferred_attempts==1 && ev.applied==0);
    module.tesla->config.get("enable")->updateBool(false);ui_r5_main_pass();
    assert(!r->deferred_soc && r->deferred_attempts==0 && !module.tesla->state.get("restart_required")->asBool());
    release=true;holder.join();action=true;return;
   }
   ui_r5_main_pass();assert(ev.applied==1 && module.tesla->state.get("paired")->asBool());
   if(scenario==5) {
    assert(completions==10 && completed_request!=refused.request && !r->session.matches(refused));
    assert(module.tesla->state.get("last_error")->asString().length()==0);
    module.tesla->config.get("enable")->updateBool(false);ui_r5_main_pass();action=true;return;
   }
   if(scenario>=9) {
    if(scenario==10)r6_unknown_probe();
    if(scenario==11)r->command_outcome.sequence=UINT64_MAX;
    r->stop_requested.store(true);action=true;return;
   }
   if(scenario<=4) {
    if(scenario==2 || scenario==3) {
     const uint8_t dummy=7;assert(tesla_clean::KeyStorage::committed(r->storage->save_transaction("session_vcsec",&dummy,1)));
    }
    if(scenario==0 || scenario==1){ModuleAllocationFault::calls=0;ModuleAllocationFault::fail_at=1;ModuleAllocationFault::active=true;}
    if(scenario==1 || scenario==3)r->command_outcome.sequence=UINT64_MAX-1;
    fs_fault=removal;r6_remove_fault=removal;
   }
   ui_r6_command("tesla_vehicle/forget_key");action=true;return;
  }
  if(scenario==5 && !action && !completions) {
   // Real eight-item event queue, filled by successive public poll completions.
   // Drain initial Connected once, before withholding main. Never change capacity
   // or call charge_state_poll: this tests event delivery, not command admission.
   if(r->in_flight_started_ms){ui_r5_main_pass();fill=true;}
  }
 };
 if(scenario==7 || scenario==8) {
  on_gatt_close=[&]() {
   ++closes;esp_ble_gattc_cb_param_t p;p.close.conn_id=p.disconnect.conn_id=4;
   TeslaVehicle::gatt_callback(scenario==7?ESP_GATTC_CLOSE_EVT:ESP_GATTC_DISCONNECT_EVT,1,&p);
   assert(r->closing_link.load() && !r->retirement_operation.retired());
   module.tesla->config.get("enable")->updateBool(false);ui_r5_main_pass();
   assert(module.tesla->runtime_owner.is_stopping());
   TeslaVehicle::gatt_callback(scenario==7?ESP_GATTC_DISCONNECT_EVT:ESP_GATTC_CLOSE_EVT,1,&p);
   // Callback admission is now closed. Stale duplicates cannot reopen state.
   TeslaVehicle::gatt_callback(ESP_GATTC_CLOSE_EVT,1,&p);TeslaVehicle::gatt_callback(ESP_GATTC_DISCONNECT_EVT,1,&p);
  };
 }
 run_task(r->task_handle);on_task_delay={};ModuleAllocationFault::active=false;fs_fault={};r6_remove_fault={};
 assert(action && r->worker_stopped.load() && !r->deferred_soc);
 if(scenario<=3) {
  assert(remove_calls>0);if(scenario<2)assert(ModuleAllocationFault::failures>0);
  const auto expected=scenario<2?TeslaBleStorageAdapter::KeyPresence::Absent:TeslaBleStorageAdapter::KeyPresence::CleanupRequired;
  assert(r->storage->private_key_presence()==expected);
  assert(!r->command_outcome.deletion_done);cleanup_result=false;
  if(scenario==1 || scenario==3) {
   assert(r->command_outcome.sequence==UINT64_MAX && r->command_outcome.publication_exhausted());
  }
  ui_r5_main_pass();
  assert(!module.tesla->state.get("paired")->asBool()); // R5 BLOCKER 1 RED.
  assert(module.tesla->state.get("key_presence")->asString()==(scenario<2?"absent":"cleanup"));
 } else ui_r5_main_pass();
 if(scenario==4) {
  assert(retained && r->command_outcome.publication_exhausted());
  assert(module.tesla->state.get("last_error")->asString()==authoritative_error); // R5 BLOCKER 2 RED.
 }
 if(scenario==9 || scenario==10) {
  assert(module.tesla->state.get("key_presence")->asString()==(scenario==9?"present":"unknown"));
  assert(module.tesla->state.get("paired")->asBool());cleanup_result=false;
 }
 if(scenario==11) {
  assert(!r->supervision_failed && r->teardown_handle);
  assert(module.tesla->state.get("last_error")->asString().find("exhausted")!=std::string::npos);
  clock_ms.fetch_add(31001);ui_r5_main_pass();
  authoritative_error=module.tesla->state.get("last_error")->asString();
  assert(authoritative_error.find("stop failed or timed out")!=std::string::npos);
 } else if(r->teardown_handle)run_task(r->teardown_handle);
 for(unsigned i=0;i<4;++i) {
  ui_r5_main_pass();
  if(scenario<=3)assert(!module.tesla->state.get("paired")->asBool());
  if(scenario==1 || scenario==3)assert(r->command_outcome.sequence==UINT64_MAX);
  if(scenario==4 || scenario==11)assert(module.tesla->state.get("last_error")->asString()==authoritative_error);
 }
 if(scenario<=4 || scenario>=9) {
  assert(module.tesla->runtime_owner.get()==r && module.tesla->state.get("restart_required")->asBool() && !module.accepts_commands());
  if(scenario==9 || scenario==10)assert(module.tesla->state.get("paired")->asBool());
 } else {
  assert(!module.tesla->runtime_owner.get() && !module.tesla->state.get("running")->asBool());
  assert(ev.applied==(scenario==6?0:1));
 }
 assert(iterations>0 && !sdk_init);
 std::printf("R6_ORDINARY_TASK scenario=%u worker_delays=%u authenticated_completions=%u retained=%s passed\n",scenario,iterations,completions,module.tesla->runtime_owner.get()?"true":"false");
}
