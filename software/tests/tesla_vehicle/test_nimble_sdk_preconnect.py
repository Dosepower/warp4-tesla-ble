"""Real pinned NimBLE GAP connection-establishment bodies + production central.

Reproduces the live OPEN quarantine (connect_flags=15, connect_cancel_rc=2):
with CONFIG_BT_NIMBLE_DEFER_CONN_EVENTS_UNTIL_CONNECT=1 the real SDK clears the
master procedure at HCI LE Connection Complete, but posts BLE_GAP_EVENT_CONNECT
only after remote feature/version exchange. Inside that window
ble_gap_conn_cancel() returns BLE_HS_EALREADY and the only terminal the SDK can
deliver is DISCONNECT (or a late CONNECT). The physical link must be retired
and that DISCONNECT accepted as this OPEN's terminal; a missing terminal must
remain quarantined.

Executed unchanged SDK function bodies are extracted from the pinned
esp-idf-secure checkout (ble_gap.c, ble_hs_conn.c). Host boundaries: controller
HCI wire (commands recorded, events injected), host lock/timer, connection
allocation/list storage, address derivation, GATT/L2CAP/SM broken hooks and
deferred-event queue bookkeeping. No hardware, radio, key or identifier.
Usage: TESLA_NIMBLE_SDK_ROOT=... TMPDIR=... python3 test_nimble_sdk_preconnect.py [sanitize] [case ...]
"""
from pathlib import Path
import os, re, subprocess, sys, tempfile, hashlib, json
S = Path(__file__).resolve().parents[2]; T = S / 'tests/tesla_vehicle'
SRC = Path(os.environ['TESLA_NIMBLE_SDK_ROOT']) / 'esp-idf-secure/components/bt/host/nimble/nimble/nimble/host/src'
GAP = SRC / 'ble_gap.c'; CONN = SRC / 'ble_hs_conn.c'
texts = {GAP: GAP.read_text(), CONN: CONN.read_text()}

def function(path, name):
    s = texts[path]
    ms = list(re.finditer(r'\n(?:static )?[A-Za-z_][\w \*]*?\n' + name + r'\([^;{]*?\)\n\{', s))
    assert len(ms) == 1, (name, len(ms))
    m = ms[0]; at = m.end(); depth = 1
    while depth:
        depth += (s[at] == '{') - (s[at] == '}'); at += 1
    return s[m.start() + 1:at]

GAP_NAMES = ['ble_gap_master_reset_state', 'ble_gap_has_client', 'ble_gap_master_extract_state',
    'ble_gap_master_connect_failure', 'ble_gap_master_connect_cancelled', 'ble_gap_master_failed',
    'ble_gap_conn_cancel_tx', 'ble_gap_conn_cancel_no_lock', 'ble_gap_conn_cancel', 'ble_gap_conn_active',
    'ble_gap_master_in_progress', 'ble_gap_accept_master_conn', 'ble_gap_disconnect_unclaimed_conn',
    'ble_gap_rd_rem_sup_feat_tx', 'ble_gap_rd_rem_ver_tx', 'ble_gap_call_event_cb', 'ble_gap_extract_conn_cb',
    'ble_gap_call_conn_event_cb', 'ble_gap_fill_conn_desc', 'ble_gap_conn_to_snapshot', 'ble_gap_find_snapshot',
    'ble_gap_event_connect_call', 'ble_gap_rx_conn_complete', 'ble_gap_rx_rd_rem_sup_feat_complete',
    'ble_gap_rx_rd_rem_ver_info_complete', 'ble_gap_conn_broken', 'ble_gap_rx_disconn_complete',
    'ble_gap_terminate_with_conn', 'ble_gap_terminate', 'ble_gap_conn_find_by_addr', 'ble_gap_master_timer']
CONN_NAMES = ['ble_hs_conn_find_by_addr']

