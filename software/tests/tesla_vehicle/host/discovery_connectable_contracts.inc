// Discovery classification of a VIN-matching but NON-connectable Tesla beacon, through the
// real tesla_vehicle/scan command, NimBLE adapter scan callback, discovery policy and API
// publication. Official reference (teslamotors/vehicle-command a4b43c1e,
// pkg/connector/ble/ble.go tryToConnect): a beacon with the VIN local name that is not
// connectable means "the vehicle is already connected to the maximum number of BLE devices"
// (ErrMaxConnectionsExceeded) - the vehicle is present, not absent.
// Field link (unproven): connect ETIMEOUT(13) x3 followed by searches reporting "not found".
// Never admits adoption/pairing from a non-connectable beacon (controls below).
 struct DiscoveryAdv { uint8_t event; bool name; const char *vin; uint8_t first; };
 static void discovery_scan(TeslaVehicle &m,const std::vector<DiscoveryAdv> &advs,bool scan_rsp_name=false) {
  auto*p=m.find_profile(0);auto&sc=m.tesla->scan_command;String error;
  sc.get("vin")->updateString(p->get("vin")->asString());sc.get("ev_index")->updateInt(0);sc.get("ev_mac")->updateString("11:22:33:44:55:00");
  const int before=test_scans;error.clear();api.commands.at("tesla_vehicle/scan")(0,error);
  if(!error.empty()){std::printf("DISCOVERY_CONNECTABLE_FAIL scan rejected: %s\n",error.c_str());std::exit(1);}
  auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(!test_scanning&&std::chrono::steady_clock::now()<end)std::this_thread::yield();assert(test_scanning&&test_scans==before+1);
  const auto yielded=host_bt_yields.load(std::memory_order_acquire);while(host_bt_yields.load(std::memory_order_acquire)==yielded&&std::chrono::steady_clock::now()<end)std::this_thread::yield();
  for(const auto &a:advs){
   char name[19]{};assert(tesla_ble_vehicle_name(a.vin,name));
   uint8_t data[20]={19,9};memcpy(data+2,name,18);const uint8_t flags[3]={2,1,6};
   ble_gap_event e{};e.type=BLE_GAP_EVENT_DISC;e.disc.addr.val[5]=a.first;for(int i=0;i<5;++i)e.disc.addr.val[i]=6-i;e.disc.addr.type=0;e.disc.rssi=-60;
   e.disc.event_type=a.event;
   if(a.name&&!(scan_rsp_name&&a.event==BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND)){e.disc.data=data;e.disc.length_data=20;}else{e.disc.data=flags;e.disc.length_data=3;}
   test_scan(&e,test_scan_arg);
   if(scan_rsp_name&&a.event==BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND){e.disc.event_type=BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP;e.disc.data=data;e.disc.length_data=20;test_scan(&e,test_scan_arg);}
  }
  ble_gap_event done{};done.type=BLE_GAP_EVENT_DISC_COMPLETE;test_scanning=false;test_scan(&done,test_scan_arg);
  TeslaBleDiscovery::Snapshot s{};while(std::chrono::steady_clock::now()<end){if(m.bluetooth.discovery_snapshot(s)&&s.status==TeslaBleDiscovery::Status::Complete)break;std::this_thread::yield();}
  assert(s.status==TeslaBleDiscovery::Status::Complete);m.publish_discovery();
 }
 static void discovery_connectable_flow(TeslaVehicle &m,const std::string &mode) {
  HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);ev.index=0;ev.mac=A;seed(0);const auto files=LittleFS.files;
  auto*p=m.find_profile(0);const std::string vin=p->get("vin")->asString(),address=p->get("ble_address")->asString();
  auto &state=m.tesla->discovery_state;
  auto verdict=[&](const char *label,bool want_flag,bool want_match){
   auto *rows=state.get("targets");unsigned matches=0;for(size_t i=0;i<rows->count();++i)if(rows->get(i)->get("matches_vin")->asBool()){++matches;assert(rows->get(i)->get("ble_address")->asString()==address);}
   const Config *flag=state.has("vin_not_connectable")?state.get("vin_not_connectable"):nullptr;
   const bool admitted=m.fresh_binding(0,A,vin.c_str(),address.c_str(),0,state.get("revision")->asUint());
   std::printf("DISCOVERY_CONNECTABLE case=%s status=%s rows=%u matches=%u flag=%s admitted=%d\n",label,state.get("status")->asString().c_str(),unsigned(rows->count()),matches,
    flag?(flag->asBool()?"true":"false"):"absent",int(admitted));std::fflush(stdout);
   // Admission guards are never widened: only a connectable unique VIN match is adoptable/admitted.
   assert(matches==(want_match?1U:0U)&&admitted==want_match);
   if(!flag||flag->asBool()!=want_flag){std::printf("DISCOVERY_CONNECTABLE_RED mode=%s case=%s present_vehicle_reported_as_absent\n",mode.c_str(),label);std::fflush(stdout);std::_Exit(3);}
  };
  const char *other="5YJ3E1EA7KF000002";
  // Field case: VIN beacon present, non-connectable (vehicle at max BLE connections).
  discovery_scan(m,{{BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND,true,vin.c_str(),1}});verdict("nonconn",true,false);
  discovery_scan(m,{{BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND,true,vin.c_str(),1}});verdict("scannable",true,false);
  discovery_scan(m,{{BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND,true,vin.c_str(),1}},true);verdict("scannable-rsp-name",true,false);
  // Controls: connectable beacon unchanged; mixed at same address -> connectable match; other VIN -> nothing.
  discovery_scan(m,{{BLE_HCI_ADV_RPT_EVTYPE_ADV_IND,true,vin.c_str(),1}});verdict("connectable",false,true);
  discovery_scan(m,{{BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND,true,vin.c_str(),1},{BLE_HCI_ADV_RPT_EVTYPE_ADV_IND,true,vin.c_str(),1}});verdict("mixed-same-address",false,true);
  discovery_scan(m,{{BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND,true,other,1}});verdict("other-vin-nonconn",false,false);
  discovery_scan(m,{});verdict("empty",false,false);
  // A connectable match plus a non-connectable same-VIN advertiser elsewhere stays fail-closed-unique on the connectable one.
  discovery_scan(m,{{BLE_HCI_ADV_RPT_EVTYPE_ADV_IND,true,vin.c_str(),1},{BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND,true,vin.c_str(),2}});verdict("connectable-plus-foreign-nonconn",false,true);
  assert(LittleFS.files==files&&peer_open_calls==0&&ev.applied==0);
  std::printf("DISCOVERY_CONNECTABLE_PASS mode=%s\n",mode.c_str());std::fflush(stdout);
 }
