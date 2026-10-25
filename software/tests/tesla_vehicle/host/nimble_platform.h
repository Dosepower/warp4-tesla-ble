#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#ifdef TESLA_BLE_MODULE_HOST_TEST
#include "module_platform.h"
constexpr int configMAX_PRIORITIES=25;
#endif
constexpr int BLE_HS_EUNKNOWN=1,BLE_HS_EDONE=14,BLE_HS_EAUTHEN=23,BLE_HS_IO_NO_INPUT_OUTPUT=3,BLE_GAP_REPEAT_PAIRING_IGNORE=0;
constexpr int BLE_ERR_REM_USER_CONN_TERM=19;
constexpr int BLE_HS_EALREADY=2;
inline int ble_gap_conn_cancel();
#ifndef TESLA_BLE_MODULE_HOST_TEST
constexpr int ESP_BT_CONTROLLER_STATUS_IDLE=0;
#endif
constexpr int BLE_UUID_TYPE_128=128,BLE_UUID_TYPE_16=16;
constexpr int BLE_GATT_CHR_PROP_WRITE=8,BLE_GATT_CHR_PROP_NOTIFY=16,BLE_GATT_CHR_PROP_INDICATE=32;
constexpr int BLE_GAP_EVENT_CONNECT=1,BLE_GAP_EVENT_DISCONNECT=2,BLE_GAP_EVENT_NOTIFY_RX=3,BLE_GAP_EVENT_ENC_CHANGE=4,BLE_GAP_EVENT_PASSKEY_ACTION=5,BLE_GAP_EVENT_REPEAT_PAIRING=6,BLE_GAP_EVENT_DISC=7,BLE_GAP_EVENT_DISC_COMPLETE=8;
constexpr int BLE_HCI_ADV_RPT_EVTYPE_ADV_IND=0,BLE_HCI_ADV_RPT_EVTYPE_DIR_IND=1,BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP=4;
struct ble_uuid_t{uint8_t type;};struct ble_uuid128_t{ble_uuid_t u{};uint8_t value[16]{};};struct ble_uuid_any_t{ble_uuid_t u{};uint8_t value[16]{};};
inline int ble_uuid_cmp(const ble_uuid_t *a,const ble_uuid_t *b){return a->type==b->type?memcmp(reinterpret_cast<const ble_uuid128_t *>(a)->value,reinterpret_cast<const ble_uuid128_t *>(b)->value,a->type==128?16:2):1;}
inline uint16_t ble_uuid_u16(const ble_uuid_t *a){return a->type==16?reinterpret_cast<const ble_uuid128_t *>(a)->value[0]|reinterpret_cast<const ble_uuid128_t *>(a)->value[1]<<8:0;}
struct ble_addr_t{uint8_t type=0,val[6]{};};
struct os_mbuf{std::vector<uint8_t> bytes;};
#define OS_MBUF_PKTLEN(m) ((m)->bytes.size())
inline int os_mbuf_copydata(const os_mbuf *m,int at,size_t n,void *out){if(at+n>m->bytes.size())return 1;memcpy(out,m->bytes.data()+at,n);return 0;}
struct ble_gap_disc_params{uint16_t itvl=0,window=0;uint8_t passive=1,filter_duplicates=0;};
struct ble_gap_disc_desc{uint8_t event_type=0;ble_addr_t addr;int8_t rssi=-50;uint8_t length_data=0;const uint8_t *data=nullptr;};
struct ble_gap_event{int type=0;struct{int status=0;uint16_t conn_handle=0;}connect;struct{struct{uint16_t conn_handle=0;}conn;}disconnect;struct{uint16_t conn_handle=0,attr_handle=0;os_mbuf *om=nullptr;bool indication=false;}notify_rx;struct{int status=0;uint16_t conn_handle=0;}enc_change;struct{int reason=0;}disc_complete;ble_gap_disc_desc disc;struct{uint16_t conn_handle=0;}passkey,repeat_pairing;};
struct ble_gatt_error{int status=0;};struct ble_gatt_svc{uint16_t start_handle=0,end_handle=0;};
struct ble_gatt_chr{uint16_t def_handle=0,val_handle=0;uint8_t properties=0;ble_uuid_any_t uuid;};
struct ble_gatt_dsc{uint16_t handle=0;ble_uuid_any_t uuid;};struct ble_gatt_attr{uint16_t handle;};
struct BleConfig{void(*sync_cb)()=nullptr;void(*reset_cb)(int)=nullptr;int sm_io_cap=0,sm_bonding=0,sm_mitm=0,sm_sc=0;};inline BleConfig ble_hs_cfg;
using GapFn=int(*)(ble_gap_event *,void *);using SvcFn=int(*)(uint16_t,const ble_gatt_error *,const ble_gatt_svc *,void *);using ChrFn=int(*)(uint16_t,const ble_gatt_error *,const ble_gatt_chr *,void *);using DscFn=int(*)(uint16_t,const ble_gatt_error *,uint16_t,const ble_gatt_dsc *,void *);using WriteFn=int(*)(uint16_t,const ble_gatt_error *,ble_gatt_attr *,void *);
inline GapFn test_gap=nullptr,test_scan=nullptr;
inline std::function<void(int)> test_submit;inline void *test_gap_arg=nullptr,*test_scan_arg=nullptr,*test_proc_arg=nullptr;
inline SvcFn test_service=nullptr;inline ChrFn test_char=nullptr;inline DscFn test_descriptor=nullptr;inline WriteFn test_written=nullptr;
inline int test_connects=0,test_scans=0,test_terminates=0,test_stop_rc=0,test_deinit_rc=0,test_api_rc=0,test_status=0;
inline std::atomic<bool> test_scanning{false};inline ble_gap_disc_params test_scan_params;inline std::vector<uint8_t> test_write;inline uint16_t test_handle=0;inline ble_addr_t test_peer;
inline std::atomic<bool> test_run_host{false};inline std::function<void()> test_on_stop;
#ifndef TESLA_BLE_MODULE_HOST_TEST
inline int esp_bt_controller_get_status(){return test_status;}
#endif
inline int test_init_rc=0;
inline int nimble_port_init(){if(test_init_rc){test_status=1;
#ifdef TESLA_BLE_MODULE_HOST_TEST
controller_status=1;
#endif
return test_init_rc;}test_status=2;test_run_host=true;
#ifdef TESLA_BLE_MODULE_HOST_TEST
controller_status=2;
#endif
return 0;}
inline void nimble_port_run(){ble_hs_cfg.sync_cb();while(test_run_host.load())std::this_thread::yield();}
inline int nimble_port_stop(){if(test_stop_rc)return test_stop_rc;if(test_on_stop)test_on_stop();test_run_host=false;return 0;}
inline int nimble_port_deinit(){if(test_deinit_rc)return test_deinit_rc;test_status=0;
#ifdef TESLA_BLE_MODULE_HOST_TEST
controller_status=0;
#endif
return 0;}
inline int ble_hs_util_ensure_addr(int){return 0;}inline int ble_hs_id_infer_auto(int,uint8_t *p){*p=0;return 0;}
inline int ble_gap_disc_active(){return test_scanning;}
inline int ble_gap_disc_cancel(){test_scanning=false;return test_api_rc;}
inline int ble_gap_disc(uint8_t,int32_t,const ble_gap_disc_params*p,GapFn cb,void *arg){++test_scans;
#ifdef TESLA_BLE_MODULE_HOST_TEST
++scan_start_calls;
#endif
test_scan_params=*p;test_scan=cb;test_scan_arg=arg;test_scanning=true;return test_api_rc;}
inline int test_connect_timeout=0;
inline int ble_gap_connect(uint8_t,const ble_addr_t *p,int32_t timeout,const void *,GapFn f,void *a){test_connect_timeout=timeout;++test_connects;test_peer=*p;test_gap=f;test_gap_arg=a;if(!test_api_rc&&test_submit)test_submit(1);return test_api_rc;}
inline int ble_gap_conn_cancel(){if(!test_api_rc&&test_submit)test_submit(7);return test_api_rc;}
inline int ble_gap_terminate(uint16_t,uint8_t){++test_terminates;if(!test_api_rc&&test_submit)test_submit(6);return test_api_rc;}
inline int ble_gattc_disc_svc_by_uuid(uint16_t,const ble_uuid_t *,SvcFn f,void *a){test_service=f;test_proc_arg=a;if(!test_api_rc&&test_submit)test_submit(2);return test_api_rc;}
inline int ble_gattc_disc_all_chrs(uint16_t,uint16_t,uint16_t,ChrFn f,void *a){test_char=f;test_proc_arg=a;if(!test_api_rc&&test_submit)test_submit(3);return test_api_rc;}
inline int ble_gattc_disc_all_dscs(uint16_t,uint16_t s,uint16_t e,DscFn f,void *a){assert(e>=s);test_descriptor=f;test_proc_arg=a;if(!test_api_rc&&test_submit)test_submit(4);return test_api_rc;}
inline int ble_gattc_write_flat(uint16_t,uint16_t h,const void*p,uint16_t n,WriteFn f,void*a){test_handle=h;test_write.assign((const uint8_t*)p,(const uint8_t*)p+n);test_written=f;test_proc_arg=a;if(!test_api_rc&&test_submit)test_submit(5);return test_api_rc;}