HEAD = r'''
#include "tesla_nimble_central.h"
#include <sys/queue.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define MYNEWT_VAL(x) (MYNEWT_VAL_##x)
#define MYNEWT_VAL_BLE_ROLE_CENTRAL 1
#define MYNEWT_VAL_BLE_ROLE_OBSERVER 1
#define MYNEWT_VAL_BLE_GATTC 1
#define MYNEWT_VAL_BLE_HS_PVCY 1
#define MYNEWT_VAL_BLE_DEFER_CONN_EVENTS 1
#define MYNEWT_VAL_BT_NIMBLE_MEM_OPTIMIZATION 1
#define NIMBLE_BLE_CONNECT 1
#define NIMBLE_BLE_SCAN 0
#define NIMBLE_BLE_ADVERTISE 0
#define NIMBLE_BLE_SM 1
#define STATS_INC(a,b) ((void)0)
#define BLE_HS_LOG(...) ((void)0)
#define BLE_HS_DBG_ASSERT(x) assert(x)
#define BLE_HS_HCI_ERR(x) (0x200+(x))
#define BLE_HCI_OP(ogf,ocf) ((uint16_t)(((ogf)<<10)|(ocf)))
#ifndef htole16
#define htole16(x) (x)
#endif
#ifndef le16toh
#define le16toh(x) (x)
#endif
#define BLE_ADDR_PUBLIC 0
#define BLE_ADDR_RANDOM 1
#define BLE_ADDR_PUBLIC_ID 2
#define BLE_ADDR_RANDOM_ID 3
#define BLE_ADDR_IS_RPA(a) (((a)->type==BLE_ADDR_RANDOM)&&(((a)->val[5]&0xc0)==0x40))
static const ble_addr_t ble_addr_any_storage{};
#define BLE_ADDR_ANY (&ble_addr_any_storage)
static int ble_addr_cmp(const ble_addr_t *a,const ble_addr_t *b){int t=int(a->type)-int(b->type);return t?t:memcmp(a->val,b->val,6);}
static uint32_t get_le32(const uint8_t *p){return p[0]|p[1]<<8|p[2]<<16|uint32_t(p[3])<<24;}
constexpr int BLE_HS_EAGAIN=1,BLE_HS_EINVAL=3,BLE_HS_ENOENT=5,BLE_HS_ENOMEM=6,BLE_HS_ENOTCONN=7,BLE_HS_EAPP=9,BLE_HS_ETIMEOUT=13,BLE_HS_EPREEMPTED=29,BLE_HS_EDISABLED=30,BLE_HS_FOREVER=INT32_MAX;
constexpr int BLE_GAP_EVENT_LINK_ESTAB=40,BLE_GAP_EVENT_TERM_FAILURE=41,BLE_GAP_EVENT_CONN_UPDATE_REQ=42;
constexpr uint16_t BLE_HS_CONN_HANDLE_NONE=0xffff,BLE_HCI_LE_CONN_HANDLE_MAX=0x0eff;
constexpr int BLE_GAP_OP_NULL=0,BLE_GAP_OP_M_DISC=1,BLE_GAP_OP_M_CONN=2,BLE_GAP_CANCEL_RETRY_TIMEOUT_MS=100;
constexpr uint8_t BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER=0,BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE=1;
constexpr int BLE_HS_CONN_F_MASTER=0x01,BLE_HS_CONN_F_TERMINATING=0x02;
constexpr uint32_t BLE_HS_HCI_LE_FEAT_CONN_PARAM_REQUEST=2;
constexpr uint16_t BLE_HCI_SUGG_DEF_DATALEN_TX_OCTETS_MAX=0xfb,BLE_HCI_SUGG_DEF_DATALEN_TX_TIME_MAX=0x848;
enum { BLE_ERR_SUCCESS=0,BLE_ERR_UNK_CONN_ID=0x02,BLE_ERR_CONN_SPVN_TMO=0x08,BLE_ERR_UNKNOWN_HCI_CMD=0x01,BLE_ERR_CMD_DISALLOWED=0x0c,BLE_ERR_CONN_TERM_LOCAL=0x16,BLE_ERR_LMP_LL_RSP_TMO=0x22,BLE_ERR_DIR_ADV_TMO=0x3c,BLE_ERR_CONN_ESTABLISHMENT=0x3e };
constexpr int BLE_HCI_OGF_LINK_CTRL=0x01,BLE_HCI_OGF_LE=0x08,BLE_HCI_OCF_DISCONNECT_CMD=0x06,BLE_HCI_OCF_RD_REM_VER_INFO=0x1d,BLE_HCI_OCF_LE_CREATE_CONN_CANCEL=0x0e,BLE_HCI_OCF_LE_RD_REM_FEAT=0x16;
typedef int ble_gap_event_fn(ble_gap_event *,void *);
struct ble_hci_lc_disconnect_cp{uint16_t conn_handle;uint8_t reason;};
struct ble_hci_le_rd_rem_feat_cp{uint16_t conn_handle;};
struct ble_hci_rd_rem_ver_info_cp{uint16_t conn_handle;};
struct ble_hci_ev_disconn_cmp{uint8_t status;uint16_t conn_handle;uint8_t reason;};
struct ble_hci_ev_le_subev_rd_rem_used_feat{uint8_t status;uint16_t conn_handle;uint8_t features[8];};
struct ble_hci_ev_rd_rem_ver_info_cmp{uint8_t status;uint16_t conn_handle;uint8_t version;uint16_t manufacturer,subversion;};
struct ble_gap_conn_complete{uint8_t status;uint16_t connection_handle;uint8_t role,peer_addr_type,peer_addr[6];uint16_t conn_itvl,conn_latency,supervision_timeout;uint8_t master_clk_acc,local_rpa[6],peer_rpa[6];};
struct ble_hs_conn {
 SLIST_ENTRY(ble_hs_conn) bhc_next;uint16_t bhc_handle=0;uint8_t bhc_our_addr_type=0;uint32_t bhc_flags=0;
 ble_addr_t bhc_peer_addr,bhc_our_rpa_addr,bhc_peer_rpa_addr;uint16_t bhc_itvl=0,bhc_latency=0,bhc_supervision_timeout=0;uint8_t bhc_master_clock_accuracy=0;
 ble_gap_sec_state bhc_sec_state{};ble_gap_event_fn *bhc_cb=nullptr;void *bhc_cb_arg=nullptr;uint32_t supported_feat=0;uint8_t slave_conn=0;
 struct{uint8_t version;uint16_t manufacturer,subversion;}bhc_rd_rem_ver_params{};uint16_t bhc_max_tx_time=0,bhc_max_rx_time=0,bhc_max_tx_octets=0,bhc_max_rx_octets=0;
 uint8_t bhc_deferred_pvcy_add=0,bhc_deferred_pvcy_replace=0,bhc_connect_delivered=0;ble_addr_t bhc_deferred_pvcy_addr;uint8_t bhc_deferred_pvcy_irk[16]{};
};
struct ble_hs_conn_addrs{ble_addr_t our_id_addr,peer_id_addr,our_ota_addr,peer_ota_addr;};
struct ble_gap_snapshot{ble_gap_conn_desc *desc;ble_gap_event_fn *cb;void *cb_arg;};
struct ble_gap_master_state{uint8_t op=0,exp_set=0,preempted_op=0;uint32_t exp_os_ticks=0;ble_gap_event_fn *cb=nullptr;void *cb_arg=nullptr;struct{uint8_t using_wl=0,our_addr_type=0,cancel=0;ble_addr_t peer_addr;}conn;};
struct ble_gap_slave_state{ble_gap_event_fn *cb=nullptr;void *cb_arg=nullptr;uint8_t our_addr_type=0;};
struct ble_gap_update_entry{int unused;};
static ble_gap_master_state ble_gap_master;static ble_gap_slave_state ble_gap_slave[1];
static SLIST_HEAD(,ble_hs_conn) ble_hs_conns=SLIST_HEAD_INITIALIZER(ble_hs_conns);
static ble_addr_t ble_gap_deferred_pvcy_addr;static uint8_t ble_gap_deferred_pvcy_irk[16];static bool ble_gap_deferred_pvcy_add,ble_gap_deferred_pvcy_replace;
// --- recorded host boundaries ---
static int hci_rc_disconnect=0,hci_rc_cancel=0,hci_disconnects=0,hci_cancels=0,hci_feature_reads=0,hci_version_reads=0,gattc_broken=0,delivered_marks=0,deferred_discards=0,drain_starts=0;static uint16_t hci_last_disconnect=0xffff;static unsigned master_ticks=1;
static int lock_depth=0;static void ble_hs_lock(){assert(lock_depth==0);++lock_depth;} static void ble_hs_unlock(){assert(lock_depth==1);--lock_depth;} static bool ble_hs_locked_by_cur_task(){return lock_depth>0;} static void ble_hs_timer_resched(){}
static bool ble_hs_is_enabled(){return true;}
static uint32_t ble_gap_master_ticks_until_exp(){return master_ticks;}
static uint32_t ble_npl_time_ms_to_ticks32(uint32_t n){return n;}
static int ble_hs_hci_cmd_tx(uint16_t op,const void *cmd,uint8_t,void *,uint8_t){
 if(op==BLE_HCI_OP(BLE_HCI_OGF_LINK_CTRL,BLE_HCI_OCF_DISCONNECT_CMD)){++hci_disconnects;if(hci_rc_disconnect)return hci_rc_disconnect;hci_last_disconnect=static_cast<const ble_hci_lc_disconnect_cp*>(cmd)->conn_handle;return 0;}
 if(op==BLE_HCI_OP(BLE_HCI_OGF_LE,BLE_HCI_OCF_LE_CREATE_CONN_CANCEL)){++hci_cancels;return hci_rc_cancel;}
 if(op==BLE_HCI_OP(BLE_HCI_OGF_LE,BLE_HCI_OCF_LE_RD_REM_FEAT)){++hci_feature_reads;return 0;}
 if(op==BLE_HCI_OP(BLE_HCI_OGF_LINK_CTRL,BLE_HCI_OCF_RD_REM_VER_INFO)){++hci_version_reads;return 0;}
 std::abort();
}
static ble_hs_conn *ble_hs_conn_find(uint16_t h){ble_hs_conn *c;SLIST_FOREACH(c,&ble_hs_conns,bhc_next)if(c->bhc_handle==h)return c;return nullptr;}
static int ble_hs_atomic_conn_flags(uint16_t h,uint32_t *f){auto*c=ble_hs_conn_find(h);if(!c)return BLE_HS_ENOTCONN;if(f)*f=c->bhc_flags;return 0;}
static ble_hs_conn *ble_hs_conn_alloc(uint16_t h){auto*c=new ble_hs_conn;c->bhc_handle=h;return c;}
static void ble_hs_conn_insert(ble_hs_conn *c){SLIST_INSERT_HEAD(&ble_hs_conns,c,bhc_next);}
static void ble_hs_atomic_conn_delete(uint16_t h){auto*c=ble_hs_conn_find(h);if(c){SLIST_REMOVE(&ble_hs_conns,c,ble_hs_conn,bhc_next);delete c;}}
static void ble_hs_conn_addrs(const ble_hs_conn *c,ble_hs_conn_addrs *a){*a={};a->peer_id_addr=c->bhc_peer_addr;a->peer_ota_addr=c->bhc_peer_addr;}
static int ble_gap_event_listener_call(ble_gap_event *){return 0;}
static bool ble_gap_master_conn_matches_slave_complete(const ble_gap_conn_complete *){return false;}
static int ble_gap_accept_slave_conn(uint8_t){return BLE_HS_ENOENT;}
static void ble_gap_slave_reset_state(uint8_t){}
static ble_gap_update_entry *ble_gap_update_entry_remove(uint16_t){return nullptr;}
static void ble_gap_update_notify(uint16_t,int){} static void ble_gap_update_entry_free(ble_gap_update_entry *){}
static void ble_l2cap_sig_conn_broken(uint16_t,int){} static void ble_sm_connection_broken(uint16_t){}
static void ble_gattc_connection_broken(uint16_t){++gattc_broken;} static void ble_hs_flow_connection_broken(uint16_t){}
static void ble_gap_apply_deferred_pvcy_add(){}
static void ble_gap_mark_connect_delivered(uint16_t h){auto*c=ble_hs_conn_find(h);if(c){c->bhc_connect_delivered=1;++delivered_marks;}}
static void ble_gap_deferred_drain_start(uint16_t){++drain_starts;} static void ble_gap_conn_deferred_discard(ble_hs_conn *){++deferred_discards;}
static int ble_hs_hci_util_set_data_len(uint16_t,uint16_t,uint16_t){return 0;}
int ble_gap_conn_active(void);ble_hs_conn *ble_hs_conn_find_by_addr(const ble_addr_t *addr);
'''

