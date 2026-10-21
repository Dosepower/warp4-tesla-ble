"""Actual SDK NPL allocator-edge functions, controlled RTOS heap only."""
from pathlib import Path
import os,re,subprocess,resource,json,hashlib
R=Path(os.environ['TESLA_NIMBLE_SDK_ROOT'])/'esp-idf-secure';O=Path(os.environ['TESLA_CLEAN_TEST_OUT']);O.mkdir(parents=True,exist_ok=True)
p=R/'components/bt/host/nimble/nimble/porting/npl/freertos/src/npl_os_freertos.c';t=p.read_text()
def extract(n):
 m=re.search(r'(?m)^(?:static )?(?:int|void)\s*\n?'+n+r'\([^;]*?\)\s*\{',t);assert m
 i=t.index('{',m.start())+1;d=1
 while d:d+=(t[i]=='{')-(t[i]=='}');i+=1
 return t[m.start():i]
sync=extract('npl_eventq_sync_init');queue=extract('npl_freertos_eventq_init');callout=extract('npl_freertos_callout_init')
context=extract('ble_freertos_ensure_ctx');mempool=extract('npl_freertos_mempool_init');funcs=extract('npl_freertos_funcs_init')
osalfile=R/'components/bt/common/osal/src/bt_osal_freertos.c';osalt=osalfile.read_text();saved=t;t=osalt;osal=extract('bt_osal_freertos_funcs_init');t=saved
pre=r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define BLE_LL_ASSERT(x) assert(x)
#define OS_MEM_ALLOC 1
#define CONFIG_BT_NIMBLE_USE_ESP_TIMER 1
#define BLE_TOTAL_EV_COUNT 100
#define ESP_OK 0
#define MYNEWT_VAL(x) CFG_##x
#define CFG_BLE_STATIC_TO_DYNAMIC 1
#define CFG_MP_RUNTIME_ALLOC 0
#define SOC_ESP_NIMBLE_CONTROLLER 0
#define CONFIG_BT_CONTROLLER_ENABLED 1
#define BLE_HS_ENOMEM 6
#define BLE_TOTAL_EVQ_COUNT 10
#define BLE_TOTAL_CO_COUNT 40
#define BLE_TOTAL_SEM_COUNT 10
#define BLE_TOTAL_MUTEX_COUNT 10
#define OS_MEMPOOL_SIZE(n,s) ((n)*(s))
typedef int os_membuf_t;
struct ble_npl_event {int dummy;};
struct ble_npl_eventq {void *eventq;};
struct ble_npl_eventq_freertos {void *q;};
struct ble_npl_callout {void *co;};
struct ble_npl_callout_freertos {struct ble_npl_event ev;void *handle;void *evq;};
typedef void ble_npl_event_fn(struct ble_npl_event *);
typedef struct {void (*callback)(void *);void *arg;const char *name;} esp_timer_create_args_t;
static void *npl_eventq_sync;
struct ble_npl_event_freertos{int v;};struct ble_npl_sem_freertos{int v;};struct ble_npl_mutex_freertos{int v;};
struct pools{int ev_pool,evq_pool,co_pool,sem_pool,mutex_pool;int ev_buf[100];void *evq_buf,*co_buf,*sem_buf,*mutex_buf;};static struct pools *ble_freertos_ctx;
#define ble_freertos_ev_pool (ble_freertos_ctx->ev_pool)
#define ble_freertos_evq_pool (ble_freertos_ctx->evq_pool)
#define ble_freertos_co_pool (ble_freertos_ctx->co_pool)
#define ble_freertos_sem_pool (ble_freertos_ctx->sem_pool)
#define ble_freertos_mutex_pool (ble_freertos_ctx->mutex_pool)
#define ble_freertos_ev_buf (ble_freertos_ctx->ev_buf)
#define ble_freertos_evq_buf (ble_freertos_ctx->evq_buf)
#define ble_freertos_co_buf (ble_freertos_ctx->co_buf)
#define ble_freertos_sem_buf (ble_freertos_ctx->sem_buf)
#define ble_freertos_mutex_buf (ble_freertos_ctx->mutex_buf)
struct npl_funcs_t{int v;};static struct npl_funcs_t *npl_funcs;static const struct npl_funcs_t npl_funcs_ro={1};
struct bt_osal_funcs_t{int v;};static struct bt_osal_funcs_t *bt_osal_funcs;static const struct bt_osal_funcs_t bt_osal_funcs_ro={1};
static int fail,allocs;
static void *rtos_heap(void){allocs++;return fail==allocs?NULL:malloc(16);}
static void *nimble_platform_mem_calloc(size_t n,size_t z){allocs++;return fail==allocs?NULL:calloc(n,z);}
static void nimble_platform_mem_free(void *p){free(p);}
static void *bt_osal_malloc(size_t n){return nimble_platform_mem_calloc(1,n);}
static void vSemaphoreDelete(void *p){free(p);}
static int os_mempool_init(void *p,int n,int z,void *b,const char*s){(void)p;(void)n;(void)z;(void)b;(void)s;return 0;}
static void os_mempool_unregister(void *p){(void)p;}
static void *xSemaphoreCreateRecursiveMutex(void){return rtos_heap();}
static void *xQueueCreate(int a,size_t b){(void)a;(void)b;return rtos_heap();}
static void *os_memblock_get(void *pool){(void)pool;return calloc(1,128);}
static int os_memblock_put(void *p,void *v){(void)p;free(v);return 0;}
static void ble_npl_event_init(struct ble_npl_event *p,ble_npl_event_fn*f,void*a){(void)p;(void)f;(void)a;}
static void ble_npl_event_deinit(struct ble_npl_event *p){(void)p;}
static void ble_npl_event_fn_wrapper(void *p){(void)p;}
static int esp_timer_create(const esp_timer_create_args_t *a,void **p){(void)a;*p=rtos_heap();return *p?0:-1;}
'''
main=r'''
static void cb(struct ble_npl_event *p){(void)p;}
int main(int argc,char **argv){assert(argc==3);int which=atoi(argv[1]);fail=atoi(argv[2]);
 if(which==1||which==2){int saved=fail;fail=0;assert(!ble_freertos_ensure_ctx());allocs=0;fail=saved;}
 if(which==0){int rc=CALL_SYNC;assert((rc!=0)==(fail!=0));assert((npl_eventq_sync==NULL)==(fail!=0));free(npl_eventq_sync);}
 if(which==1){struct ble_npl_eventq q={0};npl_freertos_eventq_init(&q);assert((q.eventq==NULL)==(fail!=0));if(q.eventq){free(((struct ble_npl_eventq_freertos*)q.eventq)->q);free(q.eventq);}}
 if(which==2){struct ble_npl_callout c={0};int rc=npl_freertos_callout_init(&c,NULL,cb,NULL);assert((rc!=0)==(fail!=0));assert((c.co==NULL)==(fail!=0));if(c.co){free(((struct ble_npl_callout_freertos*)c.co)->handle);free(c.co);}}
 if(which==3){int rc=npl_freertos_mempool_init();assert((rc!=0)==(fail!=0));}
 if(which==4){npl_freertos_funcs_init();assert((npl_funcs==NULL)==(fail!=0));free(npl_funcs);}
 if(which==5){bt_osal_freertos_funcs_init();assert((bt_osal_funcs==NULL)==(fail!=0));free(bt_osal_funcs);}
 if(ble_freertos_ctx){free(ble_freertos_evq_buf);free(ble_freertos_co_buf);free(ble_freertos_sem_buf);free(ble_freertos_mutex_buf);free(ble_freertos_ctx);}
 if(which==3)free(npl_eventq_sync);
 puts("actual SDK NPL RTOS allocation edge PASS");return 0;
}
'''
if sync.startswith('static int'):main=main.replace('CALL_SYNC','npl_eventq_sync_init()')
else:main=main.replace('CALL_SYNC','(npl_eventq_sync_init(),0)')
f=O/'actual-sdk-npl.c';f.write_text(pre+context+'\n'+sync+'\n'+queue+'\n'+callout+'\n'+mempool+'\n'+funcs+'\n'+osal+main);exe=O/'actual-sdk-npl'
flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']if os.getenv('TESLA_PERMANENT_SANITIZE')else[]
subprocess.run(['gcc','-std=c11','-Wall','-Wextra',*flags,str(f),'-o',str(exe)],check=True)
rows=[]
for kind in range(6):
 for failure in range(7 if kind==3 else 2):
  r=subprocess.run([str(exe),str(kind),str(failure)],capture_output=True,text=True,timeout=5,preexec_fn=lambda:resource.setrlimit(resource.RLIMIT_CORE,(0,0)))
  rows.append({'kind':kind,'failure':failure,'exit':r.returncode,'output':r.stdout+r.stderr})
(O/'actual-sdk-npl-results.json').write_text(json.dumps({'source':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'osal_source':str(osalfile),'osal_sha256':hashlib.sha256(osalfile.read_bytes()).hexdigest(),'sanitize':bool(flags),'rows':rows},indent=2))
print(json.dumps(rows,indent=2));assert all(row['exit']==0 for row in rows)
