// Production manual-profile path. Only SDK/OS callbacks are controlled.
 static void subscription(TeslaVehicle &m,const std::string &mode) {
  seed(0);seed(1);const auto saved=LittleFS.files;
  HostBluetoothDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);
  host_single_link_enforced=true;host_rx_properties=mode=="neither"?0:ESP_GATT_CHAR_PROP_BIT_INDICATE;
  gatt_close_result=ESP_OK;
  ev.index=0;ev.mac=A;explicit_scan(m,0);String error;assert(m.select_profile(0,false,error));
  auto *r=m.tesla->runtime_owner.get();assert(r && !r->vehicle);
  unsigned phase=0;bool in_hook=false,done=false;
  on_gatt_close=[&] {
   assert(host_physical_links==1);
   const auto opens=peer_open_calls;
   esp_ble_gattc_cb_param_t p;p.close.conn_id=4;p.disconnect.conn_id=4;
   if(mode=="reconnect-reverse")TeslaVehicle::gatt_callback(ESP_GATTC_DISCONNECT_EVT,1,&p);
   else TeslaVehicle::gatt_callback(ESP_GATTC_CLOSE_EVT,1,&p);
   assert(peer_open_calls==opens && host_physical_links==1);
   // Capacity is not released until the second physical close event.
   --host_physical_links;
   if(mode=="reconnect-reverse")TeslaVehicle::gatt_callback(ESP_GATTC_CLOSE_EVT,1,&p);
   else TeslaVehicle::gatt_callback(ESP_GATTC_DISCONNECT_EVT,1,&p);
  };
  on_delay=[&] {
   if(in_hook || done || r->profile_pending.load())return;
   in_hook=true;
   assert(r->profile_ready.load() && r->authorized.load());
   if(mode=="wrong-descriptor" || mode=="stale-setup") {
    on_descriptor_write=[&](uint16_t connection,uint16_t handle) {
     esp_ble_gattc_cb_param_t p;p.write.conn_id=connection+1;p.write.handle=handle;
     TeslaVehicle::gatt_callback(ESP_GATTC_WRITE_DESCR_EVT,1,&p);
     p.write.conn_id=connection;p.write.handle=handle+1;TeslaVehicle::gatt_callback(ESP_GATTC_WRITE_DESCR_EVT,1,&p);
     assert(!r->connected.load());
     if(mode=="stale-setup") {r->session.disconnect();r->advance_connection();}
     // Wrong descriptor/connection alone must never complete the operation.
    };
   }
   const bool ok=m.ensure_connected();
   if(mode=="neither" || mode=="wrong-descriptor" || mode=="stale-setup") {
    assert(!ok && !r->connected.load());
    assert(host_cccd_writes==(mode=="neither"?0u:1u));done=true;r->stop_requested.store(true);in_hook=false;return;
   }
   assert(ok && host_physical_links==1 && host_cccd_value==2 && peer_open_calls==phase+1);
   const auto generation=r->physical_generation.load();
   uint8_t bytes[]={0,1,0};esp_ble_gattc_cb_param_t p;p.notify.conn_id=4;p.notify.handle=20;p.notify.value=bytes;p.notify.value_len=sizeof(bytes);p.notify.is_notify=false;memcpy(p.notify.remote_bda,r->remote_address,6);
   // Wrong interface/connection/handle/address never reach the queue.
   TeslaVehicle::gatt_callback(ESP_GATTC_NOTIFY_EVT,2,&p);
   ++p.notify.conn_id;TeslaVehicle::gatt_callback(ESP_GATTC_NOTIFY_EVT,1,&p);--p.notify.conn_id;
   ++p.notify.handle;TeslaVehicle::gatt_callback(ESP_GATTC_NOTIFY_EVT,1,&p);--p.notify.handle;
   p.notify.remote_bda[0]^=1;TeslaVehicle::gatt_callback(ESP_GATTC_NOTIFY_EVT,1,&p);p.notify.remote_bda[0]^=1;
   TeslaVehicle::RxFragment fragment;assert(xQueueReceive(r->queues.rx(),&fragment,0)!=pdTRUE);
   TeslaVehicle::gatt_callback(ESP_GATTC_NOTIFY_EVT,1,&p);
   assert(xQueueReceive(r->queues.rx(),&fragment,0)==pdTRUE && fragment.connection_generation==generation && fragment.length==sizeof(bytes) && !memcmp(fragment.data,bytes,sizeof(bytes)));
   // Queue one old indication; switch/close must drain it before a successor.
   TeslaVehicle::gatt_callback(ESP_GATTC_NOTIFY_EVT,1,&p);
   if(phase<2) {
    const int next=phase==0?1:0;++ev.generation;ev.index=next;ev.mac=next?B:A;
    m.loop();explicit_scan(m,next);error.clear();assert(m.select_profile(next,false,error));
    m.update_active_vehicle();m.apply_profile();m.publish_profile();
    assert(host_physical_links==0 && !r->connected.load() && r->physical_generation.load()==generation);
    assert(xQueueReceive(r->queues.rx(),&fragment,0)!=pdTRUE);
    // Late predecessor event rejected before successor readiness.
    TeslaVehicle::gatt_callback(ESP_GATTC_NOTIFY_EVT,1,&p);
    assert(xQueueReceive(r->queues.rx(),&fragment,0)!=pdTRUE);
    ++phase;
   } else {
    assert(LittleFS.files==saved);m.disconnect_internal();assert(host_physical_links==0);done=true;r->stop_requested.store(true);
   }
   in_hook=false;
  };
  run_task(r->task_handle);on_delay={};on_descriptor_write={};
  if(!done)std::fprintf(stderr,"subscription stopped phase=%u stop=%d retired=%d close_retired=%d terminal=%s state=%s\n",phase,int(r->stop_requested.load()),int(r->operation.retired()),int(r->retirement_operation.retired()),r->terminal_error,m.tesla->state.get("last_error")->asString().c_str());
  assert(done);
  m.loop();if(r->teardown_handle)run_task(r->teardown_handle);m.loop();on_gatt_close={};host_single_link_enforced=false;
  assert(ev.applied==0);std::printf("SUBSCRIPTION %s PASS capability/descriptor/generation or real A-B-A single-capacity close+RX retirement\n",mode.c_str());
 }