FOOT = r'''
// Controller/initiation boundary: the state real ble_gap_connect() establishes
// after a successful LE Create Connection submission.
static ble_gap_event_fn *gap_fn=nullptr;static void *gap_arg=nullptr;
static int tesla_gap_entry(ble_gap_event *e){return gap_fn(e,gap_arg);}
int ble_gap_connect(uint8_t own,const ble_addr_t *peer,int32_t,const void *,GapFn cb,void *arg){
 gap_fn=cb;gap_arg=arg;
 if(ble_gap_master.op!=BLE_GAP_OP_NULL)return BLE_HS_EALREADY;
 ble_gap_master.op=BLE_GAP_OP_M_CONN;ble_gap_master.cb=cb;ble_gap_master.cb_arg=arg;ble_gap_master.conn.peer_addr=*peer;ble_gap_master.conn.our_addr_type=own;ble_gap_master.exp_set=1;return 0;
}
static ble_addr_t peer_addr(){return ble_gap_master.conn.peer_addr;}
static void hci_conn_ok(uint16_t h){ble_gap_conn_complete e{};e.status=0;e.connection_handle=h;e.role=BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER;const auto p=peer_addr();e.peer_addr_type=p.type;memcpy(e.peer_addr,p.val,6);assert(ble_gap_rx_conn_complete(&e,0)==0);}
static void hci_conn_cancelled(){ble_gap_conn_complete e{};e.status=BLE_ERR_UNK_CONN_ID;ble_gap_rx_conn_complete(&e,0);}
static void hci_feat(uint16_t h,uint8_t st){ble_hci_ev_le_subev_rd_rem_used_feat e{};e.status=st;e.conn_handle=h;ble_gap_rx_rd_rem_sup_feat_complete(&e);}
static void hci_ver(uint16_t h,uint8_t st){ble_hci_ev_rd_rem_ver_info_cmp e{};e.status=st;e.conn_handle=h;ble_gap_rx_rd_rem_ver_info_complete(&e);}
static void hci_disc(uint16_t h,uint8_t reason){ble_hci_ev_disconn_cmp e{};e.status=0;e.conn_handle=h;e.reason=reason;ble_gap_rx_disconn_complete(&e);}
static TeslaNimbleCentral *central=nullptr;static unsigned open_ok=0,open_fail=0,disconnected=0,events_while_idle=0;static int last_open_status=0;
static void received(const TeslaNimbleEvent &e){
 if(central->idle())++events_while_idle;
 if(e.kind==TeslaNimbleEvent::Open){if(e.status){++open_fail;last_open_status=e.status;}else ++open_ok;}
 if(e.kind==TeslaNimbleEvent::Disconnected)++disconnected;
}
static const char *current="";
#define CHECK(x,why) do{if(!(x)){std::printf("PRECONNECT_RED case=%s reason=%s line=%d\n",current,why,__LINE__);std::fflush(stdout);std::_Exit(3);}}while(0)
static uint64_t epoch=0;
static void start(){const uint8_t addr[]={0xc1,2,3,4,5,6};CHECK(central->open(addr,1,++epoch),"open admission");CHECK(!central->idle(),"busy after submit");}
static void settled(){CHECK(central->idle(),"terminal drained");CHECK(SLIST_EMPTY(&ble_hs_conns),"no SDK link left");CHECK(ble_gap_master.op==BLE_GAP_OP_NULL,"no master procedure left");}
static uint32_t flags(){return central->connect_diagnostic().flags;}
static void run_case(const std::string &name){
 current=name.c_str();const auto f0=open_fail,o0=open_ok,d0=disconnected;hci_rc_disconnect=0;hci_rc_cancel=0;master_ticks=1;
 if(name=="disconnect-before-connect"){
  // 0x3E after HCI success: SDK posts no CONNECT; DISCONNECT is the only terminal.
  start();hci_conn_ok(4);CHECK(ble_gap_master.op==BLE_GAP_OP_NULL&&hci_feature_reads>0,"real SDK cleared master and started feature read");
  hci_feat(4,BLE_ERR_CONN_ESTABLISHMENT);CHECK(!central->idle()&&open_fail==f0,"no terminal yet");
  hci_disc(4,BLE_ERR_CONN_ESTABLISHMENT);CHECK(central->idle(),"pre-CONNECT DISCONNECT must drain this OPEN");
  CHECK(open_fail==f0+1&&open_ok==o0&&disconnected==d0&&last_open_status==BLE_HS_HCI_ERR(BLE_ERR_CONN_ESTABLISHMENT),"exactly one OPEN failure, no session disconnect");
  CHECK((flags()&(16|256))==256,"diagnostic: unannounced link DISCONNECT, no CONNECT");
  hci_disc(4,BLE_ERR_CONN_ESTABLISHMENT);hci_ver(4,0);CHECK(open_fail==f0+1&&open_ok==o0,"duplicate/late SDK events after deletion reach no callback");
 } else if(name=="stalled-features-cancel"){
  // Live signature: link up, CONNECT withheld, worker cancels at 7 s.
  start();hci_conn_ok(5);const auto disc=hci_disconnects;
  const int rc=central->cancel_open();
  CHECK(central->connect_diagnostic().cancel_rc==BLE_HS_EALREADY,"real SDK returns EALREADY in deferred window");
  CHECK(hci_disconnects==disc+1&&hci_last_disconnect==5,"unannounced physical link terminated");
  CHECK(rc==0,"retirement accepted");CHECK(!central->idle()&&!central->shutdown()&&!central->deinitialize(),"retained until real DISCONNECT");ble_hs_cfg.sync_cb(); // shutdown() probe closed admission; host resync
  CHECK((flags()&(4|8|64|128))==(4|8|64|128),"diagnostic bits cancel+lookup+terminate");
  central->cancel_open();CHECK(hci_disconnects==disc+1,"no repeated terminate");
  hci_disc(5,BLE_ERR_CONN_TERM_LOCAL);CHECK(central->idle()&&open_fail==f0+1&&open_ok==o0,"drained OPEN failure");
  hci_feat(5,0);CHECK(open_ok==o0,"late feature completion after deletion is inert");
 } else if(name=="cancel-then-late-connect"){
  start();hci_conn_ok(6);central->cancel_open();hci_feat(6,0);hci_ver(6,0);
  CHECK(open_ok==o0&&!central->idle(),"late CONNECT after cancel is never admitted");
  hci_disc(6,BLE_ERR_CONN_TERM_LOCAL);CHECK(central->idle()&&open_ok==o0,"drained");
 } else if(name=="cancel-race-link"){
  // Cancel accepted while M_CONN, controller still completes the link.
  start();const int rc=central->cancel_open();CHECK(rc==0&&hci_cancels>0&&ble_gap_master.conn.cancel==1,"real cancel accepted");
  const auto cancels=hci_cancels;hci_conn_ok(7);CHECK(ble_gap_master.op==BLE_GAP_OP_NULL,"link won the race");
  central->cancel_open();CHECK(hci_cancels==cancels,"no second LE cancel command");CHECK(hci_last_disconnect==7,"repeated drain step terminates raced link");
  hci_disc(7,BLE_ERR_CONN_TERM_LOCAL);CHECK(central->idle()&&open_fail==f0+1,"drained");
 } else if(name=="absent-peer-timeout"){
  start();master_ticks=0;CHECK(ble_gap_master_timer()==BLE_HS_FOREVER,"SDK timer cancels");hci_conn_cancelled();
  CHECK(central->idle()&&open_fail==f0+1&&last_open_status==BLE_HS_ETIMEOUT,"normal timeout terminal");
 } else if(name=="undrained-terminate"){
  start();hci_conn_ok(8);central->cancel_open();for(int i=0;i<20;++i)central->cancel_open();
  CHECK(!central->idle()&&!central->shutdown()&&!central->deinitialize(),"missing DISCONNECT stays quarantined");ble_hs_cfg.sync_cb();
  std::printf("PRECONNECT_QUARANTINE retained flags=%u\n",flags());
  hci_disc(8,BLE_ERR_CONN_TERM_LOCAL);CHECK(central->idle(),"cleanup after test");
 } else if(name=="terminate-error-retry"){
  start();hci_conn_ok(9);hci_rc_disconnect=BLE_HS_HCI_ERR(BLE_ERR_CMD_DISALLOWED);const int rc=central->cancel_open();
  CHECK(rc==hci_rc_disconnect&&!central->idle(),"terminate failure reported, ownership retained");CHECK((flags()&(64|128))==64,"lookup without accepted terminate");
  hci_rc_disconnect=0;central->cancel_open();CHECK(hci_last_disconnect==9&&(flags()&128),"bounded retry terminates");
  hci_disc(9,BLE_ERR_CONN_TERM_LOCAL);CHECK(central->idle()&&open_fail==f0+1,"drained");
 } else if(name=="normal-connect"){
  start();hci_conn_ok(10);hci_feat(10,0);hci_ver(10,0);CHECK(open_ok==o0+1&&!central->idle(),"CONNECT after exchange");
  hci_disc(10,BLE_ERR_CONN_TERM_LOCAL);CHECK(central->idle()&&disconnected==d0+1,"session disconnect");
 } else if(name=="foreign-handle"){
  start();hci_conn_ok(11);central->cancel_open();
  ble_gap_event e{};e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.reason=BLE_HS_HCI_ERR(0x13);e.disconnect.conn.conn_handle=12;
  // Synthetic (the real SDK cannot address another handle to this cookie).
  tesla_gap_entry(&e);CHECK(!central->idle()&&open_fail==f0,"other handle cannot drain learned link");
  hci_disc(11,BLE_ERR_CONN_TERM_LOCAL);CHECK(central->idle()&&open_fail==f0+1,"drained");
 } else { std::printf("unknown case %s\n",name.c_str());std::_Exit(2); }
 CHECK(lock_depth==0,"host lock balanced");settled();CHECK(events_while_idle==0,"no callback after idle");std::printf("PRECONNECT case=%s PASS flags=%u\n",current,flags());std::fflush(stdout);
}
int main(int argc,char **argv){
 TeslaNimbleCentral c;central=&c;assert(c.initialize(received));ble_hs_cfg.sync_cb();
 std::vector<std::string> cases;for(int i=1;i<argc;++i)cases.push_back(argv[i]);
 for(unsigned round=0;round<3;++round)for(const auto &n:cases)run_case(n);
 assert(c.shutdown()&&c.deinitialize());
 std::printf("PRECONNECT_ALL PASS cases=%zu rounds=3 attempts=%u\n",cases.size(),c.connect_diagnostic().attempt);
}
'''

