#if TESLA_BLE_USE_NIMBLE
#include "../evse_common/manager_mode_observation.h"
#ifdef TESLA_BLE_MODULE_HOST_TEST
static ManagerModeObservation host_remote_manager;
static float host_remote_power=NAN;
static uint64_t host_remote_power_received=0;
static uint8_t host_remote_capabilities=0;
#else
#include "modules/evse_common/evse_common.h"
extern EvseCommon evse_common;
#endif
#ifndef WARP_REMOTE_CONFIG_PATH
#define WARP_REMOTE_CONFIG_PATH "/data/config/warp_remote_enabled"
#endif
static uint64_t remote_now_ms(){
#ifdef TESLA_BLE_MODULE_HOST_TEST
 return now_us()/1000;
#else
 return now_us().to<millis_t>().as<uint64_t>();
#endif
}
void TeslaVehicle::remote_pre_setup(){
 remote_config=Config::Object({{"enable",Config::Bool(false)}});
 remote_update=Config::Object({{"enable",Config::Bool(false)}});
 remote_candidate=Config::Object({{"index",Config::Uint(0,0,3)}, {"address",Config::Str("",0,17)}, {"address_type",Config::Uint(0,0,1)}});
 remote_candidates=Config::Object({{"entries",Config::Array({},&remote_candidate,0,4)}});
 remote_binding=Config::Object({{"id",Config::Str("",0,64)},{"name",Config::Str("",0,32)},{"phase",Config::Str("pending",0,16)}});
 remote_bindings=Config::Object({{"entries",Config::Array({},&remote_binding,0,4)}});
 remote_forget=Config::Object({{"id",Config::Str("",0,64)},{"confirmed",Config::Bool(false)}});
 remote_recover=Config::Object({{"id",Config::Str("",0,64)},{"confirmed",Config::Bool(false)}});
 remote_select=Config::Object({{"generation",Config::Uint(0,0,UINT32_MAX)}, {"index",Config::Uint(0,0,3)}});
 remote_state=Config::Object({{"phase",Config::Str("idle",0,24)}, {"busy",Config::Bool(false)},
  {"generation",Config::Uint(0,0,UINT32_MAX)}, {"remaining_ms",Config::Uint(0,0,UINT32_MAX)}, {"persistence_error",Config::Bool(false)},
  {"trusted",Config::Bool(false)}, {"normal",Config::Bool(false)}, {"binding_capacity",Config::Uint(4,4,4)}, {"enrollment_ready",Config::Bool(false)}});
}
bool TeslaVehicle::remote_entropy(void *,uint8_t *out,size_t n){return TeslaBleTransportAdapter::random(nullptr,out,n)==0;}
bool TeslaVehicle::remote_enrollment_save(void *ctx,const uint8_t *peer,const uint8_t *key,unsigned phase){
 auto &s=*static_cast<TeslaVehicle *>(ctx);if(!s.remote_bindings_ok)return false;
 bool ok=phase==1?s.remote_store.prepare(peer,key,"WARP Fernbedienung"):
         phase==2?s.remote_store.commit_prepared(peer,key):
         phase==3&&s.remote_store.verify_prepared(peer,key)&&s.remote_store.activate(peer);
 if(s.remote_store.quarantined())s.remote_bindings_ok=false;
 return ok;
}
bool TeslaVehicle::remote_recovery_load(void *ctx,const uint8_t *wanted,uint8_t *id,uint8_t *key,unsigned *phase){
 auto&s=*static_cast<TeslaVehicle*>(ctx);
 // Reload exact dedicated store, not stale in-memory stage. Main task only.
 if(!wanted||!s.remote_store.load())return false;
 auto*b=s.remote_store.find(wanted);if(!b)return false;
 memcpy(id,b->peer,32);memcpy(key,b->key,32);*phase=b->state==2?3:b->state==3?2:1;return true;
}
bool TeslaVehicle::remote_recovery_save(void *ctx,unsigned phase){
 auto&s=*static_cast<TeslaVehicle*>(ctx);bool ok=s.remote_store.recover_stage(s.remote_recover_id,phase);
 if(s.remote_store.quarantined())s.remote_bindings_ok=false;return ok;
}
bool TeslaVehicle::remote_session_load(void *ctx,const uint8_t *wanted,uint8_t *id,uint8_t *key){
 auto&s=*static_cast<TeslaVehicle*>(ctx);if(!wanted||!s.remote_store.load())return false;
 auto*b=s.remote_store.find(wanted);if(!b||b->state!=2)return false;
 memcpy(id,b->peer,32);memcpy(key,b->key,32);return true;
}
bool TeslaVehicle::remote_session_reserve(void *ctx,const uint8_t *id,uint64_t *value){
 auto&s=*static_cast<TeslaVehicle*>(ctx);bool ok=s.remote_store.reserve(id,*value);if(s.remote_store.quarantined())s.remote_bindings_ok=false;return ok;
}
static ManagerModeObservation remote_manager_observed(){
#ifdef TESLA_BLE_MODULE_HOST_TEST
 return host_remote_manager;
#else
 return evse_common.get_manager_mode_observation();
#endif
}
bool TeslaVehicle::remote_session_pending(void *ctx){return static_cast<TeslaVehicle*>(ctx)->remote_response_waiting;}
void TeslaVehicle::remote_session_abort(void *ctx){auto&s=*static_cast<TeslaVehicle*>(ctx);s.remote_response_waiting=false;s.remote_response_until=s.remote_response_revision=0;}
bool TeslaVehicle::remote_session_respond(void *ctx,const uint8_t *request,uint8_t *out){
 auto&s=*static_cast<TeslaVehicle*>(ctx);if(s.remote_response_waiting)return false;
 if(request[0]==3){
  memcpy(out,request,16);memset(out+16,0,48);
  const uint8_t*id=s.remote_session.identity();const auto*binding=s.remote_store.find(id);
  if(!s.remote_operations_ok||!s.remote_available()||!binding||binding->state!=2)return true;
  if(!s.remote_operations.load()){s.remote_operations_ok=false;return true;}
  uint64_t operation=0;for(unsigned i=0;i<8;i++)operation=(operation<<8)|request[1+i];
  const auto admission=s.remote_operations.begin(id,operation,3);
  if(admission==RemoteOperationJournal::Fresh){
   // Harmless diagnostic only: qualify durable issue/result gates, NO writer.
   if(!s.remote_operations.issue(id,operation)||!s.remote_operations.finish(id,operation,1)){s.remote_operations_ok=false;return true;}
  }
  if(s.remote_operations.quarantined()){s.remote_operations_ok=false;return true;}
  out[38]=1;out[39]=2;
  const auto*record=s.remote_operations.find(id);
  if(admission==RemoteOperationJournal::Fresh||admission==RemoteOperationJournal::Existing)out[39]=record&&record->phase==3?record->outcome:0;
  return true;
 }
 if(!remote_session_snapshot(ctx,request,out))return false;
 // Read-only bounded wait for one fresh manager observation. No charge request.
 const auto observation=remote_manager_observed();const uint64_t now=remote_now_ms();
#ifndef TESLA_BLE_MODULE_HOST_TEST
 if(!evse_common.get_management_enabled())return true;
#endif
 if(request[0]!=1||out[16]||observation.fresh(now)||now>UINT64_MAX-1000)return true;
 s.remote_response_waiting=true;s.remote_response_until=now+1000;s.remote_response_revision=observation.revision();return false;
}
int TeslaVehicle::remote_session_finish(void *ctx,const uint8_t *request,uint8_t *out){
 auto&s=*static_cast<TeslaVehicle*>(ctx);if(!s.remote_response_waiting||!s.remote_available())return -1;
 const uint64_t now=remote_now_ms();
 if(now>=s.remote_response_until){memcpy(out,request,16);memset(out+16,0,48);remote_session_abort(ctx);return 1;}
 if(!remote_session_snapshot(ctx,request,out))return -1;
 const auto observation=remote_manager_observed();
 if(out[16]&&observation.revision()>s.remote_response_revision){remote_session_abort(ctx);return 1;}
 // Timeout is handled BEFORE considering a late observation above.
 return 0;
}
bool TeslaVehicle::remote_session_snapshot(void *ctx,const uint8_t *request,uint8_t *out){
 if(request[0]<1||request[0]>3)return false;
 // Only independent manager echo evidence, never requested charge_mode Config.
 memcpy(out,request,16);memset(out+16,0,48);
#ifdef TESLA_BLE_MODULE_HOST_TEST
 const auto observed=host_remote_manager;
#else
 const auto observed=evse_common.get_manager_mode_observation();
 if(request[0]==1&&!evse_common.get_management_enabled())return true;
#endif
 const uint64_t now=remote_now_ms();
 if(request[0]==2){
  auto &s=*static_cast<TeslaVehicle*>(ctx);const auto live=current_ev_identity();
  if(live.index<0||!live.mac||live.generation==UINT64_MAX||ev.get_profile_mac(live.index)!=live.mac)return true;
  unsigned soc=0;uint32_t age=0;
  auto put32=[&](unsigned at,uint32_t v){for(unsigned i=0;i<4;i++)out[at+i]=(uint8_t)(v>>(24-8*i));};
  if(s.remote_soc.read(now,live.generation,live.index,live.mac,soc,age)){out[23]=1;out[24]=(uint8_t)soc;put32(25,age);}
  float watts=NAN;uint64_t stamp=0;bool fresh=false;
#ifdef TESLA_BLE_MODULE_HOST_TEST
  watts=host_remote_power;stamp=host_remote_power_received;fresh=true;
#else
  fresh=evse_common.get_charger_meter_power_observation(&watts,&stamp,5000_ms)==MeterValueAvailability::Fresh;
#endif
  // Measured EVSE input power, not a requested current or simulated car value.
  if(live.index>=0&&live.mac&&fresh&&std::isfinite(watts)&&watts>=0&&(double)watts<=4294967295.0&&now>=stamp&&now-stamp<=5000){out[29]=1;put32(30,(uint32_t)std::llround(watts));put32(34,(uint32_t)(now-stamp));}
  if(live!=current_ev_identity())memset(out+16,0,48);
  // Rating remains unavailable until installed phase/rating evidence exists.
 }
 if(request[0]==1&&observed.fresh(now)&&observed.mode()<=2){
  out[16]=1;out[17]=observed.mode()==1?0:observed.mode()==2?1:2;
  uint32_t age=(uint32_t)(now-observed.received_at());for(unsigned i=0;i<4;i++)out[18+i]=(uint8_t)(age>>(24-8*i));out[22]=1; // native CM/LAN source, not crypto evcc attestation
  const auto live=current_ev_identity();uint8_t capabilities=0;
  if(live.index>=0&&live.mac&&live.generation!=UINT64_MAX&&ev.get_profile_mac(live.index)==live.mac){
#ifdef TESLA_BLE_MODULE_HOST_TEST
   capabilities=host_remote_capabilities;
#else
   capabilities=evse_common.get_remote_mode_capabilities();
#endif
  }
  if(live==current_ev_identity()&&capabilities&&!(capabilities&~7)){out[40]=capabilities;out[41]=1;}
 }
 return true;
}
void TeslaVehicle::remote_load(){
 bool enabled=false;
 BluetoothConfigStore store(WARP_REMOTE_CONFIG_PATH,remote_storage_quarantined);
 remote_commit_ok=store.load(enabled);remote_storage_quarantined=store.io_quarantined();
 remote_config.get("enable")->updateBool(remote_commit_ok&&enabled);
 remote_bindings_ok=remote_store.load();remote_publish_bindings();
 remote_operations_ok=remote_operations.load(); // corruption gates operations only, never tracker/phone keys
 remote_setup.set_enrollment(remote_enrollment.port());
 remote_state.get("enrollment_ready")->updateBool(remote_bindings_ok);
 remote_state.get("persistence_error")->updateBool(!remote_commit_ok||!remote_bindings_ok);
 // No discovery/connect/reception/vehicle work on boot or config restore.
}
bool TeslaVehicle::remote_available()const{
 return remote_bindings_ok&&!remote_store.quarantined()&&remote_commit_ok&&remote_config.get("enable")->asBool()&&bluetooth_config.get("enable")->asBool()&&
        bluetooth.gatt_ready()&&!bluetooth_restart_pending&&
        !(tesla&&(tesla->state.get("action_busy")->asBool()||tesla->state.get("pairing_pending")->asBool()||tesla->state.get("stopping")->asBool()));
}
void TeslaVehicle::remote_publish_bindings(){
 auto entries=remote_bindings.get("entries");entries->setCount(remote_bindings_ok?remote_store.count()+remote_store.prepared_count():0);
 unsigned at=0;
 if(remote_bindings_ok)for(unsigned i=0;i<4;i++){
  const auto &binding=remote_store.slot(i);if(!binding.state)continue;
  char id[65]{};for(unsigned j=0;j<32;j++)snprintf(id+2*j,3,"%02x",binding.peer[j]);
  auto entry=entries->get(at++);entry->get("id")->updateString(id);entry->get("name")->updateString(binding.name);entry->get("phase")->updateString(binding.state==2?"active":binding.state==3?"commit_pending":"prepared");
 }
}
static bool remote_parse_id(const String &id,uint8_t peer[32]){
 if(id.length()!=64)return false;
 for(unsigned i=0;i<32;i++){unsigned value=0;for(unsigned j=0;j<2;j++){
  char c=id[2*i+j];if(c>='0'&&c<='9')value=(value<<4)|(c-'0');else if(c>='a'&&c<='f')value=(value<<4)|(c-'a'+10);else return false;
 }peer[i]=(uint8_t)value;}return true;
}
void TeslaVehicle::remote_register_urls(){
 api.addState("bluetooth/remote_config",&remote_config);
 api.addState("bluetooth/remote_state",&remote_state);
 api.addState("bluetooth/remote_candidates",&remote_candidates);
 api.addState("bluetooth/remote_bindings",&remote_bindings);
 api.addCommand("bluetooth/remote_config_update",&remote_update,{},[this](Language,String &error){
  bool enabled=remote_update.get("enable")->asBool();
  if(enabled&&(!bluetooth_config.get("enable")->asBool()||!bluetooth.gatt_ready())){error=checked_error_string("Bluetooth not active");return;}
  // Disabling closes admission immediately even if persistence fails. It never
  // forgets any remote/phone/Tesla credentials, restarts BT, or changes charging.
  if(!enabled)remote_setup.cancel();
  BluetoothConfigStore store(WARP_REMOTE_CONFIG_PATH,remote_storage_quarantined);
  remote_commit_ok=store.save(enabled);remote_storage_quarantined=store.io_quarantined();
  if(remote_commit_ok)remote_config.get("enable")->updateBool(enabled);
  else{remote_config.get("enable")->updateBool(false);error=checked_error_string("Remote configuration commit failed");}
  remote_next_scan=remote_now_ms()+2000;
  remote_state.get("persistence_error")->updateBool(!remote_commit_ok||!remote_bindings_ok);
 },false);
 api.addCommand("bluetooth/remote_recover",&remote_recover,{},[this](Language,String &error){
  auto*central=bluetooth.central();uint8_t id[32]{};
  bool confirmed=remote_recover.get("confirmed")->asBool();remote_recover.get("confirmed")->updateBool(false);
  if(!confirmed||!remote_parse_id(remote_recover.get("id")->asString(),id)||!remote_available()||!central||
     remote_setup.busy()||!central->idle()||bluetooth.discovery_busy()||!remote_store.find(id)){
   error=checked_error_string("Remote recovery not confirmed or unavailable");return;
  }
  remote_recovery.cancel();remote_normal=false;memcpy(remote_recover_id,id,32);
  if(!remote_recovery.select(id)||!remote_setup.set_enrollment(remote_recovery.port())||!remote_setup.start(*central,remote_now_ms())){
   error=checked_error_string("Remote recovery radio unavailable");return;
  }
  remote_recovering=true;remote_loop();
 },true);
 api.addCommand("bluetooth/remote_start",&empty_command,{},[this](Language,String &error){
  auto *central=bluetooth.central();
  if(remote_available()&&central&&!remote_setup.busy()&&central->idle()&&!bluetooth.discovery_busy()){
   remote_recovering=false;remote_normal=false;remote_setup.set_enrollment(remote_enrollment.port());
  }
  if(!remote_available()||!central||remote_setup.busy()||!central->idle()||bluetooth.discovery_busy()||
     !remote_setup.start(*central,remote_now_ms()))error=checked_error_string("Remote setup unavailable or radio busy");
  remote_loop();
 },true);
 api.addCommand("bluetooth/remote_select",&remote_select,{},[this](Language,String &error){
  uint8_t nonce[16];
  if(!remote_available()||TeslaBleTransportAdapter::random(nullptr,nonce,sizeof(nonce))!=0||
     !remote_setup.select(remote_now_ms(),remote_select.get("generation")->asUint(),remote_select.get("index")->asUint(),nonce))
   error=checked_error_string("Remote candidate expired or unavailable");
  memset(nonce,0,sizeof(nonce));remote_loop();
 },true);
 api.addCommand("bluetooth/remote_forget",&remote_forget,{},[this](Language,String &error){
  const auto id=remote_forget.get("id")->asString();uint8_t peer[32]{};
  bool valid=id.length()==64;
  for(unsigned i=0;valid&&i<32;i++){
   unsigned value=0;
   for(unsigned j=0;j<2;j++){
    char c=id[2*i+j];if(c>='0'&&c<='9')value=(value<<4)|(c-'0');
    else if(c>='a'&&c<='f')value=(value<<4)|(c-'a'+10);else{valid=false;break;}
   }
   peer[i]=static_cast<uint8_t>(value);
  }
  if(!remote_forget.get("confirmed")->asBool()||remote_setup.busy()||!valid||!remote_bindings_ok||!remote_store.forget(peer))
   error=checked_error_string("Remote removal not confirmed or storage unavailable");
  else if(remote_operations_ok&&(remote_store.find(peer)||!remote_operations.retire(peer,true))){
   remote_operations_ok=false;error=checked_error_string("Binding removed; operation metadata cleanup failed");
  }
  remote_forget.get("confirmed")->updateBool(false);
  if(remote_store.quarantined()){remote_bindings_ok=false;remote_state.get("persistence_error")->updateBool(true);}
  remote_publish_bindings();
 },true);
 api.addCommand("bluetooth/remote_cancel",&empty_command,{},[this](Language,String &){remote_setup.cancel();remote_loop();},true);
}
void TeslaVehicle::remote_loop(){
 const uint64_t now=remote_now_ms();
 if(!remote_available())remote_setup.cancel();
 bool was_busy=remote_setup.busy();remote_setup.poll(now);
 if(was_busy&&!remote_setup.busy())remote_next_scan=now+2000;
 if(remote_normal&&remote_setup.busy()){
  if(remote_setup.phase()==RemoteSetup::Scanning&&remote_setup.count()){
   uint8_t nonce[16];if(remote_entropy(nullptr,nonce,sizeof(nonce)))remote_setup.select(now,remote_setup.generation(),0,nonce);memset(nonce,0,16);
  }else if(remote_setup.phase()==RemoteSetup::Scanning&&now>=remote_scan_until)remote_setup.cancel();
 }
 if(remote_available()&&!remote_setup.busy()&&remote_store.count()&&now>=remote_next_scan){
  auto*c=bluetooth.central();remote_next_scan=now+5000;
  if(c&&c->idle()&&!bluetooth.discovery_busy()){
   remote_session.cancel();remote_normal=true;remote_recovering=false;
   if(remote_setup.set_enrollment(remote_session.port())&&remote_setup.start(*c,now,true))remote_scan_until=now+3000;
  }
 }
 const char *phase="idle";
 switch(remote_setup.phase()){
  case RemoteSetup::Scanning:phase="scanning";break;
  case RemoteSetup::Choosing:phase="choosing";break;
  case RemoteSetup::Connecting:phase="confirm_on_tag";break;
  case RemoteSetup::SessionComplete:phase="session_complete";break;
  case RemoteSetup::ConsentReceived:phase="consent_untrusted";break;
  case RemoteSetup::Paired:phase="paired_tofu";break;
  case RemoteSetup::Failed:phase="failed";break;
  case RemoteSetup::Cancelled:phase="cancelled";break;
  case RemoteSetup::Draining:phase="draining";break;
  case RemoteSetup::Expired:phase="expired";break;
  default:break;
 }
 remote_state.get("phase")->updateString(phase);
 const auto *paired=remote_store.find(remote_recovering?remote_recovery.identity():remote_enrollment.identity());
 remote_state.get("trusted")->updateBool(remote_setup.phase()==RemoteSetup::Paired&&paired&&paired->state==2);
 remote_state.get("persistence_error")->updateBool(!remote_commit_ok||!remote_bindings_ok);
 remote_publish_bindings();
 remote_state.get("normal")->updateBool(remote_normal);
 remote_state.get("busy")->updateBool(remote_setup.busy());
 remote_state.get("generation")->updateUint(remote_setup.generation());
 remote_state.get("remaining_ms")->updateUint(remote_setup.remaining(now));
 auto entries=remote_candidates.get("entries");
 if(entries->count()!=remote_setup.count())entries->setCount(remote_setup.count());
 for(unsigned i=0;i<remote_setup.count();i++){
  const auto &peer=remote_setup.candidate(i);char address[18];
  snprintf(address,sizeof(address),"%02X:%02X:%02X:%02X:%02X:%02X",peer.address[0],peer.address[1],peer.address[2],peer.address[3],peer.address[4],peer.address[5]);
  auto entry=entries->get(i);entry->get("index")->updateUint(i);entry->get("address")->updateString(address);entry->get("address_type")->updateUint(peer.type);
 }
 // No command/status authorizer yet: enrollment never writes charging.
}
#endif
