// Deterministic connection delays at the real adapter boundary, not instant-open mocks.
 static void connect_flow(TeslaVehicle&m,const std::string&mode) {
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;explicit_scan(m,0);
  seed(0);TeslaBleStorageAdapter store;assert(store.select_profile(0,A));assert(store.remove_key("authorized"));
  const auto key=LittleFS.files.at("/config/tesla_p00_112233445500_private_key");
  // Pre-CONNECT window (live failure signature): the real SDK has completed the
  // HCI link and cleared M_CONN (ble_gap_conn_cancel -> EALREADY) while CONNECT
  // stays deferred behind remote feature/version exchange. See
  // test_nimble_sdk_preconnect.py for the executed SDK bodies of this order.
  const bool pre_disconnect=mode=="connect-repeat-preconnect-disconnect",pre_stall=mode=="connect-repeat-preconnect-stall";
  const bool pre_late=mode=="connect-repeat-preconnect-late-connect",pre_undrained=mode=="connect-repeat-preconnect-undrained";
  const bool pre=pre_disconnect||pre_stall||pre_late||pre_undrained;
  const bool missing=mode=="connect-repeat-missing",blocked=mode=="connect-repeat-blocked",quarantine=missing||blocked||pre_undrained;
  const bool success=mode=="connect-repeat-late-success",refused=mode=="connect-repeat-refused",normal=mode=="connect-repeat-sdk-timeout",identity=mode=="connect-repeat-identity";
  auto*p=m.find_profile(0);String error;unsigned attempts=0,completed=0,cancels=0,main_ticks=0,terminates=0;uint32_t opened=0,terminal_at=0,disconnect_at=0,link_at=0,pre_disconnect_at=0,late_connect_at=0;bool committed=false,finished=false,identity_changed=false;
  uint64_t previous_epoch=0;bool late_connected=false;
  auto command=[&](const char*phase){auto&c=m.tesla->guided_command;auto*q=c.get("profile");entry(q,0);
   for(const char*k:{"ble_address_type","discovery_revision","poll_interval"})q->get(k)->updateUint(p->get(k)->asUint());
   c.get("phase")->updateString(phase);c.get("request")->updateString("0000000000000000000000000000000"+std::to_string(attempts+1));c.get("generation")->updateString(std::to_string(ev.generation));c.get("revision")->updateUint(m.tesla->config_revision);
   error.clear();api.commands.at("tesla_vehicle/guided")(0,error);assert(error.empty());};
  command("prepare_pair");auto*r=m.tesla->runtime_owner.get();const auto epoch=r->epoch;
  if(pre){ // repeated prepare before commit: rejected without replacing the pending single-use consent or touching radio
   const std::string pending=m.tesla->guided.request;auto&c=m.tesla->guided_command;c.get("request")->updateString("00000000000000000000000000000099");
   error.clear();api.commands.at("tesla_vehicle/guided")(0,error);assert(!error.empty()&&pending==m.tesla->guided.request&&peer_open_calls==0);error.clear();}
  auto main_pass=[&]{auto*worker=running_task;running_task=nullptr;m.loop();++main_ticks;running_task=worker;};
  auto terminal=[&](int status){ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect.status=status;e.connect.conn_handle=4;test_gap(&e,test_gap_arg);};
  auto unannounced_disconnect=[&](int reason){test_unannounced_handle=-1;ble_gap_event e{};e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.reason=reason;e.disconnect.conn.conn_handle=4;test_gap(&e,test_gap_arg);};
  test_submit=[&](int stage){
   if(stage==1){opened=millis();if(normal)terminal_at=opened+6500;if(refused)test_api_rc=7;if(pre)link_at=opened+300;
    // A stale success of the previous attempt's generation is rejected by epoch.
    if(previous_epoch)TeslaVehicle::gatt_callback({TeslaNimbleEvent::Open,previous_epoch,4,0,0,nullptr,0});
    if(blocked){clock_ms.fetch_add(10001);main_pass();assert(m.tesla->runtime_owner.get()==r&&!r->worker_stopped.load()&&!r->task_handle->deleted);}return;}
   if(stage==7){++cancels;if(!missing&&!pre)terminal_at=millis()+120;if(pre_late)late_connect_at=millis()+50; // SDK posts the deferred CONNECT regardless of cancel
    if(mode=="connect-repeat-cancel-error")test_api_rc=7;
    if(mode=="connect-repeat-cancel-already")test_api_rc=2;
    return;}
   if(stage==6&&pre){++terminates;assert(r->operation.generation()&&!r->operation.retired());
    if(pre_stall)pre_disconnect_at=millis()+120;
    if(pre_late&&late_connected)pre_disconnect_at=millis()+100;
    return;}
   if(stage==6&&success){disconnect_at=millis()+180;return;}
   nimble_auto_submit(stage);
  };
  on_task_delay=[&](uint32_t){
   if(identity&&opened&&!identity_changed&&millis()-opened>=200){identity_changed=true;++ev.generation;m.refresh_live_session();}
   if(terminal_at&&millis()>=terminal_at){terminal_at=0;test_api_rc=0;terminal(success?0:13);}
   if(disconnect_at&&millis()>=disconnect_at){disconnect_at=0;nimble_force_disconnect();}
   if(link_at&&millis()>=link_at){link_at=0;test_unannounced_handle=4;test_cancel_rc=BLE_HS_EALREADY;if(pre_disconnect)pre_disconnect_at=millis()+600;}
   if(late_connect_at&&millis()>=late_connect_at){late_connect_at=0;test_unannounced_handle=-1;late_connected=true;terminal(0);}
   if(pre_disconnect_at&&millis()>=pre_disconnect_at){pre_disconnect_at=0;if(late_connected){late_connected=false;nimble_force_disconnect();}else unannounced_disconnect(0x200+0x3e);}
   main_pass();
  };
  on_delay=[&]{
   if(!running_task||running_task->name!="tesla_ble"||finished)return;
   main_pass();if(r->profile_pending.load())return;
   if(!committed){assert(m.tesla->guidance.get("ready")->asBool());command("commit");++attempts;committed=true;return;}
   if(m.tesla->state.get("pairing_pending")->asBool())return;
   if(!opened)return;
   m.publish_bluetooth_state();
   std::printf("CONNECT_REPEAT mode=%s attempt=%u elapsed=%u cancel=%u terminate=%u retired=%d stop=%d restart=%d flags=%u main=%u\n",mode.c_str(),attempts,millis()-opened,cancels,terminates,r->operation.retired(),r->stop_requested.load(),m.tesla->state.get("restart_required")->asBool(),unsigned(m.bluetooth.central()->connect_diagnostic().flags),main_ticks);std::fflush(stdout);
   if(quarantine){assert(r->operation.retired()&&r->stop_requested.load()&&m.tesla->state.get("restart_required")->asBool());assert(m.tesla->state.get("timeout_operation")->asUint()==2&&m.tesla->state.get("timeout_cause")->asString()=="gatt_operation");assert(!m.bluetooth.central()->idle());
    if(!blocked)assert(m.tesla->state.get("last_error")->asString()=="Tesla BLE connection attempt undrained; ownership retained; reboot required");
    finished=true;return;}
   assert(!r->operation.retired()&&!r->stop_requested.load()&&!m.tesla->state.get("restart_required")->asBool());
   assert(m.bluetooth.central()->idle()&&r->epoch==epoch&&!r->authorized.load());
   assert(!m.tesla->state.get("pair_tx_complete")->asBool()&&r->pair_tx_attempted.load()==0);
   if(!identity)assert(m.tesla->state.get("last_error")->asString()=="Could not connect to Tesla for pairing");
   if(!identity)assert(m.tesla->state.get("pair_outcome")->asString()=="connection_failed");
   assert(millis()-opened<10000);assert(cancels==(normal||refused||pre_disconnect?0:attempts));
   if(pre){const auto f=m.bluetooth.central()->connect_diagnostic();if(pre_late)assert((f.flags&(16|32))==(16|32)&&!(f.flags&256));else assert((f.flags&256)&&!(f.flags&16));
    assert(pre_disconnect?terminates==0&&!(f.flags&64):pre_late?f.cancel_rc==BLE_HS_EALREADY:(f.flags&(64|128))==(64|128)&&f.cancel_rc==BLE_HS_EALREADY);}
   assert(LittleFS.files.at("/config/tesla_p00_112233445500_private_key")==key&&ev.applied==0);
   ++completed;if(completed==6||identity){finished=true;r->stop_requested=true;return;}
   previous_epoch=r->physical_generation.load();opened=0;committed=false;test_api_rc=0;test_cancel_rc=-1;test_unannounced_handle=-1;late_connected=false;command("prepare_pair");
  };
  run_task(r->task_handle);on_delay={};on_task_delay={};on_gatt_bytes={};if(blocked){assert(r->operation.retired()&&r->worker_stopped.load()&&m.tesla->runtime_owner.get()==r);finished=true;}assert(finished&&(quarantine||identity||completed==6));test_api_rc=0;
  if(quarantine){
   const auto flags=m.tesla->state.get("connect_flags")->asUint();
   assert(flags==(blocked?1U:pre_undrained?207U:15U));assert(m.tesla->state.get("connect_attempt")->asUint()==1);
   if(pre_undrained){unannounced_disconnect(0x200+0x16);}else terminal(13);
   assert(m.bluetooth.central()->idle());m.loop();assert(m.tesla->state.get("connect_flags")->asUint()==flags);
  }
  test_cancel_rc=-1;test_unannounced_handle=-1;late_connected=false;
  m.loop();if(quarantine){
   const auto first=m.tesla->state.get("last_error")->asString();
   std::strcpy(r->teardown_error,"Bluetooth teardown failed; ownership retained; reboot required");r->teardown.complete(false);m.loop();
   assert(m.tesla->state.get("last_error")->asString()==first && "secondary teardown must not erase initiating OPEN fault");
  }else{if(r->teardown_handle)run_task(r->teardown_handle);m.loop();}
  if(pre)std::printf("CONNECT_REPEAT_PRECONNECT mode=%s PASS completed=%u attempts=%u\n",mode.c_str(),completed,attempts);
 }
