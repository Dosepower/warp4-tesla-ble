// Independent late-event probe; only synthetic OS/radio boundary scheduling.
static void followup_flow(TeslaVehicle &m,const std::string &mode) {
 using namespace controller_fixture;
 Harness peer;IdentityLease key;assert(peer.keys.acquire(key));uint8_t pem[512]{};size_t n=0;assert(key.key()->export_pem(pem,sizeof(pem),n));
 TeslaBleStorageAdapter store;assert(store.select_profile(0,A));assert(tesla_clean::KeyStorage::committed(store.save_transaction("private_key",pem,n)));
 assert(tesla_clean::KeyStorage::committed(store.save_transaction("vehicle_vin",reinterpret_cast<const uint8_t *>("5YJ3E1EA7KF000001"),17)));
 const auto retained=LittleFS.files.at("/config/tesla_p00_112233445500_private_key");
 HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;explicit_scan(m,0);String error;assert(m.select_profile(0,false,error));auto*r=m.tesla->runtime_owner.get();
 bool submitted=false,complete=false,waiting=false,injected=false,finished=false,terminal_held=false;
 uint32_t write_at=0,event_elapsed=0;unsigned frames=0,close_calls=0;std::vector<uint8_t> wire;
 unsigned attempts=0;const unsigned required=mode=="followup-normal-ack"?1:3;
 TeslaNimbleEvent old_event{TeslaNimbleEvent::Write,0,4,19,0};
 const bool should_retire=false; // Normative: real SDK/terminal/application drainage permits retry.
 on_gatt_bytes=[&](const uint8_t*p,size_t count){wire.insert(wire.end(),p,p+count);if(wire.size()>=2&&wire.size()==size_t(wire[0])*256+wire[1]+2){++frames;wire.clear();}};
 test_submit=[&](int stage){
  if(stage==6&&waiting){
   ++close_calls;
   if(mode=="followup-security-delayed"&&!terminal_held){terminal_held=true;return;}
  }
  if(stage==5&&test_handle!=21&&complete&&!waiting){
   waiting=true;write_at=millis();sdk_track_pending_write();return; // returned submission, real SDK abort bodies on disconnect
  }
  nimble_auto_submit(stage);
 };
 on_task_delay=[&](uint32_t){
  if(waiting&&!injected&&millis()-write_at>=180){
   injected=true;event_elapsed=millis()-write_at;
   assert(event_elapsed==180 && !r->operation.retired());
   if(mode=="followup-rx-overflow"){
    // Real adapter callback fills the bounded production RX queue within one
    // yield (no worker service possible), then rejects the next notification.
    for(unsigned i=0;i<TeslaVehicle::RX_QUEUE_DEPTH+1;++i){os_mbuf mb;mb.bytes={0,1};ble_gap_event e{};e.type=BLE_GAP_EVENT_NOTIFY_RX;e.notify_rx.conn_handle=4;e.notify_rx.attr_handle=20;e.notify_rx.om=&mb;test_gap(&e,test_gap_arg);}
   } else if(mode=="followup-normal-ack"){
    // Drain tracked proc via its actual callback, with success status, then free it.
    auto*p=pending_sdk_write;assert(p);pending_sdk_write=nullptr;ble_gattc_write_cb(p,0,0);ble_gattc_proc_free(p);nimble_before_disconnect={};
   } else {
    ble_gap_event e{};e.type=BLE_GAP_EVENT_ENC_CHANGE;e.enc_change.conn_handle=4;e.enc_change.status=23;
    for(unsigned burst=0;burst<(mode=="followup-security-burst"?12U:1U);++burst)test_gap(&e,test_gap_arg);
   }
  }
  m.loop();
 };
 on_delay=[&]{
  if(!running_task||running_task->name!="tesla_ble"||r->profile_pending.load())return;
  if(!submitted){m.loop();m.tesla->profile_command.get("ev_index")->updateInt(0);api.commands.at("tesla_vehicle/pair")(0,error);assert(error.empty());submitted=true;return;}
  if(!complete&&r->pair_result.load()==int(tesla_clean::VehicleResultCode::PairRequestSent)){
   complete=true;assert(r->pair_tx_total.load()==88&&r->pair_tx_acked.load()==88&&frames==1&&!r->authorized.load());
   if(attempts){const auto generation=r->connection_generation.load();TeslaVehicle::gatt_callback(old_event);assert(r->connection_generation.load()==generation&&!r->operation.retired());}
   old_event.epoch=r->physical_generation.load();
  }
  if(!injected||finished||!r->worker_watch.elapsed_ms(millis()))return;
  // Only inspect after gatt_operation has returned, not inside its 10-ms yields.
  if(millis()-write_at<200)return;
  m.loop();m.publish_bluetooth_state();
  assert(m.bluetooth_state.get("restart_reason")->asString()==(should_retire?"lifecycle_latched":""));
  assert(m.bluetooth_state.get("restart_required")->asBool()==should_retire);
  const auto elapsed=m.tesla->state.get("timeout_elapsed_ms")->asUint();
  std::printf("FOLLOWUP mode=%s injected_ms=%u current_write_elapsed=%u retired=%d restart=%d cause=%s operation=%u diagnostic_elapsed=%u frames=%u sdk_drained=%u closes=%u\n",mode.c_str(),event_elapsed,millis()-write_at,r->operation.retired(),m.tesla->runtime_owner.restart_required(),m.tesla->state.get("timeout_cause")->asString().c_str(),m.tesla->state.get("timeout_operation")->asUint(),elapsed,frames,sdk_freed,close_calls);std::fflush(stdout);
  assert(!r->authorized.load()&&!m.tesla->state.get("paired")->asBool()&&ev.applied==0);
  assert(LittleFS.files.at("/config/tesla_p00_112233445500_private_key")==retained);
  assert(!pending_sdk_write&&sdk_freed==attempts+1);
  assert(r->operation.retired()==should_retire);
  assert(m.tesla->runtime_owner.restart_required()==should_retire);
  if(should_retire){assert(m.tesla->state.get("timeout_cause")->asString()=="gatt_operation");assert(m.tesla->state.get("timeout_operation")->asUint()==7);assert(elapsed==200);}
  else assert(elapsed==0);
  ++attempts;
  if(attempts<required){
   assert(m.bluetooth.central()->idle() && "real SDK, terminal AND application callback drained before retry");
   assert(!m.tesla->state.get("pairing_pending")->asBool()&&r->command_outcome.forward_available());
   submitted=complete=waiting=injected=terminal_held=false;frames=close_calls=0;wire.clear();return;
  }
  finished=true;r->stop_requested=true;
 };
 run_task(r->task_handle);on_delay={};on_task_delay={};test_submit=nimble_auto_submit;on_gatt_bytes={};assert(submitted&&complete&&injected&&finished);
 assert(attempts==required);m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();test_submit={};
 std::puts("FOLLOWUP PASS: real worker/native adapter/supervisor; synthetic event is not historical attribution");
}
