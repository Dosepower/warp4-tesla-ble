"""Pinned real SDK connection timer/cancel/terminal bodies + production central.
Controller HCI, OS timer/lock and connection initiation are explicit boundaries.
No SDK edits, physical radio or real keys. Absence of HCI terminal stays busy.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile,hashlib,json
S=Path(__file__).resolve().parents[2];T=S/'tests/tesla_vehicle'
p=Path(os.environ['TESLA_NIMBLE_SDK_ROOT'])/'esp-idf-secure/components/bt/host/nimble/nimble/nimble/host/src/ble_gap.c';s=p.read_text()
def function(name):
 m=re.search(r'\n(?:static )?(?:void|int|bool|int32_t)\n'+name+r'\([^;]*?\)\n\{',s);assert m,name
 at=m.end();depth=1
 while depth:depth+=(s[at]=='{')-(s[at]=='}');at+=1
 return s[m.start()+1:at]
names=['ble_gap_master_reset_state','ble_gap_has_client','ble_gap_master_extract_state','ble_gap_master_connect_failure','ble_gap_master_connect_cancelled','ble_gap_conn_cancel_tx','ble_gap_conn_cancel_no_lock','ble_gap_conn_cancel','ble_gap_master_timer']
head=r'''
#include "tesla_nimble_central.h"
#include <cstdio>
#define MYNEWT_VAL(x) (MYNEWT_VAL_##x)
#define MYNEWT_VAL_BLE_ROLE_CENTRAL 1
#define NIMBLE_BLE_CONNECT 1
#define NIMBLE_BLE_SCAN 0
#define STATS_INC(a,b) ((void)0)
#define BLE_HS_LOG(...) ((void)0)
#define BLE_HS_DBG_ASSERT(x) assert(x)
#define BLE_HS_HCI_ERR(x) (0x200+(x))
#define BLE_HCI_OP(a,b) 0
constexpr int BLE_HS_CONN_HANDLE_NONE=65535,BLE_GAP_EVENT_LINK_ESTAB=20;
constexpr int BLE_HS_EAPP=9,BLE_HS_ETIMEOUT=13,BLE_HS_EDISABLED=30;
constexpr int BLE_GAP_OP_NULL=0,BLE_GAP_OP_M_CONN=1,BLE_GAP_OP_M_DISC=2;
constexpr int BLE_ERR_CMD_DISALLOWED=12,BLE_ERR_UNKNOWN_HCI_CMD=1,BLE_HS_FOREVER=INT32_MAX,BLE_GAP_CANCEL_RETRY_TIMEOUT_MS=100;
namespace sdk {
struct ble_gap_master_state {int op=0,exp_set=0,preempted_op=0;GapFn cb=nullptr;void*cb_arg=nullptr;struct{int cancel=0;ble_addr_t peer_addr;}conn;};
static ble_gap_master_state ble_gap_master;
static int hci_rc=0,hci_calls=0;static unsigned ticks=0;
static void ble_hs_lock(){} static void ble_hs_unlock(){} static void ble_hs_timer_resched(){}
static bool ble_hs_is_enabled(){return true;}
static bool ble_gap_conn_active(){return ble_gap_master.op==BLE_GAP_OP_M_CONN;}
static int ble_hs_hci_cmd_tx(int,const void*,int,void*,int){++hci_calls;return hci_rc;}
static uint32_t ble_gap_master_ticks_until_exp(){return ticks;}
static uint32_t ble_npl_time_ms_to_ticks32(uint32_t n){return n;}
'''
foot=r'''
static void arm(){ble_gap_master={};ble_gap_master.op=BLE_GAP_OP_M_CONN;ble_gap_master.exp_set=1;ble_gap_master.cb=test_gap;ble_gap_master.cb_arg=test_gap_arg;hci_rc=0;}
}
static TeslaNimbleCentral*cptr=nullptr;static unsigned failures=0;
static void received(const TeslaNimbleEvent&e){if(e.kind==TeslaNimbleEvent::Open&&e.status){assert(!cptr->idle());++failures;}}
int main(){TeslaNimbleCentral c;cptr=&c;assert(c.initialize(received));ble_hs_cfg.sync_cb();const uint8_t addr[]={1,2,3,4,5,6};
 for(unsigned attempt=1;attempt<=8;++attempt){assert(c.open(addr,0,attempt));sdk::arm();const auto before=failures;
  if(attempt==1){sdk::ticks=1;assert(sdk::ble_gap_master_timer()==1&&sdk::hci_calls==0);sdk::ticks=0;assert(sdk::ble_gap_master_timer()==BLE_HS_FOREVER);assert(!c.idle()&&failures==before);sdk::ble_gap_master_connect_cancelled();}
  if(attempt==2){assert(sdk::ble_gap_conn_cancel()==0&&!c.idle());sdk::ble_gap_master_connect_cancelled();}
  if(attempt==3){sdk::hci_rc=BLE_HS_HCI_ERR(BLE_ERR_CMD_DISALLOWED);assert(sdk::ble_gap_master_timer()==BLE_HS_FOREVER);}
  if(attempt==4){sdk::hci_rc=BLE_HS_HCI_ERR(BLE_ERR_UNKNOWN_HCI_CMD);assert(sdk::ble_gap_master_timer()==BLE_HS_FOREVER);}
  if(attempt==5){sdk::hci_rc=99;assert(sdk::ble_gap_master_timer()==100&&!c.idle());sdk::hci_rc=0;assert(sdk::ble_gap_master_timer()==BLE_HS_FOREVER&&!c.idle());sdk::ble_gap_master_connect_cancelled();}
  if(attempt==6){sdk::hci_rc=BLE_HS_HCI_ERR(BLE_ERR_CMD_DISALLOWED);assert(sdk::ble_gap_conn_cancel()==sdk::hci_rc&&!c.idle());sdk::ble_gap_master_connect_failure(13);}
  if(attempt==7){sdk::ble_gap_master.op=0;assert(sdk::ble_gap_conn_cancel()==BLE_HS_EALREADY&&!c.idle());sdk::ble_gap_master_connect_failure(13);}
  if(attempt==8){assert(sdk::ble_gap_conn_cancel()==0&&!c.idle());assert(!c.shutdown()&&!c.deinitialize());sdk::ble_gap_master_connect_cancelled();}
  assert(c.idle()&&failures==before+1);std::printf("REAL_SDK_CONNECT case=%u terminal_required=1 PASS\n",attempt);
 }
 assert(c.shutdown()&&c.deinitialize());
}
'''
with tempfile.TemporaryDirectory(prefix='real-connect-',dir=os.environ['TMPDIR'])as td:
 d=Path(td);src=d/'test.cpp';src.write_text(head+'\n'.join(function(n)for n in names)+foot)
 flags=['-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-class-memaccess','-pthread','-DTESLA_NIMBLE_HOST_TEST=1','-DTESLA_BLE_USE_NIMBLE=1','-I'+str(T/'host'),'-I'+str(S/'src/modules/tesla_vehicle')]
 if 'sanitize'in sys.argv:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
 subprocess.run(['g++',*flags,str(src),str(S/'src/modules/tesla_vehicle/tesla_nimble_central.cpp'),'-o',str(d/'test')],check=True);subprocess.run([str(d/'test')],check=True,timeout=30)
 print(json.dumps({'source_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'functions':names,'cases':8,'status':'PASS'}))
