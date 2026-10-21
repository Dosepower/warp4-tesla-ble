"""Compile exact SDK init function bodies; only OS/controller/dependency boundaries are controlled.
No firmware API is mocked for the actual ble_hs init/stop/HCI/flow -> port error chain.
Every process injects one allocation failure; the retained quarantine is freed ONLY by
this test process on exit, never by pretending SDK partial deinit is safe.
"""
from pathlib import Path
import os,subprocess,json,re,hashlib,resource
S=Path(__file__).resolve().parents[2];R=Path(os.environ['TESLA_NIMBLE_SDK_ROOT'])/'esp-idf-secure';O=Path(os.environ['TESLA_CLEAN_TEST_OUT']);O.mkdir(parents=True,exist_ok=True)
B=R/'components/bt/host/nimble';sources={};bodies=[]
def extract(rel,n):
 p=B/rel;t=p.read_text();m=re.search(r'(?m)^(?:static )?(?:int|void|esp_err_t)\s*\n?'+n+r'\([^;]*?\)\s*\{',t);assert m,n
 i=t.index('{',m.start())+1;d=1
 while d:d+=(t[i]=='{')-(t[i]=='}');i+=1
 sources[n]={'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()};return t[m.start():i]
stop=extract('nimble/nimble/host/src/ble_hs_stop.c','ble_hs_stop_init');hci=extract('nimble/nimble/host/src/ble_hs_hci.c','ble_hs_hci_init');flow=extract('nimble/nimble/host/src/ble_hs_flow.c','ble_hs_flow_init');hs=extract('nimble/nimble/host/src/ble_hs.c','ble_hs_init')
hciport=extract('esp-hci/src/esp_nimble_hci.c','esp_nimble_hci_init')
portfile='nimble/porting/nimble/src/nimble_port.c'
port=[extract(portfile,n)for n in ('esp_nimble_init','esp_nimble_deinit','nimble_port_init','nimble_port_run')]
pre=r'''
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
static size_t nth,allocs; static void *owned[1000]; static unsigned owned_n;
static int starts,full_deinits,queued;
static void *inject(size_t n,size_t z){++allocs;if(nth==allocs)return NULL;void *p=calloc(n,z);assert(p&&owned_n<1000);owned[owned_n++]=p;return p;}
static void release(void *p){if(!p)return;for(unsigned i=0;i<owned_n;i++)if(owned[i]==p){owned[i]=NULL;free(p);return;}assert(0);}
#define nimble_platform_mem_calloc inject
#define nimble_platform_mem_free release
#define MYNEWT_VAL(x) CFG_##x
#define CFG_BLE_STATIC_TO_DYNAMIC 1
#define CFG_BLE_HS_FLOW_CTRL 1
#define CFG_BLE_HS_DEBUG 0
#define CFG_BLE_PERIODIC_ADV 0
#define CFG_BLE_GATTS 0
#define CFG_BLE_GATTC 1
#define CFG_BLE_GATT_CACHING 0
#define CFG_BLE_HS_AUTO_START 1
#define CFG_BLE_QUEUE_CONG_CHECK 0
#define CFG_BLE_MONITOR_RTT 0
#define NIMBLE_BLE_CONNECT 1
#define CONFIG_BT_NIMBLE_ENABLED 1
#define CONFIG_BT_CONTROLLER_ENABLED 1
#define SOC_ESP_NIMBLE_CONTROLLER 0
#define CONFIG_BT_PRF_TASK_ENABLED 0
#ifndef CONFIG_BT_OSAL_ENABLED
#define CONFIG_BT_OSAL_ENABLED 1
#endif
#define CONFIG_ENABLE_BT_TESTING 0
#define BT_HCI_LOG_INCLUDED 0
#define TRUE 1
#define CONFIG_IDF_TARGET_ESP32 1
#define ESP_BT_MODE_CLASSIC_BT 0
#define ESP_BT_MODE_BLE 1
typedef int esp_bt_controller_config_t;
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() 0
static int esp_bt_controller_mem_release(int mode){(void)mode;return 0;}
static int esp_bt_controller_init(int *cfg){(void)cfg;return 0;}
static int esp_bt_controller_enable(int mode){(void)mode;return 0;}
static int esp_bt_controller_disable(void){return 0;}
static int esp_bt_controller_deinit(void){full_deinits++;return 0;}
struct ble_npl_sem{int v;};static struct ble_npl_sem ble_hs_stop_sem;
static void ble_npl_sem_release(struct ble_npl_sem *p){(void)p;}
static void ble_npl_event_deinit(void *p){(void)p;}
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define BLE_HS_ENOMEM 6
#define BLE_HS_EINVAL 3
#define BLE_HS_ENOENT 5
#define BLE_HS_ENABLED_STATE_OFF 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define MODLOG_DFLT(...) ((void)0)
#define SYSINIT_ASSERT_ACTIVE() ((void)0)
#define SYSINIT_PANIC_ASSERT(x) assert(x)
#define BLE_HS_DBG_ASSERT_EVAL(x) assert(x)
#define BLE_HS_DBG_ASSERT(x) assert(x)
#define BLE_HS_HCI_EVT_COUNT 10
#define OS_MEMPOOL_SIZE(a,b) ((a)*(b))
#define BLE_HS_HCI_FRAG_MEMPOOL_SIZE 100
#define BLE_HS_HCI_FRAG_MEMBLOCK_SIZE 100
#define stats_init_and_reg(...) 0
#define STATS_HDR(x) 0
#define STATS_SIZE_INIT_PARMS(...) 0
#define STATS_NAME_INIT_PARMS(...) 0
typedef int esp_err_t;typedef int os_membuf_t;
struct ble_npl_event{int v;};struct ble_npl_eventq {void *eventq;};
struct ble_hs_ctx_t{os_membuf_t *hci_os_event_buf;void *parent_task;};
static struct ble_hs_ctx_t *ble_hs_ctx;
struct ble_hs_state_ctx_t{int v;};static struct ble_hs_state_ctx_t *ble_hs_state_ctx;
static int ble_hs_hci_ev_pool,ble_hs_reset_reason,ble_hs_enabled_state,ble_hs_mutex,ble_hs_rx_q;
static struct ble_npl_event ble_hs_ev_reset,ble_hs_ev_start_stage1,ble_hs_ev_start_stage2;
static os_membuf_t *ble_hs_hci_os_event_buf;
static int ble_hs_initialized;
struct ble_hs_stop_ctx{int terminate_tmo;};static struct ble_hs_stop_ctx *ble_hs_stop_ctx;
#define ble_hs_stop_terminate_tmo (ble_hs_stop_ctx->terminate_tmo)
struct hci_ctx{int dummy;};static struct hci_ctx *ble_hs_hci_ctx;
static void *ble_hs_hci_frag_data;static int ble_hs_hci_sem,ble_hs_hci_mutex,ble_hs_hci_frag_mempool,ble_hs_hci_frag_mbuf_pool,ble_hs_hci_initialized;
struct flow_ctx{int dummy;};static struct flow_ctx *ble_hs_flow_ctx;
static struct ble_npl_event ble_hs_flow_ev;static int ble_hs_flow_timer;
struct portctx{struct ble_npl_eventq eventq;};static struct portctx *ble_npl_ctx;
#define g_eventq_dflt (ble_npl_ctx->eventq)
static bool ble_npl_deiniting,nimble_init_quarantined,nimble_host_ready,nimble_port_init_failed,nimble_port_run_active;
static int dependency(void){return inject(1,16)?0:BLE_HS_ENOMEM;}
static void ble_npl_reset_deinit_flag(void){ble_npl_deiniting=false;}
static int ble_npl_ensure_ctx(void){ble_npl_ctx=inject(1,sizeof(*ble_npl_ctx));return ble_npl_ctx?0:6;}
static void os_mempool_module_init(void){}
static void *npl_table;
static void npl_freertos_funcs_init(void){npl_table=inject(1,16);}
static void *npl_freertos_funcs_get(void){return npl_table;}
static void npl_freertos_funcs_deinit(void){full_deinits++;}
static int npl_freertos_mempool_init(void){return dependency();}
static void npl_freertos_mempool_deinit(void){full_deinits++;}
static int vhci_host_cb,vhci_registered;static void *vhci_send_sem;
static int ble_buf_alloc(void){return dependency()?ESP_ERR_NO_MEM:0;}
static void ble_buf_free(void){full_deinits++;}
static int esp_vhci_host_register_callback(void *p){(void)p;vhci_registered=1;return 0;}
static void ble_transport_init(void){}
static void *xSemaphoreCreateBinary(void){return inject(1,16);}
static void xSemaphoreGive(void *p){(void)p;}
static int esp_nimble_hci_deinit(void){full_deinits++;return 0;}
static int esp_nimble_hci_and_controller_init(void){return dependency()?ESP_ERR_NO_MEM:0;}
static int esp_nimble_hci_and_controller_deinit(void){full_deinits++;return 0;}
static void ble_npl_eventq_init(struct ble_npl_eventq *q){q->eventq=inject(1,8);}
static void ble_npl_eventq_deinit(struct ble_npl_eventq *q){release(q->eventq);q->eventq=NULL;}
static void os_msys_init(void){}
static void *osal_table;
#if CONFIG_BT_OSAL_ENABLED
static void bt_osal_freertos_funcs_init(void){osal_table=inject(1,16);}
#else
static void bt_osal_freertos_funcs_init(void){}
#endif
static void *bt_osal_freertos_funcs_get(void){return osal_table;}
static void bt_osal_freertos_funcs_deinit(void){full_deinits++;}
static void ble_transport_ll_init(void){}
static void ble_transport_ll_deinit(void){full_deinits++;}
static void ble_transport_hs_deinit(void){full_deinits++;}
static void ble_hs_deinit(void){full_deinits++;}
static int os_mempool_init(void *a,int b,int c,void *d,const char *e){(void)a;(void)b;(void)c;(void)d;(void)e;return 0;}
static int ble_npl_mutex_init(int *p){(void)p;return dependency();}
static int ble_npl_sem_init(int *p,int n){(void)p;(void)n;return dependency();}
static int mem_init_mbuf_pool(void *a,void*b,void*c,int d,int e,const char*f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return 0;}
static void ble_npl_event_init(struct ble_npl_event *e,void(*cb)(struct ble_npl_event*),void*a){(void)e;(void)cb;(void)a;}
static int ble_npl_callout_init(int *p,struct ble_npl_eventq *q,void(*cb)(struct ble_npl_event*),void*a){(void)p;(void)q;(void)cb;(void)a;return dependency();}
static struct ble_npl_eventq *nimble_port_get_dflt_eventq(void){return &g_eventq_dflt;}
static struct ble_npl_eventq *ble_hs_evq_get(void){return &g_eventq_dflt;}
static void ble_hs_event_reset(struct ble_npl_event *p){(void)p;}
static void ble_hs_event_start_stage1(struct ble_npl_event *p){(void)p;}
static void ble_hs_event_start_stage2(struct ble_npl_event *p){(void)p;}
static void ble_hs_event_rx_data(struct ble_npl_event *p){(void)p;}
static void ble_hs_stop_terminate_timeout_cb(struct ble_npl_event *p){(void)p;}
static void ble_hs_flow_event_cb(struct ble_npl_event *p){(void)p;}
static int ble_hs_conn_init(void){return dependency();}
static int ble_l2cap_init(void){return dependency();}
static int ble_gap_init(void){return dependency();}
static int ble_att_init(void){return dependency();}
static int ble_gattc_init(void){return dependency();}
static void ble_mqueue_init(int *p,void(*cb)(struct ble_npl_event*),void*a){(void)p;(void)cb;(void)a;}
static void ble_hs_evq_set(struct ble_npl_eventq *q){(void)q;}
static void ble_npl_eventq_put(struct ble_npl_eventq *q,struct ble_npl_event *e){(void)q;(void)e;queued++;}
#define BLE_NPL_TIME_FOREVER 0
static struct ble_npl_event *ble_npl_eventq_get(struct ble_npl_eventq *q,int t){(void)q;(void)t;starts++;return NULL;}
static void ble_npl_event_run(struct ble_npl_event *e){(void)e;}
static struct ble_npl_event ble_hs_ev_stop;
'''
# Old SDK void return is deliberately modelled exactly by its old transport caller.
def wrapper(n,body):return f'static int call_{n}(void){{'+(f'return {n}();' if body.startswith('int') else f'{n}();return 0;')+'}\n'
# Return-type adapters are only used for old SDK regression reproduction.
if hs.startswith('void'):pre+='static void ble_hs_hci_init(void);static void ble_hs_stop_init(void);static void ble_hs_flow_init(void);\n'
else:pre+='static int ble_hs_hci_init(void);static int ble_hs_stop_init(void);static int ble_hs_flow_init(void);\n'
# Exact functions are externally defined, so avoid mismatching static declarations.
pre=pre.replace('static int ble_hs_hci_init','int ble_hs_hci_init').replace('static int ble_hs_stop_init','int ble_hs_stop_init').replace('static int ble_hs_flow_init','int ble_hs_flow_init').replace('static void ble_hs_hci_init','void ble_hs_hci_init').replace('static void ble_hs_stop_init','void ble_hs_stop_init').replace('static void ble_hs_flow_init','void ble_hs_flow_init')
transport='void ble_transport_hs_init(void){ble_hs_init();}\n'
main=r'''
int main(int argc,char **argv){nth=argc>1?strtoul(argv[1],NULL,10):0;
 int rc=nimble_port_init();
 if(nth){
  if(vhci_registered&&full_deinits){return 26;}
  if(!rc){fprintf(stderr,"MASKED failure allocation %zu of %zu\\n",nth,allocs);return 21;}
  if(nimble_host_ready){return 22;}
  int before=full_deinits;size_t n=allocs;
  if(nimble_port_init()==0||allocs!=n){return 23;}
  if(esp_nimble_deinit()==0||full_deinits!=before){return 24;}
  nimble_port_run();if(starts||queued){return 25;}
 }else{assert(rc==0&&nimble_host_ready&&queued==1);}
 printf("allocation=%zu total=%zu rc=%d quarantine=%d full_deinits=%d\\n",nth,allocs,rc,nimble_init_quarantined,full_deinits);
 for(unsigned i=0;i<owned_n;i++){free(owned[i]);}return 0;
}
'''
# Put declarations in the same order as the real chain and extract port functions unmodified.
if 'nimble_host_ready = true' not in port[0]:main=main.replace('rc==0&&nimble_host_ready&&queued==1','rc==0&&queued==1')
code=pre+stop+'\n'+hci+'\n'+flow+'\n'+hs+'\n'+transport+'\n'+hciport+'\n'+'\n'.join(port)+main
p=O/'actual-sdk-init.c';p.write_text(code);exe=O/'actual-sdk-init'
flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']if os.getenv('TESLA_PERMANENT_SANITIZE')else[]
if os.getenv('TESLA_SDK_INIT_OSAL_DISABLED'):flags+=['-DCONFIG_BT_OSAL_ENABLED=0']
subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-variable',*flags,str(p),'-o',str(exe)],check=True)
rows=[]
def run(n):
 r=subprocess.run([str(exe),str(n)],capture_output=True,text=True,timeout=10,preexec_fn=lambda:resource.setrlimit(resource.RLIMIT_CORE,(0,0)));row={'allocation':n,'exit':r.returncode,'output':r.stdout+r.stderr};rows.append(row);return row
normal=run(0);assert normal['exit']==0,normal
count=int(re.search(r'total=(\d+)',normal['output'])[1]);bad=[]
for n in range(1,count+1):
 row=run(n)
 if row['exit']:bad.append(row)
(O/'actual-sdk-init-results.json').write_text(json.dumps({'actual_source_functions':sources,'controlled_boundaries':'OS/controller + unchanged dependency initializers, not substituted SDK chain','sanitize':bool(os.getenv('TESLA_PERMANENT_SANITIZE')),'osal_enabled':not bool(os.getenv('TESLA_SDK_INIT_OSAL_DISABLED')),'allocations':count,'rows':rows},indent=2))
print(json.dumps({'allocations':count,'passed':len(rows)-len(bad),'failed':len(bad),'failures':bad},indent=2));assert not bad
