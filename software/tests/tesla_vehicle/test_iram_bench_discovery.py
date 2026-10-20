#!/usr/bin/env python3
"""Real bench engine/binding with injected SDK events; not hardware evidence."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_iram_bench_smoke import SDK, MODULE
from test_iram_bench_smoke_api import PLATFORM

EXTRA = r'''
#include <string>
constexpr int ESP_ERR_NOT_FOUND=0x105, ESP_UUID_LEN_128=16, ESP_UUID_LEN_16=2;
constexpr int BLE_SCAN_TYPE_PASSIVE=0, BLE_SCAN_TYPE_ACTIVE=1, BLE_ADDR_TYPE_PUBLIC=0, BLE_SCAN_FILTER_ALLOW_ALL=0, BLE_SCAN_DUPLICATE_ENABLE=1;
constexpr int ESP_BLE_EVT_CONN_ADV=0, ESP_BLE_EVT_CONN_DIR_ADV=1, ESP_BLE_EVT_DISC_ADV=2, ESP_BLE_EVT_NON_CONN_ADV=3, ESP_BLE_EVT_SCAN_RSP=4;
constexpr int ESP_GATT_CHAR_PROP_BIT_WRITE=8, ESP_GATT_CHAR_PROP_BIT_NOTIFY=16;
constexpr int ESP_GATT_WRITE_TYPE_RSP=1, ESP_GATT_AUTH_REQ_NONE=0;
constexpr unsigned portTICK_PERIOD_MS=1;
constexpr int ESP_BT_CONTROLLER_STATUS_ENABLED=2;
using esp_ble_addr_type_t=int;
using esp_ble_evt_type_t=int;
struct esp_ble_gattc_cancel_open_params_t { esp_gatt_if_t gattc_if; uint8_t remote_bda[6]; };
struct esp_bt_uuid_t { int len; union { uint8_t uuid128[16]; uint16_t uuid16; } uuid; };
struct esp_ble_scan_params_t { int scan_type,own_addr_type,scan_filter_policy; uint16_t scan_interval,scan_window; int scan_duplicate; };
struct esp_gattc_service_elem_t { uint16_t start_handle,end_handle; };
struct esp_gattc_char_elem_t { uint16_t char_handle; int properties; };
struct esp_gattc_descr_elem_t { uint16_t handle; };
enum esp_gap_ble_cb_event_t { ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT,ESP_GAP_BLE_SCAN_START_COMPLETE_EVT,ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT,ESP_GAP_BLE_SCAN_RESULT_EVT,ESP_GAP_BLE_SEC_REQ_EVT };
constexpr int ESP_GAP_SEARCH_INQ_RES_EVT=0, ESP_GAP_SEARCH_INQ_CMPL_EVT=1;
struct esp_ble_gap_cb_param_t {
 struct { int status; } scan_param_cmpl,scan_start_cmpl,scan_stop_cmpl;
 struct { int search_evt; uint8_t bda[6]; int ble_addr_type,rssi,ble_evt_type; uint8_t ble_adv[62]; uint8_t adv_data_len,scan_rsp_len; } scan_rst;
 struct { struct { uint8_t bd_addr[6]; } ble_req; } ble_security;
};
using esp_gap_ble_cb_t=void (*)(esp_gap_ble_cb_event_t,esp_ble_gap_cb_param_t *);
extern "C" {
int esp_ble_gap_register_callback(esp_gap_ble_cb_t);
int esp_ble_gap_set_scan_params(esp_ble_scan_params_t *); int esp_ble_gap_start_scanning(uint32_t); int esp_ble_gap_stop_scanning();
int esp_ble_gap_security_rsp(uint8_t *,bool);
int esp_ble_get_bond_device_num();
int esp_ble_gattc_register_for_notify(esp_gatt_if_t,uint8_t *,uint16_t);
int esp_ble_gattc_write_char_descr(esp_gatt_if_t,uint16_t,uint16_t,uint16_t,uint8_t *,int,int);
int esp_ble_gattc_write_char(esp_gatt_if_t,uint16_t,uint16_t,uint16_t,uint8_t *,int,int);
int esp_ble_gattc_open(esp_gatt_if_t,uint8_t *,esp_ble_addr_type_t,bool);
int esp_ble_gattc_close(esp_gatt_if_t,uint16_t); int esp_ble_gattc_cancel_open(const esp_ble_gattc_cancel_open_params_t *);
int esp_ble_gattc_search_service(esp_gatt_if_t,uint16_t,esp_bt_uuid_t *);
int esp_ble_gattc_get_service(esp_gatt_if_t,uint16_t,esp_bt_uuid_t *,esp_gattc_service_elem_t *,uint16_t *,uint16_t);
int esp_ble_gattc_get_char_by_uuid(esp_gatt_if_t,uint16_t,uint16_t,uint16_t,esp_bt_uuid_t,esp_gattc_char_elem_t *,uint16_t *);
int esp_ble_gattc_get_descr_by_char_handle(esp_gatt_if_t,uint16_t,uint16_t,esp_bt_uuid_t,esp_gattc_descr_elem_t *,uint16_t *);
}
constexpr int MALLOC_CAP_EXEC=1,MALLOC_CAP_INTERNAL=2,MALLOC_CAP_8BIT=4,MALLOC_CAP_SPIRAM=8;
struct multi_heap_info_t { size_t total_free_bytes,largest_free_block,minimum_free_bytes; };
inline bool resource_bad=false;
inline std::string resource_metric;
inline void heap_caps_get_info(multi_heap_info_t *i,int caps) { *i={32768,16384,32768}; if(resource_bad&&caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)){if(resource_metric=="free")i->total_free_bytes=8191;if(resource_metric=="largest")i->largest_free_block=4095;if(resource_metric=="minimum")i->minimum_free_bytes=8191;if(resource_metric=="boundary")*i={8192,4096,8192};} }
inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return resource_bad?(resource_metric=="stack"?2047:(resource_metric=="boundary"?2048:4096)):4096; }
inline void esp_rom_printf(const char *,...) {}
'''
# Match the pinned IDF event union used by the implementation.
DSDK = SDK.replace('ESP_GATTC_REG_EVT, ESP_GATTC_UNREG_EVT','ESP_GATTC_REG_EVT, ESP_GATTC_UNREG_EVT, ESP_GATTC_OPEN_EVT, ESP_GATTC_CLOSE_EVT, ESP_GATTC_DISCONNECT_EVT, ESP_GATTC_DIS_SRVC_CMPL_EVT, ESP_GATTC_SEARCH_CMPL_EVT, ESP_GATTC_CANCEL_OPEN_EVT').replace('} reg; };','} reg; struct { int status; uint16_t conn_id; uint8_t remote_bda[6]; } open,close; struct { uint16_t conn_id; uint8_t remote_bda[6]; int reason; } disconnect; struct { int status; uint16_t conn_id; } dis_srvc_cmpl,search_cmpl; struct { int status; uint8_t remote_bda[6]; } cancel_open; };') + EXTRA
DSDK=DSDK.replace('ESP_GATTC_CANCEL_OPEN_EVT }','ESP_GATTC_CANCEL_OPEN_EVT, ESP_GATTC_REG_FOR_NOTIFY_EVT, ESP_GATTC_WRITE_DESCR_EVT, ESP_GATTC_WRITE_CHAR_EVT, ESP_GATTC_NOTIFY_EVT }').replace('} cancel_open; };','} cancel_open; struct { int status; uint16_t handle; } reg_for_notify; struct { int status; uint16_t conn_id,handle; } write; struct { uint16_t conn_id,handle,value_len; uint8_t remote_bda[6]; uint8_t *value; bool is_notify; } notify; };')
DSDK+='\ninline void esp_fill_random(void *p,size_t n) { static unsigned char sequence=1; auto *bytes=static_cast<unsigned char *>(p); while(n--) *bytes++=++sequence; }\n'
HARNESS = r'''
#include "sdk.h"
#include "tesla_ble_bench_discovery.h"
#include "tesla_ble_bench_smoke.h"
#include "tesla_ble_uuid.h"
#include <cassert>
#include <string>
#include <vector>
#include <cstring>
static std::vector<std::string> calls;
static TaskFunction_t worker; static void *context;
static esp_gattc_cb_t gatt; static esp_gap_ble_cb_t gap;
static uint32_t ticks, scan_started; static const char *mode="success";
static std::string fail; static bool completion=false;
static uint8_t addr[6]={1,2,3,4,5,6};
static std::vector<uint8_t> sent_wire;
static int controller_status=0;
static int call(const char *name) { calls.emplace_back(name);
 if ((!strcmp(mode,"blocked") && !strcmp(name,"controller.init")) || (!strcmp(mode,"slow_cleanup") && !strcmp(name,"controller.deinit"))) { ticks+=6000; tesla_ble_bench_discovery.supervise(); assert(tesla_ble_bench_discovery.snapshot().worker_active); }
 return fail==name ? -77 : 0; }
extern "C" {
int esp_bt_controller_get_status() { return !strcmp(mode,"owned") ? 1:controller_status; }
int esp_bluedroid_get_status() { return 0; }
int esp_bt_controller_init(esp_bt_controller_config_t *c) { assert(c->marker==12345); return call("controller.init"); }
int esp_bt_controller_enable(esp_bt_mode_t) { int rc=call("controller.enable"); if(!rc) controller_status=2; return rc; }
int esp_bluedroid_init() { return call("host.init"); }
int esp_bluedroid_enable() { return call("host.enable"); }
int esp_bluedroid_disable() { return call("host.disable"); }
int esp_bluedroid_deinit() { return call("host.deinit"); }
int esp_bt_controller_disable() { return call("controller.disable"); }
int esp_bt_controller_deinit() { int rc=call("controller.deinit"); if(!rc) controller_status=0; return rc; }
int esp_ble_gap_register_callback(esp_gap_ble_cb_t cb) { gap=cb; return call("gap.callback_register"); }
int esp_ble_gattc_register_callback(esp_gattc_cb_t cb) { gatt=cb; return call("gatt.callback_register"); }
int esp_ble_gattc_app_register(uint16_t id) { int rc=call("gatt.app_register"); if(!rc) { esp_ble_gattc_cb_param_t p{}; p.reg={0,id}; gatt(ESP_GATTC_REG_EVT,7,&p); } return rc; }
int esp_ble_gattc_app_unregister(esp_gatt_if_t) { int rc=call("gatt.app_unregister"); if(!rc && strcmp(mode,"unreg_timeout")) gatt(ESP_GATTC_UNREG_EVT,7,nullptr); return rc; }
int esp_ble_gap_set_scan_params(esp_ble_scan_params_t *p) { assert(p->scan_type==BLE_SCAN_TYPE_ACTIVE && p->scan_interval==0x10 && p->scan_window==0x10); int rc=call("scan.params"); if(!rc) { esp_ble_gap_cb_param_t p{}; p.scan_param_cmpl.status=!strcmp(mode,"params_event_fail")?133:0; gap(ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT,&p); } return rc; }
int esp_ble_gap_start_scanning(uint32_t seconds) { assert(seconds==30); int rc=call("scan.start"); if(!rc) { scan_started=ticks; esp_ble_gap_cb_param_t p{}; p.scan_start_cmpl.status=0; gap(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT,&p); completion=true; } return rc; }
int esp_ble_gap_stop_scanning() { int rc=call("scan.stop"); if(!rc) { esp_ble_gap_cb_param_t p{}; p.scan_stop_cmpl.status=0; gap(ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT,&p); } return rc; }
int esp_ble_gap_security_rsp(uint8_t *,bool allow) { assert(!allow); return call("security.reject"); }
int esp_ble_get_bond_device_num() { call("security.bond_check"); return !strcmp(mode,"bonded")?1:(!strcmp(mode,"bond_query_fail")?-1:0); }
int esp_ble_gattc_register_for_notify(esp_gatt_if_t,uint8_t *,uint16_t h) { int rc=call("gatt.subscribe"); if(!rc) { esp_ble_gattc_cb_param_t p{}; p.reg_for_notify={0,h}; gatt(ESP_GATTC_REG_FOR_NOTIFY_EVT,7,&p); } return rc; }
int esp_ble_gattc_write_char_descr(esp_gatt_if_t,uint16_t id,uint16_t h,uint16_t n,uint8_t *b,int type,int auth) { assert(n==2&&b[0]==1&&b[1]==0&&type==ESP_GATT_WRITE_TYPE_RSP&&auth==ESP_GATT_AUTH_REQ_NONE); int rc=call("gatt.cccd_enable"); if(!rc) { esp_ble_gattc_cb_param_t p{}; p.write={0,id,h}; gatt(ESP_GATTC_WRITE_DESCR_EVT,7,&p); } return rc; }
int esp_ble_gattc_write_char(esp_gatt_if_t,uint16_t id,uint16_t h,uint16_t n,uint8_t *b,int type,int auth) { assert(n<=20&&type==ESP_GATT_WRITE_TYPE_RSP&&auth==ESP_GATT_AUTH_REQ_NONE); int rc=call("gatt.payload_write"); if(!rc) { sent_wire.insert(sent_wire.end(),b,b+n); esp_ble_gattc_cb_param_t p{}; p.write={0,id,h}; gatt(ESP_GATTC_WRITE_CHAR_EVT,7,&p); } return rc; }
int esp_ble_gattc_open(esp_gatt_if_t,uint8_t *a,esp_ble_addr_type_t t,bool direct) {
 assert(!memcmp(a,addr,6) && t==1 && direct); int rc=call("gatt.open");
 if(!rc && strcmp(mode,"open_timeout") && strcmp(mode,"late_open")) { esp_ble_gattc_cb_param_t p{}; p.open.status=!strcmp(mode,"open_event_fail")?133:0; p.open.conn_id=42; memcpy(p.open.remote_bda,a,6);
 gatt(ESP_GATTC_OPEN_EVT,8,&p); p.open.remote_bda[0]^=1; gatt(ESP_GATTC_OPEN_EVT,7,&p); p.open.remote_bda[0]^=1; gatt(ESP_GATTC_OPEN_EVT,7,&p);
 if(!p.open.status) { p.dis_srvc_cmpl={0,42}; gatt(ESP_GATTC_DIS_SRVC_CMPL_EVT,7,&p); } }
 if(!rc && !strcmp(mode,"security")) { esp_ble_gap_cb_param_t p{}; memcpy(p.ble_security.ble_req.bd_addr,a,6); gap(ESP_GAP_BLE_SEC_REQ_EVT,&p); }
 return rc;
}
int esp_ble_gattc_cancel_open(const esp_ble_gattc_cancel_open_params_t *params) { assert(params->gattc_if==7); int rc=call("gatt.cancel_open"); if(!rc) { esp_ble_gattc_cb_param_t p{}; memcpy(p.cancel_open.remote_bda,params->remote_bda,6); p.cancel_open.status=0; gatt(ESP_GATTC_CANCEL_OPEN_EVT,7,&p);
 if(!strcmp(mode,"late_open")) { p.open.status=0; p.open.conn_id=42; memcpy(p.open.remote_bda,params->remote_bda,6); gatt(ESP_GATTC_OPEN_EVT,7,&p); } } return rc; }
int esp_ble_gattc_close(esp_gatt_if_t,uint16_t id) { assert(id==42); int rc=call("gatt.close"); if(!rc && strcmp(mode,"close_timeout")) { esp_ble_gattc_cb_param_t p{}; p.close.conn_id=id; p.close.status=0; memcpy(p.close.remote_bda,addr,6); gatt(ESP_GATTC_CLOSE_EVT,7,&p); } return rc; }
int esp_ble_gattc_search_service(esp_gatt_if_t,uint16_t id,esp_bt_uuid_t *u) { assert(id==42 && u->uuid.uuid128[12]==0x11); int rc=call("gatt.search"); if(!rc && strcmp(mode,"search_timeout")) { esp_ble_gattc_cb_param_t p{}; p.search_cmpl={0,42}; gatt(ESP_GATTC_SEARCH_CMPL_EVT,7,&p); } return rc; }
int esp_ble_gattc_get_service(esp_gatt_if_t,uint16_t,esp_bt_uuid_t *u,esp_gattc_service_elem_t *s,uint16_t *n,uint16_t) { auto expected=tesla_ble_uuid(0x11); assert(u->len==expected.len && !memcmp(u->uuid.uuid128,expected.uuid.uuid128,16)); int rc=call("gatt.service"); *s={10,20}; *n=!strcmp(mode,"no_service")?0:1;
 if(!strcmp(mode,"security_at_cache")) { esp_ble_gap_cb_param_t p{}; memcpy(p.ble_security.ble_req.bd_addr,addr,6); gap(ESP_GAP_BLE_SEC_REQ_EVT,&p); } return rc; }
int esp_ble_gattc_get_char_by_uuid(esp_gatt_if_t,uint16_t,uint16_t start,uint16_t end,esp_bt_uuid_t u,esp_gattc_char_elem_t *c,uint16_t *n) { const bool tx=u.uuid.uuid128[12]==0x12; auto expected=tesla_ble_uuid(tx?0x12:0x13); assert(start==10 && end==20 && u.len==expected.len && !memcmp(u.uuid.uuid128,expected.uuid.uuid128,16)); int rc=call(tx?"gatt.write_metadata":"gatt.notify_metadata"); *n=(!strcmp(mode,"no_write")&&tx)||(!strcmp(mode,"no_notify")&&!tx)?0:1; *c={uint16_t(u.uuid.uuid128[12]),!strcmp(mode,"wrong_properties")||(!strcmp(mode,"wrong_notify_properties")&&!tx)?0:(tx?8:16)}; return rc; }
int esp_ble_gattc_get_descr_by_char_handle(esp_gatt_if_t,uint16_t,uint16_t handle,esp_bt_uuid_t u,esp_gattc_descr_elem_t *d,uint16_t *n) { assert(handle==19 && u.len==ESP_UUID_LEN_16 && u.uuid.uuid16==0x2902); int rc=call("gatt.cccd_metadata"); *n=!strcmp(mode,"no_cccd")?0:1; d->handle=21; return rc; }
}
TickType_t xTaskGetTickCount() { return ticks; }
int xTaskCreatePinnedToCore(TaskFunction_t fn,const char *,uint32_t stack,void *arg,int priority,TaskHandle_t *h,int core) { assert((stack==4096 || stack==8192) && priority==1 && core==0); worker=fn; context=arg; *h=(void *)1; return !strcmp(mode,"task_fail")?0:pdPASS; }
void vTaskDelete(TaskHandle_t) {}
void advertisement(int index=0) {
 esp_ble_gap_cb_param_t p{}; p.scan_rst.search_evt=ESP_GAP_SEARCH_INQ_RES_EVT; memcpy(p.scan_rst.bda,addr,6); p.scan_rst.bda[5]+=index;
 p.scan_rst.ble_addr_type=1; p.scan_rst.rssi=-45; p.scan_rst.ble_evt_type=ESP_BLE_EVT_CONN_ADV;
 // Pinned Bluedroid combines ADV flags + SCAN_RSP local name, retaining the
 // original advertisement event type. No advertised VCSEC UUID is necessary.
 p.scan_rst.adv_data_len=3; p.scan_rst.ble_adv[0]=2; p.scan_rst.ble_adv[1]=1; p.scan_rst.ble_adv[2]=6;
 p.scan_rst.scan_rsp_len=20; p.scan_rst.ble_adv[3]=19; p.scan_rst.ble_adv[4]=9;
 memcpy(p.scan_rst.ble_adv+5,"S0123456789abcdefC",18);
 if(!strcmp(mode,"malformed")) p.scan_rst.ble_adv[3]=21;
 if(!strcmp(mode,"non_tesla")) p.scan_rst.ble_adv[5]='X';
 if(!strcmp(mode,"non_connectable")) p.scan_rst.ble_evt_type=ESP_BLE_EVT_DISC_ADV;
 gap(ESP_GAP_BLE_SCAN_RESULT_EVT,&p);
}
void vTaskDelay(TickType_t t) { ticks+=t; if(completion && ticks-scan_started>=30000 && strcmp(mode,"scan_timeout")) { completion=false;
 for(int i=0;i<(!strcmp(mode,"overflow")?12:1);++i) { advertisement(i); advertisement(i); }
 esp_ble_gap_cb_param_t p{}; p.scan_rst.search_evt=ESP_GAP_SEARCH_INQ_CMPL_EVT; gap(ESP_GAP_BLE_SCAN_RESULT_EVT,&p);
} }
void tesla_ble_iram_snapshot(const char *,const char *,int,bool) {}
'''
MAIN = r'''
int main(int argc,char **argv) {
 mode=argc>1?argv[1]:"success"; if(argc>2 && strcmp(mode,"probe_failure")) fail=argv[2];
 auto &d=tesla_ble_bench_discovery;
 assert(!worker && calls.empty() && d.request_probe(0)==409);
 assert(d.snapshot().target_count==0);
 assert(!d.snapshot().measurements_valid);
 assert(d.request_scan()==(!strcmp(mode,"task_fail")?503:202));
 if(!strcmp(mode,"task_fail")) { auto s=d.snapshot(); assert(s.failure_code==ESP_ERR_NO_MEM && !s.worker_active && s.task_create_code==0); return 0; }
 assert(d.request_scan()==409); worker(context);
 auto scan=d.snapshot(); assert(!scan.worker_active && scan.measurements_valid && scan.stack_min_bytes==4096 && scan.internal8_free==32768);
 assert(std::find(calls.begin(),calls.end(),"gatt.open")==calls.end());
 if(!fail.empty() || !strcmp(mode,"owned") || !strcmp(mode,"params_event_fail") || !strcmp(mode,"scan_timeout") || !strcmp(mode,"blocked") || !strcmp(mode,"slow_cleanup")) {
 assert(strcmp(scan.status,"succeeded") && d.request_probe(0)==409); return 0; }
 assert(!strcmp(scan.status,"succeeded") && scan.teardown_complete);
 if(!strcmp(mode,"malformed") || !strcmp(mode,"non_tesla")) { assert(scan.target_count==0 && d.request_probe(0)==409); return 0; }
 assert(scan.target_count==(!strcmp(mode,"overflow")?8:1)); assert(scan.targets[0].address_type==1);
 assert(!strcmp(scan.targets[0].local_name,"S0123456789abcdefC") && scan.selected_target==-1 && !scan.transport_ready);
 if(!strcmp(mode,"non_connectable")) { calls.clear(); auto before=scan; assert(!scan.targets[0].connectable && d.request_probe(0)==409); scan=d.snapshot(); assert(!scan.probe_attempted && !scan.worker_active && scan.selected_target==before.selected_target && calls.empty()); return 0; }
 assert(scan.targets[0].connectable);
 assert(d.request_probe(8)==409);
 if(!strcmp(mode,"stale")) { ticks+=120001; assert(d.request_probe(0)==409); return 0; }
 calls.clear(); if(!strcmp(mode,"probe_failure")) fail=argv[2];
 const unsigned selected=!strcmp(mode,"overflow")?3:0;
 if(selected) addr[5]+=selected;
 assert(d.request_probe(selected)==202 && d.request_probe(selected)==409); worker(context);
 auto s=d.snapshot(); assert(!s.worker_active && d.request_probe(0)==409);
 if(!fail.empty()) { assert(s.failure_code==-77 && strcmp(s.status,"succeeded")); return 0; }
 if(!strcmp(mode,"bonded") || !strcmp(mode,"bond_query_fail")) { assert(s.failure_code==(!strcmp(mode,"bonded")?ESP_ERR_INVALID_STATE:-1)); assert(std::find(calls.begin(),calls.end(),"gatt.open")==calls.end()); return 0; }
 if(!strcmp(mode,"security")) { assert(s.failure_code==ESP_ERR_INVALID_STATE && !s.transport_ready); assert(std::find(calls.begin(),calls.end(),"gatt.service")==calls.end()); return 0; }
 if(!strcmp(mode,"security_at_cache")) { assert(s.failure_code==ESP_ERR_INVALID_STATE && !s.transport_ready); assert(std::find(calls.begin(),calls.end(),"gatt.write_metadata")==calls.end()); return 0; }
 if(!strcmp(mode,"open_timeout") || !strcmp(mode,"late_open") || !strcmp(mode,"search_timeout") || !strcmp(mode,"close_timeout") || !strcmp(mode,"unreg_timeout")) assert(s.failure_code==ESP_ERR_TIMEOUT);
 else if(!strcmp(mode,"open_event_fail")) assert(s.failure_code==133);
 else if(!strcmp(mode,"no_service") || !strcmp(mode,"wrong_properties") || !strcmp(mode,"no_write") || !strcmp(mode,"no_notify") || !strcmp(mode,"wrong_notify_properties") || !strcmp(mode,"no_cccd")) assert(s.failure_code==ESP_ERR_NOT_FOUND && !s.transport_ready);
 else assert(!strcmp(s.status,"succeeded") && s.transport_ready && s.teardown_complete && s.write_handle==18 && s.notify_handle==19 && s.cccd_handle==21);
}
'''.replace('#include','')

class BenchDiscoveryTests(unittest.TestCase):
    def build(self, temp, main=MAIN, api=False, defines=None, harness=HARNESS):
        (temp/'sdk.h').write_text(DSDK)
        (temp/'freertos').mkdir()
        for h in ('esp_bt.h','esp_bt_main.h','esp_gattc_api.h','esp_gap_ble_api.h','esp_heap_caps.h','esp_rom_sys.h','esp_random.h','freertos/FreeRTOS.h','freertos/task.h'):
            (temp/h).write_text('#include "sdk.h"\n')
        (temp/'test.cpp').write_text('#include <algorithm>\n'+harness+main)
        cmd=['g++','-std=c++17','-Wall','-Wextra','-Werror','-pthread','-I'+str(temp),'-I'+str(MODULE)]
        cmd+=['-D'+d for d in (defines if defines is not None else ('TESLA_BLE_BENCH_ONLY=1','TESLA_BLE_IRAM_DIAGNOSTICS=1'))]
        cmd += [str(temp/'test.cpp'),str(MODULE/'tesla_ble_bench_discovery.cpp')]
        cmd += pairing_link_args(temp)
        if api:
            (temp/'platform.h').write_text(PLATFORM.replace('inline FakeServer server;','inline FakeServer server;'))
        if os.environ.get('TESLA_BLE_SMOKE_SANITIZE')=='1':
            cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
        cmd+=['-o',str(temp/'test')]
        p=subprocess.run(cmd,capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stderr)

    def test_lifecycle_advertisement_selection_and_bounded_probe(self):
        with tempfile.TemporaryDirectory() as directory:
            t=Path(directory); self.build(t)
            for scenario in ('success','malformed','non_tesla','overflow','stale','owned','task_fail','scan_timeout','params_event_fail','open_timeout','open_event_fail','search_timeout','close_timeout','no_service','wrong_properties','no_write','no_notify','wrong_notify_properties','no_cccd','non_connectable','unreg_timeout','blocked','slow_cleanup','late_open','security','security_at_cache','bonded','bond_query_fail'):
                with self.subTest(scenario=scenario):
                    p=subprocess.run([str(t/'test'),scenario],capture_output=True,text=True)
                    self.assertEqual(p.returncode,0,p.stderr)
            for stage in ('controller.init','controller.enable','host.init','host.enable','gap.callback_register','scan.params','scan.start','host.disable','host.deinit','controller.disable','controller.deinit'):
                with self.subTest(stage=stage):
                    p=subprocess.run([str(t/'test'),'failure',stage],capture_output=True,text=True)
                    self.assertEqual(p.returncode,0,p.stderr)
            for stage in ('controller.init','controller.enable','host.init','host.enable','gap.callback_register','gatt.callback_register','gatt.app_register','gatt.open','gatt.search','gatt.service','gatt.write_metadata','gatt.notify_metadata','gatt.cccd_metadata','gatt.close','gatt.app_unregister','host.disable','host.deinit','controller.disable','controller.deinit'):
                with self.subTest(probe_stage=stage):
                    p=subprocess.run([str(t/'test'),'probe_failure',stage],capture_output=True,text=True)
                    self.assertEqual(p.returncode,0,p.stderr)

    def test_local_names_in_both_bounded_ad_blocks_and_metadata(self):
        main=r'''
static std::vector<esp_ble_gap_cb_param_t> reports;
static std::vector<uint8_t> name_ad(std::string name="S0123456789abcdefC",uint8_t type=9) {
 std::vector<uint8_t> bytes={static_cast<uint8_t>(name.size()+1),type}; bytes.insert(bytes.end(),name.begin(),name.end()); return bytes;
}
static void add(std::vector<uint8_t> adv,std::vector<uint8_t> rsp={},int event=ESP_BLE_EVT_CONN_ADV,int type=1,int index=0,int rssi=-45) {
 assert(adv.size()<=31 && rsp.size()<=31); esp_ble_gap_cb_param_t p{};
 p.scan_rst.search_evt=ESP_GAP_SEARCH_INQ_RES_EVT; memcpy(p.scan_rst.bda,addr,6); p.scan_rst.bda[5]+=index;
 p.scan_rst.ble_addr_type=type; p.scan_rst.ble_evt_type=event; p.scan_rst.rssi=rssi;
 p.scan_rst.adv_data_len=adv.size(); p.scan_rst.scan_rsp_len=rsp.size();
 if(!adv.empty()) memcpy(p.scan_rst.ble_adv,adv.data(),adv.size());
 if(!rsp.empty()) memcpy(p.scan_rst.ble_adv+adv.size(),rsp.data(),rsp.size());
 reports.push_back(p);
}
int main(int argc,char **argv) {
 assert(argc==2); std::string scenario=argv[1]; bool accepted=false, connectable=true;
 if(scenario=="adv") { add(name_ad()); accepted=true; }
 else if(scenario=="rsp") { add({2,1,6},name_ad()); accepted=true; }
 else if(scenario=="rsp_no_adv") { add({},name_ad()); accepted=true; }
 else if(scenario=="padded_adv") { add({2,1,6,0,0},name_ad()); accepted=true; }
 else if(scenario=="short_type_full_name") { add(name_ad("S0123456789abcdefC",8)); accepted=true; }
 else if(scenario=="short_then_complete") { add(name_ad("S0123",8),name_ad()); accepted=true; }
 else if(scenario=="direct") { add(name_ad(),{},ESP_BLE_EVT_CONN_DIR_ADV); accepted=true; }
 else if(scenario=="nonconn" || scenario=="scannable") { add({},name_ad(),scenario=="nonconn"?ESP_BLE_EVT_NON_CONN_ADV:ESP_BLE_EVT_DISC_ADV); accepted=true; connectable=false; }
 else if(scenario=="public") { add(name_ad(),{},ESP_BLE_EVT_CONN_ADV,0); accepted=true; }
 else if(scenario=="duplicate") { add(name_ad(),name_ad()); accepted=true; }
 else if(scenario=="update") { add(name_ad()); add({},name_ad("Sfedcba9876543210C"),ESP_BLE_EVT_DISC_ADV,1,0,-79); accepted=true; connectable=false; }
 else if(scenario=="address_type") { add(name_ad(),{},ESP_BLE_EVT_CONN_ADV,0); add(name_ad(),{},ESP_BLE_EVT_CONN_ADV,1); accepted=true; }
 else if(scenario=="uuid_only") { auto u=tesla_ble_uuid(0x11); std::vector<uint8_t> ad={17,7}; ad.insert(ad.end(),u.uuid.uuid128,u.uuid.uuid128+16); add(ad); }
 else if(scenario=="uppercase") add(name_ad("S0123456789ABCDefC"));
 else if(scenario=="nonhex") add({},name_ad("S0123456789abcdegC"));
 else if(scenario=="bad_prefix") add(name_ad("s0123456789abcdefC"));
 else if(scenario=="bad_suffix") add({},name_ad("S0123456789abcdefc"));
 else if(scenario=="truncated") add(name_ad("S0123456789abcdeC",8));
 else if(scenario=="long") add({},name_ad("S0123456789abcdef0C"));
 else if(scenario=="nul") { std::string name="S0123456789abcdefC"; name[8]=0; add(name_ad(name)); }
 else if(scenario=="empty_name") add(name_ad(""));
 else if(scenario=="other_ad_type") add(name_ad("S0123456789abcdefC",0xff));
 else if(scenario=="malformed_adv") add({31,1,6},name_ad());
 else if(scenario=="malformed_rsp") { auto ad=name_ad(); ad[0]=21; add({},ad); }
 else if(scenario=="cross_boundary") { auto ad=name_ad(); std::vector<uint8_t> a(ad.begin(),ad.begin()+10),b(ad.begin()+10,ad.end()); add(a,b); }
 else if(scenario=="trailing_garbage") { auto ad=name_ad(); ad.push_back(3); add(ad); }
 else if(scenario=="conflict") add(name_ad(),name_ad("Sfedcba9876543210C"));
 else if(scenario=="invalid_addr") add(name_ad(),{},ESP_BLE_EVT_CONN_ADV,2);
 else if(scenario=="unknown_evt") add(name_ad(),{},99);
 else if(scenario=="unassociated_rsp") add({},name_ad(),ESP_BLE_EVT_SCAN_RSP);
 else if(scenario=="oversize_adv") { add({},name_ad()); reports.back().scan_rst.adv_data_len=32; }
 else if(scenario=="oversize_rsp") { add(name_ad()); reports.back().scan_rst.scan_rsp_len=32; }
 else assert(false);
 auto &d=tesla_ble_bench_discovery; assert(d.request_scan()==202); worker(context);
 auto s=d.snapshot(); assert(!strcmp(s.status,"succeeded") && s.teardown_complete && !s.worker_active);
 assert(!s.probe_attempted && s.selected_target==-1 && !s.transport_ready);
 assert(std::find(calls.begin(),calls.end(),"gatt.open")==calls.end() && std::find(calls.begin(),calls.end(),"gatt.app_register")==calls.end());
 const unsigned count=!accepted?0:(scenario=="address_type"?2:1); assert(s.target_count==count);
 if(accepted) { const auto &t=s.targets[count-1]; assert(!memcmp(t.address,addr,6) && t.connectable==connectable);
  assert(t.address_type==(scenario=="public"?0:1));
  assert(t.rssi==(scenario=="update"?-79:-45)); assert(!strcmp(t.local_name,scenario=="update"?"Sfedcba9876543210C":"S0123456789abcdefC")); }
 if(!accepted || !connectable) { calls.clear(); assert(d.request_probe(0)==409 && calls.empty() && !d.snapshot().probe_attempted); }
}
'''
        harness=HARNESS.replace('for(int i=0;i<(!strcmp(mode,"overflow")?12:1);++i) { advertisement(i); advertisement(i); }',
                                'for(auto &p:reports) gap(ESP_GAP_BLE_SCAN_RESULT_EVT,&p);')
        # Declare before the injected delay function; the record storage is test-only.
        harness=harness.replace('void vTaskDelay(TickType_t t)', 'static std::vector<esp_ble_gap_cb_param_t> reports;\nvoid vTaskDelay(TickType_t t)')
        main=main.replace('static std::vector<esp_ble_gap_cb_param_t> reports;','')
        with tempfile.TemporaryDirectory() as directory:
            t=Path(directory); self.build(t,main=main,harness=harness)
            for scenario in ('adv','rsp','rsp_no_adv','padded_adv','short_type_full_name','short_then_complete','direct','nonconn','scannable','public','duplicate','update','address_type','uuid_only','uppercase','nonhex','bad_prefix','bad_suffix','truncated','long','nul','empty_name','other_ad_type','malformed_adv','malformed_rsp','cross_boundary','trailing_garbage','conflict','invalid_addr','unknown_evt','unassociated_rsp','oversize_adv','oversize_rsp'):
                with self.subTest(scenario=scenario):
                    p=subprocess.run([str(t/'test'),scenario],capture_output=True,text=True)
                    self.assertEqual(p.returncode,0,p.stderr)

    def test_explicit_pair_worker_after_vin_bound_scan(self):
        harness=HARNESS.replace('memcpy(p.scan_rst.ble_adv+5,"S0123456789abcdefC",18);','char vin[18]{},name[19]; memset(vin,\'A\',17); assert(BenchPairing::vehicle_name(vin,name)); memcpy(p.scan_rst.ble_adv+5,name,18);')
        harness=harness.replace('void vTaskDelay(TickType_t t) { ticks+=t;',r'''
static bool callback_injected=false, observed_wait=false,split_tail=false;
void vTaskDelay(TickType_t t) { ticks+=t;
 if(callback_injected&&!split_tail&&!strcmp(mode,"pair_notify_split")&&!strcmp(tesla_ble_bench_discovery.snapshot().stage,"pair.card_wait")) {
  split_tail=true;uint8_t tail[]={2,8,1};esp_ble_gattc_cb_param_t p{};p.notify.conn_id=42;p.notify.handle=19;p.notify.is_notify=true;memcpy(p.notify.remote_bda,addr,6);p.notify.value=tail;p.notify.value_len=sizeof(tail);gatt(ESP_GATTC_NOTIFY_EVT,7,&p);
 }
 if(!callback_injected && !strcmp(tesla_ble_bench_discovery.snapshot().stage,"pair.card_wait")) {
  callback_injected=true;
  if(!strcmp(mode,"pair_disconnect")) { esp_ble_gattc_cb_param_t p{}; p.close={0,42,{}}; memcpy(p.close.remote_bda,addr,6); gatt(ESP_GATTC_CLOSE_EVT,7,&p); }
  if(!strncmp(mode,"pair_notify_",12) || !strcmp(mode,"pair_rx_overflow")) {
   uint8_t wire[]={0,4,0x22,2,8,1,0,4,0x22,2,8,1,0,4,0x22,2,8,1}; esp_ble_gattc_cb_param_t p{}; p.notify.conn_id=42; p.notify.handle=19; p.notify.is_notify=true; memcpy(p.notify.remote_bda,addr,6); p.notify.value=wire; p.notify.value_len=!strcmp(mode,"pair_rx_overflow")?2049:(!strcmp(mode,"pair_notify_split")?3:(!strcmp(mode,"pair_notify_coalesced")?sizeof(wire):6));
   if(!strcmp(mode,"pair_notify_wait")) {
    // Unrelated callbacks may not feed this target's framing stream.
    p.notify.remote_bda[0]^=1; gatt(ESP_GATTC_NOTIFY_EVT,7,&p); p.notify.remote_bda[0]^=1;
    p.notify.conn_id=41; gatt(ESP_GATTC_NOTIFY_EVT,7,&p); p.notify.conn_id=42;
   }
   gatt(ESP_GATTC_NOTIFY_EVT,7,&p);
  }
 }
 if(!strcmp(tesla_ble_bench_discovery.snapshot().pair_state,"waiting_for_card")) observed_wait=true;
''')
        main=r'''
int main(int argc,char **argv) {
 auto &d=tesla_ble_bench_discovery; char vin[18]{}; memset(vin,'A',17);
 assert(d.request_pair_prepare(0,vin)==409 && d.request_pair_start()==409);
 assert(d.request_scan()==202); worker(context); calls.clear();
 auto s=d.snapshot(); assert(s.target_count==1);
 vin[0]='B'; assert(d.request_pair_prepare(0,vin)==409 && calls.empty()); vin[0]='A';
 assert(d.request_pair_prepare(0,vin)==202); assert(d.request_pair_start()==409); worker(context);
 s=d.snapshot(); assert(!strcmp(s.pair_state,"key_ready") && s.vin_bound && s.local_key_present && !s.worker_active);
 assert(std::find(calls.begin(),calls.end(),"controller.enable")!=calls.end() && std::find(calls.begin(),calls.end(),"gatt.open")==calls.end()); calls.clear();
 assert(d.request_probe(0)==409 && calls.empty());
 if(argc>1) { if(!strncmp(argv[1],"mode:",5)) mode=argv[1]+5; else fail=argv[1]; }
 const int admitted=d.request_pair_start();
 if(!strcmp(mode,"task_fail")) { assert(admitted==503 && d.request_pair_start()==409); s=d.snapshot(); assert(!s.worker_active && !strcmp(s.pair_state,"error") && s.local_key_present); return 0; }
 assert(admitted==202 && d.request_pair_start()==409); worker(context); s=d.snapshot();
 assert(!s.worker_active && s.teardown_complete);
 if(!fail.empty()) { assert(s.failure_code==-77 && strcmp(s.pair_state,"authenticated")); return 0; }
 if(!strncmp(mode,"pair_notify_",12)) { assert(observed_wait); mode="success"; }
 if(strcmp(mode,"success")) { assert(s.failure_code && !strcmp(s.pair_state,"error")); return 0; }
 assert(!strcmp(s.pair_state,"authorization_unverified") && s.request_may_have_reached_tesla && !s.transport_ready);
 assert(sent_wire.size()>2 && size_t(sent_wire[0]*256+sent_wire[1])==sent_wire.size()-2);
 assert(d.request_pair_start()==409);
 assert(d.request_pair_verify()==202); worker(context); s=d.snapshot(); assert(!strcmp(s.pair_state,"error") && s.pair_wait_expired);
 assert(d.request_pair_verify()==409); auto key=s.local_key_present; assert(d.confirm_pair_revoke()==200 && key && d.snapshot().local_key_present);
 assert(d.request_pair_delete()==202); worker(context); s=d.snapshot(); assert(!strcmp(s.pair_state,"no_key") && !s.local_key_present);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            t=Path(directory); self.build(t,main=main,harness=harness)
            for scenario in ('','gatt.open','gatt.subscribe','gatt.cccd_enable','gatt.payload_write','mode:task_fail','mode:security','mode:security_at_cache','mode:pair_disconnect','mode:pair_notify_wait','mode:pair_notify_split','mode:pair_notify_coalesced','mode:pair_rx_overflow'):
                # The disk intentionally survives processes; explicit successful
                # cleanup resets it. Failure paths use their own fresh directory.
                if scenario:
                    for file in (t/'store').rglob('*'):
                        if file.is_file(): file.unlink()
                p=subprocess.run([str(t/'test'),*([scenario] if scenario else [])],capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stderr)

    def test_full_scan_and_exact_supervision_budgets(self):
        # Virtual clock executes the actual worker, waits and main-loop watchdog;
        # no real sleeps, RF activity or fabricated target measurements.
        harness=HARNESS.replace('static uint32_t ticks, scan_started;', 'static const char *timing="full";\nstatic uint32_t ticks, scan_started;')
        harness=harness.replace('return fail==name ? -77 : 0; }', r'''
 const bool probing=!strcmp(tesla_ble_bench_discovery.snapshot().action,"probe");
 if((!strcmp(timing,"overall_scan") && !probing &&
      (!strcmp(name,"controller.init") || !strcmp(name,"controller.enable") || !strcmp(name,"host.init") || !strcmp(name,"host.enable") || !strcmp(name,"gap.callback_register") || !strcmp(name,"scan.params") || !strcmp(name,"scan.start"))) ||
    (!strcmp(timing,"overall_probe") && probing &&
      (!strcmp(name,"controller.init") || !strcmp(name,"controller.enable") || !strcmp(name,"host.init") || !strcmp(name,"host.enable") || !strcmp(name,"gap.callback_register") || !strcmp(name,"security.bond_check") || !strcmp(name,"gatt.callback_register")))) ticks+=4000;
 else if(!probing && strcmp(timing,"overall_scan") &&
         (!strcmp(name,"controller.init") || !strcmp(name,"controller.enable") || !strcmp(name,"host.init") || !strcmp(name,"host.enable"))) ticks+=1000;
 if((!strcmp(timing,"overall_scan") && !probing && !strcmp(name,"host.disable")) ||
    (!strcmp(timing,"overall_probe") && probing && !strcmp(name,"gatt.open"))) {
  ticks+=1999; tesla_ble_bench_discovery.supervise();
  assert(!strcmp(tesla_ble_bench_discovery.snapshot().status,"running"));
  ++ticks; tesla_ble_bench_discovery.supervise();
  auto s=tesla_ble_bench_discovery.snapshot();
  assert(s.worker_active && s.failure_code==ESP_ERR_TIMEOUT && !strcmp(s.failure_stage,name));
 }
 return fail==name ? -77 : 0; }''')
        harness=harness.replace('void vTaskDelay(TickType_t t) { ticks+=t;', r'''
void vTaskDelay(TickType_t t) { ticks+=t;
 if(!strcmp(timing,"stage") && completion) {
  ticks=scan_started+31999; tesla_ble_bench_discovery.supervise();
  assert(!strcmp(tesla_ble_bench_discovery.snapshot().status,"running"));
  ++ticks; tesla_ble_bench_discovery.supervise(); completion=false;
  auto s=tesla_ble_bench_discovery.snapshot();
  assert(s.worker_active && s.failure_code==ESP_ERR_TIMEOUT && !strcmp(s.failure_stage,"scan.complete_event"));
 }
 tesla_ble_bench_discovery.supervise();''')
        harness=harness.replace('ticks-scan_started>=30000', 'ticks-scan_started>=(!strcmp(timing,"near_wait")?31490u:30000u)')
        harness=harness.replace('advertisement(i); advertisement(i);', 'if(strcmp(timing,"empty")) { advertisement(i); advertisement(i); }')
        main=r'''
int main(int argc,char **argv) {
 assert(argc==2); timing=argv[1];
 if(!strcmp(timing,"missing")) mode="scan_timeout";
 auto &d=tesla_ble_bench_discovery;
 assert(d.request_scan()==202 && d.request_scan()==409); worker(context);
 auto s=d.snapshot();
 assert(!s.worker_active && std::find(calls.begin(),calls.end(),"gatt.open")==calls.end());
 if(!strcmp(timing,"missing") || !strcmp(timing,"stage") || !strcmp(timing,"overall_scan")) {
  assert(s.failure_code==ESP_ERR_TIMEOUT && s.restart_required && d.request_probe(0)==409);
  if(!strcmp(timing,"missing")) {
   assert(ticks-scan_started==31500 && !strcmp(s.failure_stage,"scan.complete_event"));
   assert(std::find(calls.begin(),calls.end(),"scan.stop")!=calls.end());
  }
  if(!strcmp(timing,"stage")) assert(ticks-scan_started==32000);
  if(!strcmp(timing,"overall_scan")) assert(ticks==60000 && !strcmp(s.failure_stage,"host.disable"));
  return 0;
 }
 assert(!strcmp(s.status,"succeeded") && s.teardown_complete && !s.restart_required);
 assert(ticks-scan_started==(!strcmp(timing,"near_wait")?31490u:30000u));
 assert(s.target_count==(!strcmp(timing,"empty")?0u:1u));
 assert(std::find(calls.begin(),calls.end(),"scan.stop")==calls.end());
 if(!strcmp(timing,"empty")) { assert(d.request_probe(0)==409); return 0; }
 if(!strcmp(timing,"overall_probe")) {
  const auto start=ticks; assert(d.request_probe(0)==202); worker(context);
  s=d.snapshot(); assert(ticks-start==30000 && s.failure_code==ESP_ERR_TIMEOUT && !strcmp(s.failure_stage,"gatt.open"));
  assert(!s.worker_active && d.request_probe(0)==409);
 }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            t=Path(directory); self.build(t,main=main,harness=harness)
            for scenario in ('full','empty','near_wait','missing','stage','overall_scan','overall_probe'):
                with self.subTest(timing=scenario):
                    p=subprocess.run([str(t/'test'),scenario],capture_output=True,text=True)
                    self.assertEqual(p.returncode,0,p.stderr)

    def test_disabled_and_single_guard_objects_are_empty(self):
        for defines in ((),('TESLA_BLE_BENCH_ONLY=1',),('TESLA_BLE_IRAM_DIAGNOSTICS=1',),('TESLA_BLE_BENCH_ONLY=0','TESLA_BLE_IRAM_DIAGNOSTICS=1'),('TESLA_BLE_BENCH_ONLY=1','TESLA_BLE_IRAM_DIAGNOSTICS=0')):
            for name in ('tesla_ble_bench_discovery.cpp','tesla_ble_bench_discovery_api.cpp','tesla_ble_bench_pairing.cpp','tesla_ble_bench_pair_disk.cpp'):
                with self.subTest(defines=defines,name=name), tempfile.TemporaryDirectory() as directory:
                    obj=Path(directory)/'disabled.o'
                    subprocess.run(['g++','-std=c++17',*['-D'+d for d in defines],'-c',str(MODULE/name),'-o',str(obj)],check=True)
                    self.assertFalse(subprocess.check_output(['nm','-C','--defined-only',str(obj)],text=True).strip())

    def test_no_protocol_payload_key_or_subscription_calls(self):
        source=(MODULE/'tesla_ble_bench_discovery.cpp').read_text().split('void TeslaBleBenchDiscovery::probe()')[1].split('void TeslaBleBenchDiscovery::')[0]
        for forbidden in ('esp_ble_gattc_write_char(', 'esp_ble_gattc_write_char_descr(', 'esp_ble_gattc_read_char(', 'esp_ble_gattc_register_for_notify(', 'esp_ble_set_encryption(', 'esp_ble_gap_start_advertising(', 'esp_bt_mem_release(', 'esp_bt_controller_mem_release(', 'tesla_clean::Vehicle', 'pair_charging_manager(', 'TeslaBleStorage', 'BLEDevice'):
            self.assertNotIn(forbidden,source)
        main=(MODULE/'tesla_vehicle.cpp').read_text()
        self.assertIn('tesla_ble_bench_discovery_register_urls();',main)
        self.assertIn('tesla_ble_bench_discovery.supervise();',main)

    def test_smoke_and_discovery_share_irreversible_boot_admission(self):
        main=r'''
#include "tesla_ble_bench_admission.h"
int main(int argc,char **) {
 auto &d=tesla_ble_bench_discovery;
 if(argc>1) { assert(tesla_bench::claim(1)); assert(d.request_scan()==409 && !worker); }
 else { assert(d.request_scan()==202); assert(!tesla_bench::claim(1)); assert(d.request_scan()==409); }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            t=Path(directory); self.build(t,main=main)
            for args in ((),('smoke',)):
                p=subprocess.run([str(t/'test'),*args],capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stderr)

def pairing_link_args(temp):
    from test_iram_bench_pairing import ROOT, MBED, LIB, SANITIZED_LIB
    if os.environ.get('TESLA_BLE_SMOKE_SANITIZE')=='1': LIB=SANITIZED_LIB
    (temp/'store/config').mkdir(parents=True)
    return ['-DTESLA_CLEAN_STORAGE_PREFIX="'+str(temp/'store')+'"', '-I'+str(ROOT/'software/lib/TeslaBLEClean/include'),'-I'+str(MBED/'include'),str(MODULE/'tesla_ble_bench_pairing.cpp'),str(MODULE/'tesla_ble_bench_pair_disk.cpp'),*[str(p) for p in (ROOT/'software/lib/TeslaBLEClean/src').glob('*.cpp') if p.name!='vehicle.cpp'],str(LIB/'library/libmbedcrypto.a'),str(LIB/'3rdparty/everest/libeverest.a'),str(LIB/'3rdparty/p256-m/libp256m.a')]

if __name__=='__main__': unittest.main()
