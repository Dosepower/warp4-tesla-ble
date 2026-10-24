#if defined(TESLA_BLE_USE_NIMBLE) && TESLA_BLE_USE_NIMBLE
#include "tesla_nimble_central.h"
#include "bluetooth_memory.h"
#include <cstring>
#ifndef TESLA_NIMBLE_HOST_TEST
#include <esp_bt.h>
#endif
std::atomic<TeslaNimbleCentral *> TeslaNimbleCentral::owner{nullptr};
namespace {
ble_uuid128_t uuid(uint8_t suffix){ble_uuid128_t u{};u.u.type=BLE_UUID_TYPE_128;const uint8_t a[16]={0x1e,0xb9,0xf8,0xeb,0x0c,0x96,0x88,0x9b,0xf0,0x43,0xd1,0xb2,suffix,0x02,0,0};memcpy(u.value,a,16);return u;}
const auto service_uuid=uuid(0x11),write_uuid=uuid(0x12),notify_uuid=uuid(0x13);
}
bool TeslaNimbleCentral::initialize(Handler h){
 TeslaNimbleCentral *expected=nullptr;if(!owner.compare_exchange_strong(expected,this))return false;
 handler=h;cookie.owner=this;
 if(esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_IDLE || nimble_port_init()!=0){failed=true;return false;}
 initialized=true;ble_hs_cfg.sync_cb=sync;ble_hs_cfg.reset_cb=reset;
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
 // ble_hs_stop counts established links, not an outstanding HCI connect
 // cancel. Keep the host servicing events until that terminal callback.
 const int rc=connection.load()==0xffff?ble_gap_conn_cancel():ble_gap_terminate(connection.load(),BLE_ERR_REM_USER_CONN_TERM);
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
 if(handler)handler({kind,cookie.epoch,connection.load(),handle,status,data,size});
 bluetooth_memory::sample(bluetooth_memory::Stage::GattCallback,true,1000);
}
bool TeslaNimbleCentral::open(const uint8_t *a,uint8_t type,uint64_t epoch){
 if(!ready()||!a||type>1||!epoch||discovery.busy()||ble_gap_disc_active())return false;
 bool expected=false;if(!busy.compare_exchange_strong(expected,true))return false;
 cookie.epoch=epoch;connection=0xffff;write_handle=notify_handle=cccd=0;pending=TeslaNimbleEvent::Open;
 ble_addr_t peer{};peer.type=type;for(unsigned i=0;i<6;++i)peer.val[i]=a[5-i];
 // Leave a bounded callback-delivery margin before the worker's 10s GATT
 // watchdog. Equal SDK/worker deadlines race a normal absent-peer timeout
 // into permanent ownership quarantine.
 const int rc=ble_gap_connect(own_type,&peer,7000,nullptr,gap,&cookie);
 if(rc){pending=0;busy=false;}return rc==0;
}
bool TeslaNimbleCentral::discover(){
 if(!ready()||connection==0xffff||pending.load())return false;
 service_start=service_end=write_handle=notify_handle=cccd=0;services=writes=notifies=descriptors=0;properties=0;pending=TeslaNimbleEvent::Discover;
 int rc=ble_gattc_disc_svc_by_uuid(connection,&service_uuid.u,service,&cookie);if(rc)pending=0;return rc==0;
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
  if(ble_uuid_cmp(&ch->uuid.u,&write_uuid.u)==0){++c.writes;c.write_handle=ch->val_handle;if(!(ch->properties&BLE_GATT_CHR_PROP_WRITE))c.writes+=2;}
  if(ble_uuid_cmp(&ch->uuid.u,&notify_uuid.u)==0){++c.notifies;c.notify_handle=ch->val_handle;c.properties=ch->properties;}
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
int TeslaNimbleCentral::gap(ble_gap_event *e,void *arg){
 if(!e||!arg)return 0;auto &c=*static_cast<Cookie *>(arg)->owner;
 switch(e->type){
 case BLE_GAP_EVENT_CONNECT:
  if(c.pending!=TeslaNimbleEvent::Open)return 0;
  if(!e->connect.status)c.connection=e->connect.conn_handle;
  c.pending=0;
  if(e->connect.status){const TeslaNimbleEvent ended{TeslaNimbleEvent::Open,c.cookie.epoch,0xffff,0,e->connect.status,nullptr,0};c.busy=false;if(c.handler)c.handler(ended);return 0;}
  if(c.retiring.load()){if(!c.close())c.failed=true;return 0;}
  c.emit(TeslaNimbleEvent::Open,0);break;
 case BLE_GAP_EVENT_DISCONNECT:
  if(e->disconnect.conn.conn_handle!=c.connection)return 0;
  // IDF ble_gap.c calls gattc_connection_broken before this callback. No prior
  // procedure owns cookie after this barrier; do not replace it earlier.
  c.pending=0;
  {const TeslaNimbleEvent ended{TeslaNimbleEvent::Disconnected,c.cookie.epoch,c.connection.load(),0,0,nullptr,0};
   c.connection=0xffff;c.busy=false;bluetooth_memory::sample(bluetooth_memory::Stage::GattCallback,true);
   // Last access: callback may admit a new epoch; do not touch retired cookie afterward.
   if(c.handler)c.handler(ended);
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
 using D=TeslaBleDiscovery;auto a=discovery.step(now);if(a==D::Action::Retire){failed=true;return false;}
 if(a==D::Action::Params){for(auto &ad:advertisements)ad={};next_advertisement=0;discovery.event(D::Event::Params,ready());}
 if(a==D::Action::Start){ble_gap_disc_params p{};p.itvl=0x50;p.window=0x30;p.passive=0;p.filter_duplicates=1;
  int rc=ble_gap_disc(own_type,D::SCAN_SECONDS*1000,&p,scan,this);discovery.submitted(a,rc==0);discovery.event(D::Event::Start,rc==0);}
 if(a==D::Action::Stop){int rc=ble_gap_disc_cancel();discovery.submitted(a,rc==0);discovery.event(D::Event::Stop,rc==0);}
 return true;
}
int TeslaNimbleCentral::scan(ble_gap_event *e,void *arg){
 if(!e||!arg)return 0;auto &c=*static_cast<TeslaNimbleCentral *>(arg);using D=TeslaBleDiscovery;
 if(e->type==BLE_GAP_EVENT_DISC_COMPLETE){c.discovery.event(D::Event::Done,e->disc_complete.reason==0);return 0;}
 if(e->type!=BLE_GAP_EVENT_DISC)return 0;const auto &d=e->disc;if(d.addr.type>1||d.length_data>31||!d.data)return 0;
 uint8_t address[6];for(unsigned i=0;i<6;++i)address[i]=d.addr.val[5-i];
 if(d.event_type==BLE_HCI_ADV_RPT_EVTYPE_ADV_IND||d.event_type==BLE_HCI_ADV_RPT_EVTYPE_DIR_IND){
  Advertisement *slot=nullptr;for(auto &a:c.advertisements)if(a.type==d.addr.type&&!memcmp(a.address,address,6)){slot=&a;break;}
  if(!slot)slot=&c.advertisements[c.next_advertisement++%8];memcpy(slot->address,address,6);slot->type=d.addr.type;slot->size=d.length_data;memcpy(slot->data,d.data,d.length_data);
  c.discovery.observe(d.data,d.length_data,0,address,d.addr.type,d.rssi,0);
 }else if(d.event_type==BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP){
  for(auto &a:c.advertisements)if(a.type==d.addr.type&&!memcmp(a.address,address,6)){
   uint8_t combined[62];memcpy(combined,a.data,a.size);memcpy(combined+a.size,d.data,d.length_data);c.discovery.observe(combined,a.size,d.length_data,address,d.addr.type,d.rssi,0);break;}
 }
 bluetooth_memory::sample(bluetooth_memory::Stage::GapCallback,true,1000);return 0;
}
#endif
