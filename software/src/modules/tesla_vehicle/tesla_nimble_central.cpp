#if defined(TESLA_BLE_USE_NIMBLE) && TESLA_BLE_USE_NIMBLE
#include "tesla_nimble_central.h"
#include "bluetooth_memory.h"
#include <cstring>
#ifndef TESLA_NIMBLE_HOST_TEST
#include <esp_bt.h>
#endif
std::atomic<TeslaNimbleCentral *> TeslaNimbleCentral::owner{nullptr};
namespace {
#if WARP_FEATURE_TESLA
ble_uuid128_t uuid(uint8_t suffix){ble_uuid128_t u{};u.u.type=BLE_UUID_TYPE_128;const uint8_t a[16]={0x1e,0xb9,0xf8,0xeb,0x0c,0x96,0x88,0x9b,0xf0,0x43,0xd1,0xb2,suffix,0x02,0,0};memcpy(u.value,a,16);return u;}
#endif
#if WARP_FEATURE_REMOTE
ble_uuid128_t remote_uuid(uint8_t suffix){ble_uuid128_t u{};u.u.type=BLE_UUID_TYPE_128;const uint8_t a[16]={0xde,0x53,0x42,0x9a,0xa1,0x76,0x4f,0x7c,0xa0,0x3e,0x72,0x57,suffix,1,0x65,0x9d};memcpy(u.value,a,16);return u;}
#endif
#if WARP_FEATURE_TESLA
const auto service_uuid=uuid(0x11),write_uuid=uuid(0x12),notify_uuid=uuid(0x13);
#endif
#if WARP_FEATURE_REMOTE
const auto remote_service_uuid=remote_uuid(0x11),remote_write_uuid=remote_uuid(0x12),remote_notify_uuid=remote_uuid(0x13);
#endif
#if !WARP_FEATURE_TESLA
const auto &service_uuid=remote_service_uuid,&write_uuid=remote_write_uuid,&notify_uuid=remote_notify_uuid;
#elif !WARP_FEATURE_REMOTE
const auto &remote_service_uuid=service_uuid,&remote_write_uuid=write_uuid,&remote_notify_uuid=notify_uuid;
#endif
}
bool TeslaNimbleCentral::initialize(Handler h){
 TeslaNimbleCentral *expected=nullptr;if(!owner.compare_exchange_strong(expected,this))return false;
 handler=h;cookie.owner=this;
 if(esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_IDLE || nimble_port_init()!=0){failed=true;return false;}
 initialized=true;
#if WARP_FEATURE_REMOTE
 ble_npl_event_init(&scan_barrier,remote_scan_barrier,this);
#endif
 ble_hs_cfg.sync_cb=sync;ble_hs_cfg.reset_cb=reset;
 // No automatic security initiation, no debug keys/bond deletion. Application
 // authentication/keycard is solely TeslaClean; SMP success never grants it.
 ble_hs_cfg.sm_io_cap=BLE_HS_IO_NO_INPUT_OUTPUT;ble_hs_cfg.sm_bonding=0;
 ble_hs_cfg.sm_mitm=0;ble_hs_cfg.sm_sc=1;
 return true;
}
void TeslaNimbleCentral::sync(){auto *c=owner.load();if(!c)return;if(ble_hs_util_ensure_addr(0)||ble_hs_id_infer_auto(0,&c->own_type)){c->failed=true;return;}bluetooth_memory::sample(bluetooth_memory::Stage::HostEnabled,true);c->synchronized=true;}
void TeslaNimbleCentral::reset(int){auto *c=owner.load();if(c){c->failed=true;c->synchronized=false;c->emit(TeslaNimbleEvent::Security,BLE_HS_EUNKNOWN);}}
void TeslaNimbleCentral::run(){running=true;nimble_port_run();running=false;}
bool TeslaNimbleCentral::quiesce(){
 retiring=true;synchronized=false;
 if(!initialized)return false;
 if(!busy.load())return true;
#if WARP_FEATURE_REMOTE
 if(remote_scanning.load())return cancel_remote_scan();
#endif
 // ble_hs_stop counts established links, not an outstanding HCI connect
 // cancel. Keep the host servicing events until that terminal callback.
 const int rc=cancel_open();
 // A terminal callback can win the race with the cancel/terminate call
 // (e.g. SDK reports ENOTCONN). Only the actual idle barrier, never that
 // error alone, establishes released connection ownership.
 return rc==0 || rc==BLE_HS_EALREADY || !busy.load();
}
bool TeslaNimbleCentral::shutdown(){
 synchronized=false;if(!initialized||busy.load())return false;
 if(ble_gap_disc_active() && ble_gap_disc_cancel()!=0)return false;
 return nimble_port_stop()==0 && !busy.load();
}
bool TeslaNimbleCentral::deinitialize(){
 if(!initialized||busy.load()||running.load()||nimble_port_deinit()!=0||esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_IDLE)return false;
 initialized=false;handler=nullptr;owner.store(nullptr);discovery.quiesced();return true;
}
void TeslaNimbleCentral::emit(TeslaNimbleEvent::Kind kind,int status,uint16_t handle,const uint8_t *data,size_t size){
 if(attempt_handler)attempt_handler({kind,cookie.epoch,connection.load(),handle,status,data,size});
 bluetooth_memory::sample(bluetooth_memory::Stage::GattCallback,true,1000);
}
#if WARP_FEATURE_TESLA
bool TeslaNimbleCentral::open(const uint8_t *a,uint8_t type,uint64_t epoch){return !remote_lease.load()&&begin_open(a,type,epoch,false,handler);}
#endif
#if WARP_FEATURE_REMOTE
bool TeslaNimbleCentral::open_remote(const uint8_t *a,uint8_t type,uint64_t epoch,Handler h){
 bool expected=false;
 if(!h||!remote_lease.compare_exchange_strong(expected,true))return false;
 if(begin_open(a,type,epoch,true,h))return true;
 remote_lease=false;return false;
}
bool TeslaNimbleCentral::scan_remote(uint64_t epoch,Handler h,bool normal){
 bool expected=false;
 if(!h||!epoch||!ready()||discovery.busy()||ble_gap_disc_active()||!remote_lease.compare_exchange_strong(expected,true))return false;
 expected=false;if(!busy.compare_exchange_strong(expected,true)){remote_lease=false;return false;}
 cookie.epoch=epoch;attempt_handler=h;scan_normal=normal;remote_scanning=true;scan_cancelling=false;
 pending=TeslaNimbleEvent::RemoteScanDone;
 ble_gap_disc_params p{};p.itvl=0x100;p.window=0x30;p.passive=1;p.filter_duplicates=1;
 int rc=ble_gap_disc(own_type,3000,&p,remote_scan_callback,&cookie);
 if(rc){remote_scanning=false;pending=0;busy=false;remote_lease=false;}
 return rc==0;
}
void TeslaNimbleCentral::finish_remote_scan(int status){
 pending=0;emit(TeslaNimbleEvent::RemoteScanDone,status);
 if(scan_cancelling.load()&&status!=BLE_HS_EALREADY)return;
 remote_scanning=false;busy.store(false,std::memory_order_release);
}
void TeslaNimbleCentral::remote_scan_barrier(ble_npl_event *event){
 auto *c=static_cast<TeslaNimbleCentral *>(ble_npl_event_get_arg(event));
 if(c&&c->remote_scanning.load())c->finish_remote_scan(BLE_HS_EALREADY);
}
bool TeslaNimbleCentral::cancel_remote_scan(){
 if(!remote_scanning.load())return !busy.load();
 if(scan_cancelling.exchange(true))return true;
 int rc=ble_gap_disc_cancel();
 if(rc&&rc!=BLE_HS_EALREADY){scan_cancelling=false;return false;}
 // Runs on the SAME host queue after callbacks already executing. Never
 // infer callback drainage from a successful SDK cancel alone.
 ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&scan_barrier);return true;
}
int TeslaNimbleCentral::remote_scan_callback(ble_gap_event *event,void *arg){
 if(!event||!arg)return 0;
 auto &c=*static_cast<Cookie *>(arg)->owner;
 if(!c.remote_scanning.load())return 0;
 if(c.scan_cancelling.load())return 0; // terminal comes from queued drain marker
 if(event->type==BLE_GAP_EVENT_DISC_COMPLETE){c.finish_remote_scan(event->disc_complete.reason);return 0;}
 if(event->type!=BLE_GAP_EVENT_DISC)return 0;
 const auto &d=event->disc;
 if(d.event_type!=BLE_HCI_ADV_RPT_EVTYPE_ADV_IND||d.addr.type>1||!d.data||d.length_data>31)return 0;
 bool found=false;
 for(size_t at=0;at<d.length_data;){
  size_t n=d.data[at++];if(!n)break;if(n>d.length_data-at)return 0;
  if((d.data[at]==6||d.data[at]==7)&&n>=17){
   const auto expected=remote_uuid(c.scan_normal.load()?0x14:0x11);
   for(size_t i=1;i+16<=n;i+=16)if(!memcmp(d.data+at+i,expected.value,16))found=true;
  }
  at+=n;
 }
 if(found){uint8_t peer[7];for(unsigned i=0;i<6;i++)peer[i]=d.addr.val[5-i];peer[6]=d.addr.type;c.emit(TeslaNimbleEvent::RemoteCandidate,0,0,peer,sizeof(peer));}
 return 0;
}
bool TeslaNimbleCentral::release_remote(){
 if(busy.load()||!remote_lease.load())return false;
 remote_lease=false;return true;
}
#endif
bool TeslaNimbleCentral::begin_open(const uint8_t *a,uint8_t type,uint64_t epoch,bool remote,Handler h){
 if(!ready()||!a||type>1||!epoch||discovery.busy()||ble_gap_disc_active())return false;
 bool expected=false;if(!busy.compare_exchange_strong(expected,true))return false;
 if(!remote&&remote_lease.load()){busy=false;return false;}
 remote_profile=remote;attempt_handler=h;
 // Publish the attempt's peer before pending=Open: readers that observe
 // pending==Open (retire_unannounced, possibly from the lifecycle task) see it.
 ble_addr_t peer{};peer.type=type;for(unsigned i=0;i<6;++i)peer.val[i]=a[5-i];open_peer=peer;
 cookie.epoch=epoch;connection=0xffff;cancelling_open=false;write_handle=notify_handle=cccd=0;
 unannounced=0xffff;open_cancel_accepted=false;unannounced_terminating=false;link_terminate_accepted=false;
 pending=TeslaNimbleEvent::Open;
 connect_flags=0;connect_submit_rc=-1;connect_cancel_rc=-1;connect_status=-1;
 connect_attempt.fetch_add(1);connect_flags.fetch_or(1); // entered SDK submit
 // Leave a bounded callback-delivery margin before the worker's 10s GATT
 // watchdog. Equal SDK/worker deadlines race a normal absent-peer timeout
 // into permanent ownership quarantine.
 const int rc=ble_gap_connect(own_type,&peer,7000,nullptr,gap,&cookie);
 connect_submit_rc=rc;connect_flags.fetch_or(2); // returned SDK submit
 if(rc){pending=0;busy=false;}return rc==0;
}
bool TeslaNimbleCentral::discover(){
 if(!ready()||connection==0xffff||pending.load())return false;
 service_start=service_end=write_handle=notify_handle=cccd=0;services=writes=notifies=descriptors=0;properties=0;pending=TeslaNimbleEvent::Discover;
 int rc=ble_gattc_disc_svc_by_uuid(connection,remote_profile?&remote_service_uuid.u:&service_uuid.u,service,&cookie);if(rc)pending=0;return rc==0;
}
void TeslaNimbleCentral::finish_discovery(int status){pending=0;emit(TeslaNimbleEvent::Discover,status);}
int TeslaNimbleCentral::service(uint16_t conn,const ble_gatt_error *e,const ble_gatt_svc *s,void *arg){
 auto &c=*static_cast<Cookie *>(arg)->owner;if(conn!=c.connection||c.pending!=TeslaNimbleEvent::Discover)return 0;
 if(!e){c.finish_discovery(BLE_HS_EUNKNOWN);return 0;}
 if(!e->status && s){++c.services;c.service_start=s->start_handle;c.service_end=s->end_handle;return 0;}
 if(e->status!=BLE_HS_EDONE||c.services!=1||!c.service_start||c.service_end<=c.service_start){c.finish_discovery(e->status?e->status:BLE_HS_EUNKNOWN);return 0;}
 c.descriptor_end=c.service_end;int rc=ble_gattc_disc_all_chrs(conn,c.service_start,c.service_end,characteristic,arg);if(rc)c.finish_discovery(rc);return 0;
}
int TeslaNimbleCentral::characteristic(uint16_t conn,const ble_gatt_error *e,const ble_gatt_chr *ch,void *arg){
 auto &c=*static_cast<Cookie *>(arg)->owner;if(conn!=c.connection||c.pending!=TeslaNimbleEvent::Discover)return 0;
 if(!e){c.finish_discovery(BLE_HS_EUNKNOWN);return 0;}
 if(!e->status&&ch){
  if(ble_uuid_cmp(&ch->uuid.u,c.remote_profile?&remote_write_uuid.u:&write_uuid.u)==0){++c.writes;c.write_handle=ch->val_handle;if(!(ch->properties&BLE_GATT_CHR_PROP_WRITE))c.writes+=2;}
  if(ble_uuid_cmp(&ch->uuid.u,c.remote_profile?&remote_notify_uuid.u:&notify_uuid.u)==0){++c.notifies;c.notify_handle=ch->val_handle;c.properties=ch->properties;}
  else if(c.notify_handle && ch->def_handle>c.notify_handle && ch->def_handle-1<c.descriptor_end)c.descriptor_end=ch->def_handle-1;
  return 0;
 }
 if(e->status!=BLE_HS_EDONE||c.writes!=1||c.notifies!=1||!(c.properties&(BLE_GATT_CHR_PROP_INDICATE|BLE_GATT_CHR_PROP_NOTIFY))||c.descriptor_end<=c.notify_handle){c.finish_discovery(e->status?e->status:BLE_HS_EUNKNOWN);return 0;}
 int rc=ble_gattc_disc_all_dscs(conn,c.notify_handle,c.descriptor_end,descriptor,arg);if(rc)c.finish_discovery(rc);return 0;
}
int TeslaNimbleCentral::descriptor(uint16_t conn,const ble_gatt_error *e,uint16_t chr,const ble_gatt_dsc *d,void *arg){
 auto &c=*static_cast<Cookie *>(arg)->owner;if(conn!=c.connection||c.pending!=TeslaNimbleEvent::Discover)return 0;
 if(!e){c.finish_discovery(BLE_HS_EUNKNOWN);return 0;}
 if(!e->status&&d){if(chr==c.notify_handle&&ble_uuid_u16(&d->uuid.u)==0x2902&&d->handle>chr&&d->handle<=c.descriptor_end){++c.descriptors;c.cccd=d->handle;}return 0;}
 c.finish_discovery(e->status==BLE_HS_EDONE&&c.descriptors==1?0:(e->status?e->status:BLE_HS_EUNKNOWN));return 0;
}
bool TeslaNimbleCentral::subscribe(){
 if(!ready()||connection==0xffff||pending.load()||!cccd)return false;
 uint8_t value[2]={static_cast<uint8_t>((properties&BLE_GATT_CHR_PROP_INDICATE)?2:1),0};pending=TeslaNimbleEvent::Descriptor;
 int rc=ble_gattc_write_flat(connection,cccd,value,2,written,&cookie);if(rc)pending=0;return rc==0;
}
bool TeslaNimbleCentral::write(const uint8_t *p,size_t n){
 if(!ready()||connection==0xffff||pending.load()||!write_handle||!p||!n||n>20)return false;
 pending=TeslaNimbleEvent::Write;int rc=ble_gattc_write_flat(connection,write_handle,p,n,written,&cookie);if(rc)pending=0;return rc==0;
}
int TeslaNimbleCentral::written(uint16_t conn,const ble_gatt_error *e,ble_gatt_attr *a,void *arg){
 auto &c=*static_cast<Cookie *>(arg)->owner;if(conn!=c.connection)return 0;
 int kind=c.pending.load();if(kind!=TeslaNimbleEvent::Write&&kind!=TeslaNimbleEvent::Descriptor)return 0;
 const uint16_t expected=kind==TeslaNimbleEvent::Write?c.write_handle:c.cccd;
 if(e&&e->status){c.pending=0;c.emit(static_cast<TeslaNimbleEvent::Kind>(kind),e->status,expected);return 0;}
 if(!a||a->handle!=expected)return 0;
 c.pending=0;c.emit(static_cast<TeslaNimbleEvent::Kind>(kind),e?e->status:BLE_HS_EUNKNOWN,expected);return 0;
}
bool TeslaNimbleCentral::close(){if(connection==0xffff)return false;return ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM)==0;}
int TeslaNimbleCentral::retire_unannounced(){
 // Pinned IDF ble_gap_rx_conn_complete() resets the master procedure at HCI
 // LE Connection Complete (ble_gap_conn_cancel then returns EALREADY) but,
 // with DEFER_CONN_EVENTS, posts CONNECT only after remote feature/version
 // exchange. Until then this OPEN owns a physical link we were never told
 // about. Only a master link to this attempt's exact peer is retired here.
 if(!busy.load()||connection.load()!=0xffff||pending.load()!=TeslaNimbleEvent::Open)return -1;
 ble_gap_conn_desc desc{};
 if(ble_gap_conn_find_by_addr(&open_peer,&desc)!=0||desc.role!=BLE_GAP_ROLE_MASTER||desc.conn_handle==0xffff)return -1;
 uint16_t expected=0xffff;
 if(!unannounced.compare_exchange_strong(expected,desc.conn_handle)&&expected!=desc.conn_handle)return -1;
 connect_flags.fetch_or(64);
 if(unannounced_terminating.load())return 0;
 const int rc=ble_gap_terminate(desc.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
 // EALREADY: the SDK already marked it TERMINATING; its DISCONNECT follows.
 if(rc==0||rc==BLE_HS_EALREADY){unannounced_terminating=true;connect_flags.fetch_or(128);return 0;}
 return rc;
}
int TeslaNimbleCentral::cancel_open(){
 // Set BEFORE submission: success can race cancellation. Never expose a late
 // link as usable, and never interpret an API return as terminal ownership.
 cancelling_open=true;
 if(!busy.load())return 0;
 const auto conn=connection.load();
 connect_flags.fetch_or(4);
 int rc,link=-1;
 if(conn!=0xffff){
  // Repeat calls are drain steps: once this path's terminate was accepted
  // only the SDK's DISCONNECT can finish ownership; never resubmit it.
  if(link_terminate_accepted.load())rc=BLE_HS_EALREADY;
  else{rc=ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);if(rc==0||rc==BLE_HS_EALREADY)link_terminate_accepted=true;}
 }
 else{
  // One LE Create Connection Cancel per attempt; repeating it while the SDK
  // still reports M_CONN would only produce controller Command Disallowed.
  rc=open_cancel_accepted.load()?BLE_HS_EALREADY:ble_gap_conn_cancel();
  // EALREADY: no M_CONN procedure remains for this attempt; never resubmit.
  if(rc==0||rc==BLE_HS_EALREADY)open_cancel_accepted=true;
  // Also after an accepted cancel: the controller can still complete the
  // link. Its terminate result (flags 64/128) supersedes EALREADY.
  link=retire_unannounced();
 }
 int expected=-1;connect_cancel_rc.compare_exchange_strong(expected,rc);connect_flags.fetch_or(8);
 return link!=-1?link:rc;
}
int TeslaNimbleCentral::gap(ble_gap_event *e,void *arg){
 if(!e||!arg)return 0;auto &c=*static_cast<Cookie *>(arg)->owner;
 switch(e->type){
 case BLE_GAP_EVENT_CONNECT:
  if(c.pending!=TeslaNimbleEvent::Open)return 0;
  c.connect_status=e->connect.status;c.connect_flags.fetch_or(16);
  if(!e->connect.status)c.connection=e->connect.conn_handle;
  c.pending=0;
  if(e->connect.status){const TeslaNimbleEvent ended{TeslaNimbleEvent::Open,c.cookie.epoch,0xffff,0,e->connect.status,nullptr,0};if(c.attempt_handler)c.attempt_handler(ended);c.busy.store(false,std::memory_order_release);return 0;}
  if(c.retiring.load()||c.cancelling_open.load()){c.close();return 0;}
  c.emit(TeslaNimbleEvent::Open,0);break;
 case BLE_GAP_EVENT_DISCONNECT:
  if(e->disconnect.conn.conn_handle!=c.connection){
   // Pinned IDF ble_gap_conn_broken() delivers DISCONNECT for a master link
   // even when CONNECT was never posted (e.g. 0x3E during feature exchange),
   // after deleting the connection and failing its GATT procedures. That is
   // the terminal of this pending OPEN: no CONNECT can follow for the handle.
   // Only this cookie's links reach here; a learned handle must also match.
   if(c.connection.load()!=0xffff||c.pending!=TeslaNimbleEvent::Open||!c.busy.load())return 0;
   const auto learned=c.unannounced.load();
   if(learned!=0xffff&&learned!=e->disconnect.conn.conn_handle)return 0;
   c.connect_flags.fetch_or(256);c.pending=0;
   const TeslaNimbleEvent ended{TeslaNimbleEvent::Open,c.cookie.epoch,0xffff,0,e->disconnect.reason?e->disconnect.reason:BLE_HS_EUNKNOWN,nullptr,0};
   if(c.attempt_handler)c.attempt_handler(ended);
   c.busy.store(false,std::memory_order_release);return 0;
  }
  c.connect_flags.fetch_or(32);
  // IDF ble_gap.c calls gattc_connection_broken before this callback. No prior
  // procedure owns cookie after this barrier; do not replace it earlier.
  c.pending=0;
  {const TeslaNimbleEvent ended{TeslaNimbleEvent::Disconnected,c.cookie.epoch,c.connection.load(),0,e->disconnect.reason,nullptr,0};
   c.connection=0xffff;bluetooth_memory::sample(bluetooth_memory::Stage::GattCallback,true);
   // SDK has already failed/freed old GATT procedures, but application callback
   // state is not drained until its final dispatch returns. Keep admission and
   // deinitialization closed throughout that dispatch (including reentry).
   if(c.attempt_handler)c.attempt_handler(ended);
   c.busy.store(false,std::memory_order_release);
  }return 0;
 case BLE_GAP_EVENT_NOTIFY_RX:
  if(e->notify_rx.conn_handle!=c.connection||e->notify_rx.attr_handle!=c.notify_handle||!e->notify_rx.om)break;
  {const size_t n=OS_MBUF_PKTLEN(e->notify_rx.om);if(!n||n>512)break;uint8_t copy[512];if(!os_mbuf_copydata(e->notify_rx.om,0,n,copy))c.emit(TeslaNimbleEvent::Rx,0,c.notify_handle,copy,n);}break;
 case BLE_GAP_EVENT_ENC_CHANGE:
  if(e->enc_change.conn_handle!=c.connection||!e->enc_change.status)break;
  c.emit(TeslaNimbleEvent::Security,e->enc_change.status);c.close();break;
 case BLE_GAP_EVENT_PASSKEY_ACTION:
  if(e->passkey.conn_handle!=c.connection)return 0;
  c.emit(TeslaNimbleEvent::Security,BLE_HS_EAUTHEN);c.close();break;
 case BLE_GAP_EVENT_REPEAT_PAIRING:
  if(e->repeat_pairing.conn_handle!=c.connection)return BLE_GAP_REPEAT_PAIRING_IGNORE;
  c.emit(TeslaNimbleEvent::Security,BLE_HS_EAUTHEN);c.close();return BLE_GAP_REPEAT_PAIRING_IGNORE;
 default:break;
 }return 0;
}
bool TeslaNimbleCentral::scan_tick(uint32_t now){
#if WARP_FEATURE_TESLA
 if(remote_lease.load())return true;
 using D=TeslaBleDiscovery;auto a=discovery.step(now);if(a==D::Action::Retire){failed=true;return false;}
 if(a==D::Action::Params){for(auto &ad:advertisements)ad={};next_advertisement=0;for(auto &ad:scannable)ad={};next_scannable=0;discovery.event(D::Event::Params,ready());}
 if(a==D::Action::Start){ble_gap_disc_params p{};p.itvl=0x50;p.window=0x30;p.passive=0;p.filter_duplicates=1;
  int rc=ble_gap_disc(own_type,D::SCAN_SECONDS*1000,&p,scan,this);discovery.submitted(a,rc==0);discovery.event(D::Event::Start,rc==0);}
 if(a==D::Action::Stop){int rc=ble_gap_disc_cancel();discovery.submitted(a,rc==0);discovery.event(D::Event::Stop,rc==0);}
#else
 (void)now;
#endif
 return true;
}
#if WARP_FEATURE_TESLA
int TeslaNimbleCentral::scan(ble_gap_event *e,void *arg){
 if(!e||!arg)return 0;auto &c=*static_cast<TeslaNimbleCentral *>(arg);using D=TeslaBleDiscovery;
 if(e->type==BLE_GAP_EVENT_DISC_COMPLETE){c.discovery.event(D::Event::Done,e->disc_complete.reason==0);return 0;}
 if(e->type!=BLE_GAP_EVENT_DISC)return 0;const auto &d=e->disc;if(d.addr.type>1||d.length_data>31||!d.data)return 0;
 uint8_t address[6];for(unsigned i=0;i<6;++i)address[i]=d.addr.val[5-i];
 if(d.event_type==BLE_HCI_ADV_RPT_EVTYPE_ADV_IND||d.event_type==BLE_HCI_ADV_RPT_EVTYPE_DIR_IND){
  Advertisement *slot=nullptr;for(auto &a:c.advertisements)if(a.type==d.addr.type&&!memcmp(a.address,address,6)){slot=&a;break;}
  if(!slot)slot=&c.advertisements[c.next_advertisement++%8];memcpy(slot->address,address,6);slot->type=d.addr.type;slot->size=d.length_data;memcpy(slot->data,d.data,d.length_data);
  c.discovery.observe(d.data,d.length_data,0,address,d.addr.type,d.rssi,0);
 }else if(d.event_type==BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND){
  // Not connectable: classified only (vehicle at its BLE connection limit), never adoptable.
  Advertisement *slot=nullptr;for(auto &a:c.scannable)if(a.type==d.addr.type&&!memcmp(a.address,address,6)){slot=&a;break;}
  if(!slot)slot=&c.scannable[c.next_scannable++%4];memcpy(slot->address,address,6);slot->type=d.addr.type;slot->size=d.length_data;memcpy(slot->data,d.data,d.length_data);
  c.discovery.observe(d.data,d.length_data,0,address,d.addr.type,d.rssi,BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND);
 }else if(d.event_type==BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND){
  c.discovery.observe(d.data,d.length_data,0,address,d.addr.type,d.rssi,BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND);
 }else if(d.event_type==BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP){
  const Advertisement *source=nullptr;unsigned event=0;
  for(auto &a:c.advertisements)if(a.type==d.addr.type&&!memcmp(a.address,address,6)){source=&a;break;}
  if(!source)for(auto &a:c.scannable)if(a.type==d.addr.type&&!memcmp(a.address,address,6)){source=&a;event=BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND;break;}
  if(source){uint8_t combined[62];memcpy(combined,source->data,source->size);memcpy(combined+source->size,d.data,d.length_data);c.discovery.observe(combined,source->size,d.length_data,address,d.addr.type,d.rssi,event);}
 }
 bluetooth_memory::sample(bluetooth_memory::Stage::GapCallback,true,1000);return 0;
}
#endif
#endif
