#pragma once
#include "module_platform.h"
#include "nimble_platform.h"
#include "../../../src/modules/tesla_vehicle/bluetooth_lifecycle.h"
#include "../../../src/modules/tesla_vehicle/tesla_ble_identifier.h"
class HostNimbleDriver {
 BluetoothLifecycle &owner;std::thread lifecycle,host;
public:
 HostNimbleDriver(BluetoothLifecycle&o,BluetoothLifecycle::Handler h):owner(o){controller_status=test_status=0;async_bluetooth=true;host_task_created=[this](TaskHandle_t t){if(t->name=="nimble_host"){assert(host_created_priority==configMAX_PRIORITIES-4);host=std::thread([t]{run_task(t);});}};assert(owner.start(h));auto*t=tasks.back().get();lifecycle=std::thread([t]{run_task(t);});auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!owner.active()&&!owner.failed()&&std::chrono::steady_clock::now()<end)std::this_thread::yield();assert(owner.active());auto*c=owner.central();test_on_stop=[c]{if(c&&!c->idle()){ble_gap_event e{};e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.conn_handle=4;test_gap(&e,test_gap_arg);}};}
 ~HostNimbleDriver(){owner.request_stop();lifecycle.join();if(host.joinable())host.join();owner.supervise();test_on_stop={};host_task_created={};async_bluetooth=false;assert(owner.stopped());}
};
inline uint32_t nimble_vin_scan(BluetoothLifecycle &bt,const char*vin,int index,uint64_t mac,uint8_t first=1,uint8_t type=0){
 TeslaBleDiscovery::Scope scope{};scope.index=index;scope.mac=mac;scope.generation=ev.generation;assert(tesla_ble_vehicle_name(vin,scope.name));assert(bt.request_discovery(scope));
 TeslaBleDiscovery::Snapshot s{};auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!test_scanning&&std::chrono::steady_clock::now()<end)std::this_thread::yield();assert(test_scanning);const auto yielded=host_bt_yields.load(std::memory_order_acquire);while(host_bt_yields.load(std::memory_order_acquire)==yielded&&std::chrono::steady_clock::now()<end)std::this_thread::yield();assert(host_bt_yields.load(std::memory_order_acquire)!=yielded);
 uint8_t data[20]={19,9};memcpy(data+2,scope.name,18);ble_gap_event e{};e.type=BLE_GAP_EVENT_DISC;e.disc.addr.val[5]=first;for(int i=0;i<5;++i)e.disc.addr.val[i]=6-i;e.disc.addr.type=type;e.disc.data=data;e.disc.length_data=20;e.disc.event_type=0;test_scan(&e,test_scan_arg);e.type=BLE_GAP_EVENT_DISC_COMPLETE;test_scanning=false;test_scan(&e,test_scan_arg);
 while(std::chrono::steady_clock::now()<end){if(bt.discovery_snapshot(s)&&s.status==TeslaBleDiscovery::Status::Complete)break;std::this_thread::yield();}assert(s.count==1&&s.status==TeslaBleDiscovery::Status::Complete);return s.revision;
}
inline std::function<void()> nimble_before_disconnect;
inline void nimble_force_disconnect(){if(nimble_before_disconnect)nimble_before_disconnect();ble_gap_event e{};e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.conn_handle=4;test_gap(&e,test_gap_arg);}
inline ble_uuid_any_t fixture_uuid(uint8_t suffix){ble_uuid_any_t u{};u.u.type=128;const uint8_t b[16]={0x1e,0xb9,0xf8,0xeb,0x0c,0x96,0x88,0x9b,0xf0,0x43,0xd1,0xb2,suffix,2,0,0};memcpy(u.value,b,16);return u;}
inline void nimble_complete_setup(){
 ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect.conn_handle=4;test_gap(&e,test_gap_arg);
 auto*c=static_cast<void*>(test_gap_arg);(void)c;
}
inline void nimble_discover_all(){
 ble_gatt_error ok{},done{BLE_HS_EDONE};ble_gatt_svc svc{1,40};test_service(4,&ok,&svc,test_proc_arg);test_service(4,&done,nullptr,test_proc_arg);
 ble_gatt_chr tx{9,10,8,fixture_uuid(0x12)},rx{19,20,32,fixture_uuid(0x13)};test_char(4,&ok,&tx,test_proc_arg);test_char(4,&ok,&rx,test_proc_arg);test_char(4,&done,nullptr,test_proc_arg);ble_gatt_dsc d{};d.handle=21;d.uuid.u.type=16;d.uuid.value[0]=2;d.uuid.value[1]=0x29;test_descriptor(4,&ok,20,&d,test_proc_arg);test_descriptor(4,&done,20,nullptr,test_proc_arg);
}
inline void nimble_auto_submit(int stage){
 ble_gatt_error ok{},done{BLE_HS_EDONE};
 if(stage==1){++peer_open_calls;peer_open_type=test_peer.type;nimble_complete_setup();}
 if(stage==2){ble_gatt_svc svc{1,40};test_service(4,&ok,&svc,test_proc_arg);test_service(4,&done,nullptr,test_proc_arg);}
 if(stage==3){ble_gatt_chr tx{9,10,8,fixture_uuid(0x12)},rx{19,20,uint8_t(host_rx_properties),fixture_uuid(0x13)};test_char(4,&ok,&tx,test_proc_arg);test_char(4,&ok,&rx,test_proc_arg);test_char(4,&done,nullptr,test_proc_arg);}
 if(stage==4){ble_gatt_dsc d{};d.handle=21;d.uuid.u.type=16;d.uuid.value[0]=2;d.uuid.value[1]=0x29;test_descriptor(4,&ok,20,&d,test_proc_arg);test_descriptor(4,&done,20,nullptr,test_proc_arg);}
 if(stage==5){if(test_handle==21){assert(test_write.size()==2);host_cccd_value=test_write[0];}else if(on_gatt_bytes)on_gatt_bytes(test_write.data(),test_write.size());ble_gatt_attr a{test_handle};test_written(4,&ok,&a,test_proc_arg);}
 if(stage==6)nimble_force_disconnect();
}
