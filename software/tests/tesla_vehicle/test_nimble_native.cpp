#include "tesla_nimble_central.h"
#include <cassert>
#include <cstdio>
#include <vector>
std::vector<TeslaNimbleEvent> events;std::vector<uint8_t> received;std::function<void(const TeslaNimbleEvent&)> event_hook;
void receive(const TeslaNimbleEvent &e){events.push_back(e);if(e.kind==TeslaNimbleEvent::Rx)received.assign(e.data,e.data+e.size);if(event_hook)event_hook(e);}
ble_uuid_any_t uuid(uint8_t suffix){ble_uuid_any_t u{};u.u.type=128;const uint8_t a[16]={0x1e,0xb9,0xf8,0xeb,0x0c,0x96,0x88,0x9b,0xf0,0x43,0xd1,0xb2,suffix,2,0,0};memcpy(u.value,a,16);return u;}
void emit_gap(ble_gap_event e){test_gap(&e,test_gap_arg);}
void disconnect(){ble_gap_event e{};e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.conn_handle=4;emit_gap(e);}
void discover(TeslaNimbleCentral &c,uint8_t props=32,bool duplicate=false,bool wrong=false){
 assert(c.discover());ble_gatt_error ok{},done{BLE_HS_EDONE};ble_gatt_svc svc{1,40};test_service(4,&ok,&svc,test_proc_arg);if(duplicate)test_service(4,&ok,&svc,test_proc_arg);test_service(4,&done,nullptr,test_proc_arg);if(duplicate)return;
 ble_gatt_chr tx{9,10,8,uuid(0x12)},rx{19,20,props,uuid(0x13)},next{30,31,2,uuid(0x14)};
 test_char(4,&ok,&tx,test_proc_arg);test_char(4,&ok,&rx,test_proc_arg);test_char(4,&ok,&next,test_proc_arg);test_char(4,&done,nullptr,test_proc_arg);if(!props)return;
 ble_gatt_dsc d{};d.handle=21;d.uuid.u.type=16;d.uuid.value[0]=2;d.uuid.value[1]=0x29;
 test_descriptor(4,&ok,wrong?10:20,&d,test_proc_arg);test_descriptor(4,&done,20,nullptr,test_proc_arg);
}
int main(){
 TeslaNimbleCentral c;assert(c.initialize(receive));assert(!c.ready());ble_hs_cfg.sync_cb();assert(c.ready());assert(test_connects==0&&test_scans==0);
 assert(ble_hs_cfg.sm_sc==1&&ble_hs_cfg.sm_bonding==0);
 TeslaBleDiscovery::Scope scope{};scope.index=0;scope.mac=1;scope.generation=1;strcpy(scope.name,"S0123456789abcdefC");
 assert(c.discovery.request(100,scope));c.scan_tick(100);c.scan_tick(101);assert(test_scans==1&&!test_scan_params.passive);
 uint8_t ad[]={2,1,6};uint8_t rsp[20]={19,9};memcpy(rsp+2,scope.name,18);ble_gap_event scan{};scan.type=BLE_GAP_EVENT_DISC;scan.disc.addr.type=1;for(int i=0;i<6;++i)scan.disc.addr.val[i]=6-i;
 scan.disc.event_type=4;scan.disc.data=rsp;scan.disc.length_data=20;test_scan(&scan,test_scan_arg);TeslaBleDiscovery::Snapshot snapshot;c.discovery.snapshot(snapshot);assert(snapshot.count==0); // no connectable ADV proof
 scan.disc.event_type=0;scan.disc.data=ad;scan.disc.length_data=3;test_scan(&scan,test_scan_arg);scan.disc.event_type=4;scan.disc.data=rsp;scan.disc.length_data=20;test_scan(&scan,test_scan_arg);
 scan.type=BLE_GAP_EVENT_DISC_COMPLETE;test_scanning=false;test_scan(&scan,test_scan_arg);c.scan_tick(200);c.discovery.snapshot(snapshot);assert(snapshot.count==1&&snapshot.targets[0].address_type==1&&snapshot.targets[0].address[0]==1);
 uint8_t address[]={1,2,3,4,5,6};assert(c.discovery.matches(200,scope,snapshot.revision,address,1));assert(!c.discovery.matches(200,scope,snapshot.revision,address,0));
 for(unsigned epoch=1;epoch<=6;++epoch){
  assert(c.open(address,epoch%2,epoch));assert(!c.open(address,0,100));assert(test_peer.val[0]==6&&test_peer.val[5]==1);
  ble_gap_event opened{};opened.type=BLE_GAP_EVENT_CONNECT;opened.connect.conn_handle=4;emit_gap(opened);assert(events.back().epoch==epoch&&events.back().kind==TeslaNimbleEvent::Open);
  uint8_t props=epoch==2?16:epoch==3?48:epoch==4?0:32;discover(c,props,epoch==5,epoch==6);
  if(epoch>=4){assert(events.back().status!=0);assert(!c.subscribe());disconnect();continue;}
  assert(events.back().kind==TeslaNimbleEvent::Discover&&events.back().status==0);assert(c.subscribe());assert(test_handle==21&&test_write==std::vector<uint8_t>({uint8_t(epoch==2?1:2),0}));
  ble_gatt_error ok{};ble_gatt_attr attr{21};test_written(4,&ok,&attr,test_proc_arg);assert(events.back().kind==TeslaNimbleEvent::Descriptor);
  os_mbuf mb{{1,2,3}};ble_gap_event rx{};rx.type=BLE_GAP_EVENT_NOTIFY_RX;rx.notify_rx.conn_handle=4;rx.notify_rx.attr_handle=20;rx.notify_rx.om=&mb;rx.notify_rx.indication=true;emit_gap(rx);mb.bytes.clear();assert(received==std::vector<uint8_t>({1,2,3}));
  uint8_t tx[21]{};assert(!c.write(tx,21));assert(c.write(tx,20));attr.handle=10;test_written(4,&ok,&attr,test_proc_arg);assert(events.back().kind==TeslaNimbleEvent::Write);
  ble_gap_event sec{};sec.type=BLE_GAP_EVENT_ENC_CHANGE;sec.enc_change.conn_handle=4;sec.enc_change.status=23;emit_gap(sec);assert(events.back().kind==TeslaNimbleEvent::Security&&test_terminates>0);disconnect();assert(c.idle());
 }
 assert(c.open(address,0,7));ble_gap_event connected{};connected.type=BLE_GAP_EVENT_CONNECT;connected.connect.conn_handle=4;emit_gap(connected);
 event_hook=[&](const TeslaNimbleEvent&e){if(e.kind==TeslaNimbleEvent::Disconnected){assert(e.epoch==7);assert(!c.idle() && "terminal callback must return before idle/reuse/free");assert(!c.open(address,1,8));}};
 disconnect();event_hook={};assert(events.back().epoch==7 && c.idle());
 assert(c.open(address,1,8));emit_gap(connected);assert(events.back().epoch==8);disconnect();
 assert(c.open(address,0,9));
 event_hook=[&](const TeslaNimbleEvent&e){if(e.kind==TeslaNimbleEvent::Open&&e.status){assert(!c.idle() && "failed CONNECT terminal dispatch must finish before reuse/deinit");assert(!c.open(address,1,10));}};
 connected.connect.status=13;emit_gap(connected);event_hook={};assert(c.idle());
 test_stop_rc=42;assert(!c.shutdown());test_stop_rc=0;assert(c.shutdown());test_deinit_rc=42;assert(!c.deinitialize());test_deinit_rc=0;assert(c.deinitialize());assert(test_status==0);
 puts("NATIVE NimBLE PASS scan response binding/public-random/1link/A-B-A epochs/INDICATE+NOTIFY/descriptor bounds/SDK-mbuf copy/write20/security/disconnect/deinit");
}
