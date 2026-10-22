// Production guidance + profiles validator, synthetic filesystem/EV boundaries.
 static void bootstrap(TeslaVehicle &m,const std::string &mode) {
  HostBluetoothDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);
  ev.index=0;ev.mac=A;ev.profiles.erase(1);
  auto entries=m.tesla->profiles.get("entries");assert(entries->count()==0);
  if(mode=="bootstrap-hex-mac")ev.profiles[0]=0xAABBCCDD0011ULL;
  if(mode=="bootstrap-second") {
   assert(entries->setCount(1));entry(entries->get(size_t{0}),0);seed(0);
   m.publish_guidance();ev.profiles[1]=B; // added AFTER first inventory
  }
  const int index=mode=="bootstrap-second"?1:0;const auto mac=index?B:A;
  const std::string prefix=index?"/config/tesla_p01_112233445501_":"/config/tesla_p00_112233445500_";
  if(mode=="bootstrap-retained" || mode=="bootstrap-cross-vin")seed(0);
  if(mode=="bootstrap-orphan")LittleFS.files[prefix+"session_vcsec.tmp"]={1,2,3};
  if(mode=="bootstrap-unknown")LittleFS.files[prefix+"private_key"]={1,2,3};
  if(mode=="bootstrap-io"){seed(0);fs_fault=[](const std::string &op){return op.rfind("open:",0)==0;};}
  const auto before=LittleFS.files;const auto removals=LittleFS.removals;
  m.publish_guidance();auto rows=m.tesla->guidance.get("profiles");Config *row=nullptr;
  for(size_t i=0;i<rows->count();++i)if(rows->get(i)->get("ev_index")->asInt()==index)row=rows->get(i);
  assert(row && "Clean existing WARP namespace needs authoritative guidance without Tesla mapping");
  assert(row->get("ev_mac")->asString()==(mode=="bootstrap-hex-mac"?"AA:BB:CC:DD:00:11":index?"11:22:33:44:55:01":"11:22:33:44:55:00"));
  const bool clean=mode=="bootstrap-hex-mac" || mode=="bootstrap-clean" || mode=="bootstrap-second" || mode=="bootstrap-stale";
  assert(row->get("namespace_clean")->asBool()==clean);
  assert((row->get("key_presence")->asString()=="absent")==clean);
  std::printf("BOOTSTRAP_EVIDENCE {\"case\":\"%s\",\"ev_index\":%d,\"ev_mac\":\"%s\",\"namespace_clean\":%s,\"paired\":%s,\"key_presence\":\"%s\",\"binding_fresh\":false}\n",
   mode.c_str(),index,row->get("ev_mac")->asString().c_str(),row->get("namespace_clean")->asBool()?"true":"false",
   row->get("paired")->asBool()?"true":"false",row->get("key_presence")->asString().c_str());
  if(mode=="bootstrap-stale") {
   ev.profiles[0]=B;++ev.generation;m.publish_guidance();
   assert(rows->get(size_t{0})->get("ev_mac")->asString()=="11:22:33:44:55:01");
  }
  auto update=Config::Object({{"entries",Config::Array({},&m.tesla->profile,0,EV_MAX_EVS)}});
  assert(update.get("entries")->setCount(index?2:1));
  for(int i=0;i<=index;++i){auto p=update.get("entries")->get(i);entry(p,i);p->get("ble_address")->updateString("");p->get("enable")->updateBool(false);}
  if(mode=="bootstrap-second")*update.get("entries")->get(size_t{0})=*entries->get(size_t{0});
  if(mode=="bootstrap-cross-vin")update.get("entries")->get(size_t{0})->get("vin")->updateString("5YJ3E1EA7KF000009");
  if(mode=="bootstrap-hex-mac")update.get("entries")->get(size_t{0})->get("ev_mac")->updateString("AA:BB:CC:DD:00:11");
  const auto error=m.validate_profiles(update,ConfigSource::API);
  assert(error.empty()==(mode=="bootstrap-hex-mac" || mode=="bootstrap-clean" || mode=="bootstrap-second"));
  fs_fault={};
  assert(LittleFS.files==before && LittleFS.removals==removals && api.written_configs.empty());
  assert(!m.tesla->runtime_owner.get() && !m.service_start_requested && !peer_open_calls && !scan_start_calls && !ev.applied);
  (void)mac;std::printf("BOOTSTRAP_PASS %s read-only namespace/validator/no-radio\n",mode.c_str());
 }