ALL = ['disconnect-before-connect', 'stalled-features-cancel', 'cancel-then-late-connect', 'cancel-race-link',
       'absent-peer-timeout', 'undrained-terminate', 'terminate-error-retry', 'normal-connect', 'foreign-handle']
if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != 'sanitize']
    cases = args or ALL
    central = Path(os.environ.get('TESLA_PRECONNECT_CENTRAL', S / 'src/modules/tesla_vehicle/tesla_nimble_central.cpp'))
    with tempfile.TemporaryDirectory(prefix='real-preconnect-', dir=os.environ['TMPDIR']) as td:
        d = Path(td); src = d / 'test.cpp'
        body = '\n'.join(function(GAP, n) for n in GAP_NAMES) + '\n' + '\n'.join(function(CONN, n) for n in CONN_NAMES)
        src.write_text(HEAD + body + FOOT)
        flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-Wno-class-memaccess', '-Wno-unused-function',
                 '-Wno-unused-parameter', '-Wno-unused-variable', '-Wno-unused-but-set-variable', '-Wno-missing-field-initializers', '-Wno-unused-label',
                 '-pthread', '-DTESLA_NIMBLE_HOST_TEST=1', '-DTESLA_BLE_USE_NIMBLE=1', '-DTESLA_NIMBLE_REAL_GAP=1',
                 '-I' + str(T / 'host'), '-I' + str(central.parent)]
        if 'sanitize' in sys.argv: flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-g', '-no-pie']
        subprocess.run(['g++', *flags, str(src), str(central), '-o', str(d / 'test')], check=True)
        r = subprocess.run([str(d / 'test'), *cases], timeout=60)
        if r.returncode:
            sys.exit(r.returncode)
    print(json.dumps({'status': 'PASS', 'sdk_sources': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (GAP, CONN)},
                      'real_function_bodies': GAP_NAMES + CONN_NAMES, 'cases': cases, 'rounds': 3,
                      'boundaries': 'controller HCI wire, host lock/timer, conn list storage, addr derivation, GATT/L2CAP/SM broken hooks, deferred queue bookkeeping'}, sort_keys=True))
