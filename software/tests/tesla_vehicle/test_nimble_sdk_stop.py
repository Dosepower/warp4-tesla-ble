"""Exact pinned SDK stop/preempt functions, controlled HCI/NPL boundaries.
Proves host-stop completion is not a pending-CONNECT callback barrier.
This is a native SDK replay, not radio/controller emulation.
"""
from pathlib import Path
import os,re,subprocess,json,hashlib,sys
R=Path(os.environ['TESLA_NIMBLE_SDK_ROOT'])/'esp-idf-secure/components/bt/host/nimble'
O=Path(os.environ['TESLA_CLEAN_TEST_OUT'])/'sdk-stop';O.mkdir(parents=True,exist_ok=True)
sources={}
def extract(rel,name):
 p=R/rel;t=p.read_text();m=re.search(r'(?m)^(?:static )?(?:int|void)\s*\n?'+name+r'\([^;]*?\)\s*\{',t);assert m,name
 i=t.index('{',m.start())+1;depth=1
 while depth:depth+=(t[i]=='{')-(t[i]=='}');i+=1
 sources[name]={'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
 return t[m.start():i]
gap='nimble/nimble/host/src/ble_gap.c';stop='nimble/nimble/host/src/ble_hs_stop.c';port='nimble/porting/nimble/src/nimble_port.c'
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>
#ifndef SLIST_FOREACH_SAFE
#define SLIST_FOREACH_SAFE(v,h,f,n) for((v)=SLIST_FIRST(h);(v)&&((n)=SLIST_NEXT(v,f),1);(v)=(n))
#endif
#define MYNEWT_VAL(x) CFG_##x
#define CFG_BLE_STATIC_TO_DYNAMIC 0
#define CFG_BLE_PERIODIC_ADV 0
#define NIMBLE_BLE_CONNECT 1
#define NIMBLE_BLE_SCAN 0
#define NIMBLE_BLE_ADVERTISE 0
#define BLE_ADV_INSTANCES 0
#define BLE_GAP_OP_NULL 0
#define BLE_GAP_OP_M_CONN 1
#define BLE_GAP_OP_M_DISC 2
#define BLE_HS_EBUSY 1
#define BLE_HS_EALREADY 2
#define BLE_HS_EINVAL 3
#define BLE_HS_EUNKNOWN 4
#define BLE_HS_EPREEMPTED 5
#define BLE_HS_ENABLED_STATE_ON 1
#define BLE_HS_ENABLED_STATE_STOPPING 2
#define BLE_HS_ENABLED_STATE_OFF 0
#define BLE_GAP_EVENT_ADV_COMPLETE 1
#define BLE_GAP_EVENT_DISC_COMPLETE 2
#define BLE_HS_LOG(...) ((void)0)
#define BLE_HS_DBG_ASSERT(x) assert(x)
#define BLE_NPL_TIME_FOREVER 0
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
typedef int esp_err_t;typedef int ble_npl_error_t;
struct ble_gap_event{int type;struct{int reason;}adv_complete,disc_complete;};
typedef int ble_gap_event_fn(struct ble_gap_event*,void*);
struct ble_hs_stop_listener{void(*fn)(int,void*);void*arg;SLIST_ENTRY(ble_hs_stop_listener)link;};
typedef void ble_hs_stop_fn(int,void*);
SLIST_HEAD(ble_hs_stop_listener_slist,ble_hs_stop_listener);
static struct ble_hs_stop_listener_slist ble_hs_stop_listeners;
static struct ble_hs_stop_listener stop_listener;
static int ble_hs_enabled_state=BLE_HS_ENABLED_STATE_ON,ble_hs_stop_gap_listener,ble_hs_stop_terminate_tmo;
static uint8_t ble_hs_stop_conn_cnt;
static int ble_hs_stop_gap_event(struct ble_gap_event*e,void*arg){(void)e;(void)arg;return 0;}
static struct{int preempted_op,op;ble_gap_event_fn*cb;void*cb_arg;}ble_gap_master;
static struct{int preempted;ble_gap_event_fn*cb;void*cb_arg;}ble_gap_slave[1];
static struct{ble_gap_event_fn*cb;void*arg;}slaves[1];
static int preempt_done_mutex;static bool gap_preempt_mutex_initialized=true;
static bool hci_cancel_pending,connect_callback,host_running=true;
static bool nimble_host_ready=true,nimble_init_quarantined,nimble_port_init_failed,nimble_port_run_active=true;
static void ble_hs_lock(void){}static void ble_hs_unlock(void){}
static void ble_hs_timer_resched(void){}static void ble_npl_callout_stop(int*p){(void)p;}
static int ble_gap_event_listener_unregister(int*p){(void)p;return 0;}
static int ble_gap_event_listener_register(int*p,ble_gap_event_fn*fn,void*arg){(void)p;(void)fn;(void)arg;return 0;}
static void ble_npl_mutex_pend(int*p,uint32_t n){(void)p;(void)n;}static void ble_npl_mutex_release(int*p){(void)p;}
static bool ble_gap_is_preempted(void){return ble_gap_master.preempted_op!=0;}
static int ble_gap_conn_cancel_no_lock(void){hci_cancel_pending=true;return 0;}
static int ble_gap_call_event_cb(struct ble_gap_event*e,ble_gap_event_fn*cb,void*arg){return cb?cb(e,arg):0;}
static int ble_hs_stop_terminate_conn;
static void ble_hs_conn_foreach(int fn,void*arg){(void)fn;(void)arg;} // no ESTABLISHED links
static uint32_t ble_npl_time_ms_to_ticks32(int n){return n;}
static void ble_npl_callout_reset(int*p,uint32_t n){(void)p;(void)n;}
#define BLE_HOST_STOP_TIMEOUT_MS 2000
struct ble_npl_sem{int count;};static struct ble_npl_sem ble_hs_stop_sem;
struct ble_npl_event{void(*fn)(struct ble_npl_event*);};static struct ble_npl_event ble_hs_ev_stop;
static int g_eventq_dflt;static struct ble_npl_event*queued;
static int ble_npl_sem_init(struct ble_npl_sem*s,int n){s->count=n;return 0;}
static void ble_npl_sem_release(struct ble_npl_sem*s){s->count++;}
static void ble_npl_sem_deinit(struct ble_npl_sem*s){(void)s;}
static void ble_npl_event_init(struct ble_npl_event*e,void(*fn)(struct ble_npl_event*),void*arg){(void)arg;e->fn=fn;}
static void ble_npl_eventq_put(int*q,struct ble_npl_event*e){(void)q;queued=e;}
static int ble_npl_sem_pend(struct ble_npl_sem*s,int n){(void)n;if(!s->count){assert(queued);queued->fn(queued);queued=NULL;host_running=false;s->count++;}s->count--;return 0;}
'''
body='\n'.join(extract(gap,n) for n in ('ble_gap_preempt_no_lock','ble_gap_preempt','ble_gap_preempt_done'))
body+='\n'+'\n'.join(extract(stop,n) for n in ('ble_hs_stop_done','ble_hs_stop_register_listener','ble_hs_stop_begin','ble_hs_stop'))
body+='\n'+'\n'.join(extract(port,n) for n in ('ble_hs_stop_cb','nimble_port_stop_cb','nimble_port_stop'))
main=r'''
int main(void){
 SLIST_INIT(&ble_hs_stop_listeners);ble_gap_master.op=BLE_GAP_OP_M_CONN;
 assert(nimble_port_stop()==0);
 assert(hci_cancel_pending && !connect_callback && !host_running);
 assert(ble_hs_enabled_state==BLE_HS_ENABLED_STATE_OFF);
 puts("REAL_SDK_STOP: successful host stop with zero established links leaves HCI CONNECT cancellation pending; NOT a callback-quiescence barrier");
 return 0;
}
'''
p=O/'actual-sdk-stop.c';p.write_text(pre+body+main);exe=O/'actual-sdk-stop'
flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie'] if 'sanitize' in sys.argv else []
subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Wno-unused-variable','-Wno-unused-function','-Wno-unused-parameter',*flags,str(p),'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True);print(r.stdout+r.stderr)
(O/'receipt.json').write_text(json.dumps({'functions':sources,'exit':r.returncode,'output':r.stdout+r.stderr,'boundaries':'HCI cancellation asynchronous; no established links; controlled NPL stop event scheduling; actual SDK functions unchanged'},indent=2));assert r.returncode==0
