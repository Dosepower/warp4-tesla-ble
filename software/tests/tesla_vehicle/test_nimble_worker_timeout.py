"""Real production worker plus pinned SDK GATT-abort callback chain.

Only OS/controller/ATT wire, connection lookup and SDK proc-list storage are
host boundaries. Four actual SDK function bodies drain the withheld WRITE before
native GAP DISCONNECT reaches the real adapter. No hardware or live key access.
"""
from pathlib import Path
import os, re, subprocess, sys, tempfile, hashlib, json
T=Path(__file__).resolve().parent
root=Path(os.environ['TESLA_NIMBLE_SDK_ROOT'])
p=root/'esp-idf-secure/components/bt/host/nimble/nimble/nimble/host/src/ble_gattc.c'
s=p.read_text()
def function(name):
 m=re.search(r'\n(?:static )?(?:void|int)\n'+name+r'\([^;]*?\)\n\{',s)
 if not m:raise RuntimeError('Missing real SDK function '+name)
 start=m.start()+1;at=m.end();depth=1
 while depth:
  depth+=(s[at]=='{')-(s[at]=='}');at+=1
 return s[start:at]
names=['ble_gattc_write_cb','ble_gattc_write_err','ble_gattc_fail_procs','ble_gattc_connection_broken']
head=r'''
#include <sys/queue.h>
#define MYNEWT_VAL(x) 0
#define BLE_HS_DBG_ASSERT(x) assert(x)
#define STATS_INC(a,b) ((void)0)
constexpr int BLE_HS_ENOTCONN=7, BLE_GATT_OP_NONE=0;
struct ble_gattc_proc {uint16_t conn_handle=4;uint8_t op=1;struct {WriteFn cb;void*cb_arg;uint16_t att_handle;}write;STAILQ_ENTRY(ble_gattc_proc)next;};
STAILQ_HEAD(ble_gattc_proc_list,ble_gattc_proc);
static ble_gattc_proc *pending_sdk_write=nullptr;
static unsigned sdk_freed=0,sdk_errors=0;
using ble_gattc_err_fn=void(ble_gattc_proc*,int,uint16_t);
static void ble_gattc_write_err(ble_gattc_proc*,int,uint16_t);
static bool ble_hs_locked_by_cur_task(){return false;}
static void ble_gattc_dbg_assert_proc_not_inserted(ble_gattc_proc*){}
static const ble_gatt_error *ble_gattc_error(int status,uint16_t){static ble_gatt_error e;e.status=status;++sdk_errors;return &e;}
static ble_gattc_err_fn *ble_gattc_err_dispatch_get(uint8_t op){assert(op==1);return ble_gattc_write_err;}
static void ble_gattc_extract_by_conn_op(uint16_t conn,uint8_t op,int,ble_gattc_proc_list*out){assert(conn==4&&op==0);STAILQ_INIT(out);if(pending_sdk_write){STAILQ_INSERT_TAIL(out,pending_sdk_write,next);pending_sdk_write=nullptr;}}
static void ble_gattc_proc_free(ble_gattc_proc*p){++sdk_freed;delete p;}
struct os_mbuf_pkthdr{STAILQ_ENTRY(os_mbuf_pkthdr)omp_next;};
STAILQ_HEAD(os_mbuf_hdr_list,os_mbuf_pkthdr);
struct ble_hs_conn{os_mbuf_hdr_list att_tx_q;};
static void ble_hs_lock(){}static void ble_hs_unlock(){}
static ble_hs_conn *ble_hs_conn_find(uint16_t){return nullptr;}
#define OS_MBUF_PKTHDR_TO_MBUF(x) nullptr
static void os_mbuf_free_chain(void*){assert(false);}
'''
foot=r'''
static void sdk_track_pending_write(){
 assert(!pending_sdk_write);pending_sdk_write=new ble_gattc_proc;pending_sdk_write->write={test_written,test_proc_arg,test_handle};
 nimble_before_disconnect=[](){const auto before=sdk_freed;ble_gattc_connection_broken(4);assert(!pending_sdk_write);assert(sdk_freed==before+1&&sdk_errors>0);nimble_before_disconnect={};};
}
'''
with tempfile.TemporaryDirectory(prefix='sdk-worker-drain-',dir=os.environ['TMPDIR'])as td:
 h=Path(td)/'real_sdk_drain.hpp';h.write_text(head+'\n'+'\n'.join(function(n)for n in names)+foot)
 env={**os.environ,'TESLA_NIMBLE_REAL_DRAIN_HEADER':str(h),'TESLA_NIMBLE_MODULE_MODES':os.environ.get('TESLA_TIMEOUT_FLOW_MODES','flow-no-approval flow-missing-ack flow-link-drop-during-write flow-delayed-acks flow-missing-terminal flow-blocked-submit flow-late-ack-no-main flow-approval-50s flow-denied-50s flow-approval-59s flow-aged-approval-59s authority-aged authority-fresh authority-rollover authority-prepare-delay authority-commit-expired followup-security-delayed followup-security-immediate followup-security-burst followup-rx-overflow followup-normal-ack')}
 subprocess.run([sys.executable,str(T/'test_nimble_module.py')]+(['sanitize']if'sanitize'in sys.argv else[]),env=env,check=True,timeout=180)
 print(json.dumps({'status':'PASS','sdk_source_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'real_function_bodies':names,'modes':env['TESLA_NIMBLE_MODULE_MODES'].split(),'boundaries':'OS, radio wire, SDK proc-list storage/connection lookup; no hardware'},sort_keys=True))
