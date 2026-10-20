#pragma once
#include "bluetooth_driver.h"
#include "../../../src/modules/tesla_vehicle/tesla_ble_identifier.h"
// Explicit synthetic scan through the production lifecycle and SDK boundary.
inline uint32_t host_vin_scan(BluetoothLifecycle &bt,const char *vin,int index,uint64_t mac,uint8_t first=1,uint8_t type=0) {
 TeslaBleDiscovery::Scope scope;scope.index=index;scope.mac=mac;assert(tesla_ble_vehicle_name(vin,scope.name));
 assert(bt.request_discovery(scope));TeslaBleDiscovery::Snapshot s;
 const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
 while(std::chrono::steady_clock::now()<end) {
  if(host_gap_handler) {
   esp_ble_gap_cb_param_t p;p.scan_rst.bda[0]=first;p.scan_rst.ble_addr_type=type;
   p.scan_rst.adv_data_len=20;p.scan_rst.ble_adv[0]=19;p.scan_rst.ble_adv[1]=9;memcpy(p.scan_rst.ble_adv+2,scope.name,18);
   host_gap_handler(ESP_GAP_BLE_SCAN_RESULT_EVT,&p);
  }
  if(bt.discovery_snapshot(s)&&s.count)break;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 assert(s.count==1);host_emit_done();
 while(std::chrono::steady_clock::now()<end) {
  host_emit_done();
  if(bt.discovery_snapshot(s)&&s.status==TeslaBleDiscovery::Status::Complete)break;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 assert(s.status==TeslaBleDiscovery::Status::Complete);return s.revision;
}
