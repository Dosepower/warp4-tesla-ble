#!/usr/bin/env python3
"""Target ABI + real pinned BTC/init control-flow memory barrier check.

No device access. Native dependencies are explicit test doubles; the heap is an
optimistic capacity bound, not reconstructed hardware fragmentation or TLSF.
Only BTC creation and ESP_FAIL propagation execute here, not asynchronous host
initialization. A successful native control path is NOT Bluetooth success.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def function(source, name):
    definitions = list(re.finditer(r"(?m)^[\w *]+\b" + re.escape(name) + r"\([^;]*?\)\s*\{", source))
    if len(definitions) != 1:
        raise ValueError(f"expected one definition: {name}, found {len(definitions)}")
    start = definitions[0].start()
    opening = source.index("{", start)
    depth = 0
    # These pinned bodies have no brace literals/comments. Fail closed rather
    # than silently accepting another source shape in a future SDK.
    body = source[opening:]
    for pos, char in enumerate(body):
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:opening + pos + 1]
    raise ValueError(f"unterminated function: {name}")


def target_abi(builder, directory):
    commands = json.loads((builder / "build/compile_commands.json").read_text())
    probes = {
        "common/osi/thread.c": {"thread": "sizeof(osi_thread_t)", "work_queue": "sizeof(struct work_queue)",
                               "work_item": "sizeof(struct work_item)", "pointer": "sizeof(void *)"},
        "FreeRTOS-Kernel/queue.c": {"queue": "sizeof(Queue_t)"},
        "FreeRTOS-Kernel/tasks.c": {"tcb": "sizeof(TCB_t)", "stack_element": "sizeof(StackType_t)"},
        "config/stack_config.c": {"config": "sizeof(struct stack_config_env_tag)"},
        "common/btc/core/btc_task.c": {"btc_stack": "BTC_TASK_STACK_SIZE", "btc_cb": "sizeof(btc_dm_cb_t)",
            "btc_profile_table": "sizeof(void *) * BTC_PID_NUM", "btc_adv": "sizeof(tBTA_BLE_ADV_DATA)",
            "btc_create_table": "sizeof(esp_btc_creat_tab_t)"},
        "stack/btu/btu_init.c": {"btu_stack": "BTU_TASK_STACK_SIZE"},
        "hci/hci_layer.c": {"hci_stack": "HCI_HOST_TASK_STACK_SIZE"},
    }
    result = {}
    for index, (suffix, expressions) in enumerate(probes.items()):
        matches = [c for c in commands if c["file"].endswith(suffix)]
        if len(matches) != 1:
            raise ValueError(f"ambiguous/missing target compilation: {suffix}")
        entry = matches[0]
        args = shlex.split(entry["command"])
        compiler = Path(args[0])
        output = directory / f"abi-{index}.o"
        source = directory / f"abi-{index}.c"
        source.write_text('#include ' + json.dumps(entry["file"]) + '\n' + '\n'.join(
            f"char memory_abi_{name}[{expr}];" for name, expr in expressions.items()) + '\n')
        # The sealed export is authoritative; builder configuration headers may
        # have been cleaned after export. Never reconstruct sdkconfig defines.
        exported_config = builder / "out/tools/esp32-arduino-libs/esp32/qio_qspi/include"
        if not (exported_config / "sdkconfig.h").is_file():
            raise ValueError("missing sealed exported SDK configuration")
        filtered = [args[0], "-I" + str(exported_config)]
        skip = False
        for arg in args[1:]:
            if skip:
                skip = False
                continue
            if arg in ("-o", "-MT", "-MF"):
                skip = True
            elif arg not in (entry["file"], "-c", "-MD", "-MMD"):
                filtered.append(arg)
        subprocess.run(filtered + ["-c", str(source), "-o", str(output)], cwd=entry["directory"],
                       check=True, capture_output=True, text=True)
        nm = compiler.with_name(compiler.name.removesuffix("gcc") + "nm")
        symbols = subprocess.check_output([str(nm), "-S", "--defined-only", str(output)], text=True)
        for name in expressions:
            match = re.search(r"^\w+\s+(\w+)\s+\w\s+memory_abi_" + name + "$", symbols, re.M)
            if not match:
                raise ValueError(f"missing ABI symbol: {name}")
            result[name] = int(match[1], 16)
    return result


NATIVE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TRUE 1
#define FALSE 0
#define SC_MODE_INCLUDED FALSE
#define BT_CONTROLLER_INCLUDED TRUE
#define HEAP_MEMORY_DEBUG 0
#define HEAP_MEMORY_STATS 0
#define BTC_DYNAMIC_MEMORY TRUE
#define BLE_INCLUDED TRUE
#define BT_HCI_LOG_INCLUDED FALSE
#define CONFIG_BT_PRF_TASK_ENABLED 0
#define CONFIG_BT_BLUEDROID_TASK_STACK_IN_EXT_MEM 0
#define LOG_ERROR(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define BTC_TASK_NAME "BTC_TASK"
#define BTC_TASK_PRIO 10
#define BTC_TASK_PINNED_TO_CORE 0
#define BTC_TASK_WORKQUEUE_NUM 2
#define BTC_TASK_WORKQUEUE0_LEN 0
#define BTC_TASK_WORKQUEUE1_LEN 5
#define OSI_THREAD_CORE_0 0
#define OSI_THREAD_CORE_AFFINITY 2
#define OSI_SEM_MAX_TIMEOUT UINT32_MAX
#define pdPASS 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_BT_CONTROLLER_STATUS_ENABLED 2
#define ESP_BLUEDROID_STATUS_UNINITIALIZED 0
#define ESP_BLUEDROID_STATUS_INITIALIZED 1
#define BT_STATUS_SUCCESS 0
#define BT_STATUS_NOMEM 3
#define BTC_MAIN_INIT_FUTURE 0
#define BTC_SIG_API_CALL 0
#define BTC_PID_MAIN_INIT 0
#define BTC_MAIN_ACT_INIT 0
#define FUTURE_FAIL 0
#define BT_BLUEDROID_INIT_CONFIG_DEFAULT() {true, false}
typedef int esp_err_t;
typedef int bt_status_t;
typedef int osi_thread_core_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *osi_sem_t;
typedef void (*osi_thread_func_t)(void *);
typedef struct osi_thread osi_thread_t;
typedef struct { bool ssp_en, sc_en; } esp_bluedroid_config_t;
typedef struct { int sig, pid, act; } btc_msg_t;
typedef struct { int marker; } future_t;
static int s_bt_host_state;
static const size_t DEFAULT_WORK_QUEUE_CAPACITY = 100;
static size_t BTC_TASK_STACK_SIZE, budget, largest, used, live, stack_seen;
static int mode;
static void *event_objects[3], *config_object;
static future_t future_storage, *future_pointer;
static osi_thread_t *btc_thread;
struct allocation { void *p; size_t charge; };
static struct allocation allocations[64];
static void *allocate(size_t host_bytes, size_t target_bytes) {
    if (target_bytes > largest || target_bytes > budget - used) return NULL;
    void *p = calloc(1, host_bytes); assert(p);
    for (size_t i=0; i<64; ++i) if (!allocations[i].p) {
        allocations[i]=(struct allocation){p,target_bytes}; used+=target_bytes; live++; return p;
    }
    abort();
}
static void osi_free(void *p) {
    if (!p) return;
    for (size_t i=0;i<64;++i) if (allocations[i].p==p) {
        used-=allocations[i].charge; live--; allocations[i].p=NULL; free(p); return;
    }
    abort();
}
static void *osi_calloc(size_t n);
static void *osi_malloc(size_t n);
static QueueHandle_t xQueueCreate(size_t capacity, size_t item);
static void vQueueDelete(QueueHandle_t q) { osi_free(q); }
static int osi_sem_new(osi_sem_t *s, unsigned maximum, unsigned initial) {
    assert(maximum==1 && initial==0); *s=allocate(1,ABI_queue); return *s ? 0 : -1;
}
static int osi_sem_take(osi_sem_t *s, uint32_t timeout) { assert(*s && timeout==UINT32_MAX); return 0; }
static void osi_sem_free(osi_sem_t *s) { osi_free(*s); *s=NULL; }
static void osi_thread_run(void *context) { (void)context; }
static int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, size_t stack, void *context,
                                  int priority, TaskHandle_t *task, int core) {
    (void)fn; (void)context; (void)priority; (void)core;
    assert(!strcmp(name,"BTC_TASK")); stack_seen=stack;
    // Pinned Xtensa branch allocates stack first with INTERNAL|8BIT, then TCB.
    void *s=allocate(stack,stack);
    if (!s) return 0;
    void *t=allocate(ABI_tcb,ABI_tcb);
    if (!t) { osi_free(s); return 0; }
    void **pair=calloc(2,sizeof(void *)); assert(pair); pair[0]=s; pair[1]=t; *task=pair; return pdPASS;
}
static void vTaskDelete(TaskHandle_t task) {
    if (task) { void **pair=task; osi_free(pair[0]); osi_free(pair[1]); free(pair); }
}
/* Real OSI thread types and create/delete bodies inserted here. */
@THREAD@
static void *osi_calloc(size_t n) {
    size_t charge;
    if (n==sizeof(osi_thread_t)) charge=ABI_thread;
    else { assert(n==sizeof(struct work_queue *)*2); charge=ABI_pointer*2; }
    return allocate(n,charge);
}
static void *osi_malloc(size_t n) { assert(n==sizeof(struct work_queue)); return allocate(n,ABI_work_queue); }
static QueueHandle_t xQueueCreate(size_t capacity, size_t item) {
    assert(item==sizeof(struct work_item)); return allocate(1,ABI_queue+capacity*ABI_work_item);
}
static int osi_thread_event_init(void) {
    if (mode==2) return -1;
    event_objects[0]=allocate(1,ABI_queue);
    event_objects[1]=allocate(1,16); event_objects[2]=allocate(1,16);
    assert(event_objects[0] && event_objects[1] && event_objects[2]); return 0;
}
static void osi_thread_event_deinit(void) {
    for (size_t i=0;i<3;++i) { osi_free(event_objects[i]); event_objects[i]=NULL; }
}
static void osi_thread_free(osi_thread_t *thread) {
    if (!thread) return;
    for (size_t i=0;i<thread->work_queue_num;++i) osi_work_queue_delete(thread->work_queues[i]);
    osi_free(thread->work_queues); osi_sem_free(&thread->work_sem); osi_sem_free(&thread->stop_sem);
    vTaskDelete(thread->thread_handle); osi_free(thread);
}
static bt_status_t btc_init_mem(void) { return mode==3 ? BT_STATUS_NOMEM : BT_STATUS_SUCCESS; }
static void btc_gap_callback_init(void) {}
static void btc_gap_ble_init(void) {}
@BTC@
static int esp_bt_controller_get_status(void) { return ESP_BT_CONTROLLER_STATUS_ENABLED; }
static bt_status_t bluedroid_config_init(esp_bluedroid_config_t *cfg) {
    (void)cfg; if(mode==1) return BT_STATUS_NOMEM;
    config_object=allocate(1,ABI_config); return config_object ? BT_STATUS_SUCCESS : BT_STATUS_NOMEM;
}
static void bluedroid_config_deinit(void) { osi_free(config_object); config_object=NULL; }
static future_t **btc_main_get_future_p(int id) { assert(id==0); return &future_pointer; }
static future_t *future_new(void) { return mode==4 ? NULL : &future_storage; }
static void future_free(future_t *f) { assert(f==&future_storage); }
static int future_await(future_t *f) { assert(f==&future_storage); return mode==6 ? FUTURE_FAIL : 1; }
static int btc_transfer_context(btc_msg_t *msg, void *arg, size_t length, void *copy, void *free_fn) {
    assert(msg->sig==0 && !arg && !length && !copy && !free_fn); return mode==5 ? BT_STATUS_NOMEM : BT_STATUS_SUCCESS;
}
static void btc_cleanup_partial_init(void) {}
static void btc_deinit(void) { osi_thread_free(btc_thread); btc_thread=NULL; osi_thread_event_deinit(); }
static void bt_osal_freertos_funcs_init(void) {}
esp_err_t esp_bluedroid_init_with_cfg(esp_bluedroid_config_t *cfg);
@API@
int main(int argc, char **argv) {
    assert(argc==5); budget=strtoul(argv[1],NULL,0); largest=strtoul(argv[2],NULL,0);
    BTC_TASK_STACK_SIZE=strtoul(argv[3],NULL,0); mode=atoi(argv[4]);
    int rc=esp_bluedroid_init();
    if (mode==0) { assert(rc==ESP_FAIL && stack_seen==BTC_TASK_STACK_SIZE); }
    else { assert(rc==(mode==4 ? ESP_ERR_NO_MEM : ESP_FAIL)); }
    assert(s_bt_host_state==ESP_BLUEDROID_STATUS_UNINITIALIZED && used==0 && live==0);
    printf("rc=%d stack_request=%zu live_after_failure=%zu\n",rc,stack_seen,live);
    return 0;
}
'''


def check_policy(idf, sanitize=False):
    source = Path(idf) / "components/heap/heap_caps.c"
    text = source.read_text()
    actual = '\n'.join(function(text, name) for name in
                       ("heap_caps_malloc_extmem_enable", "heap_caps_malloc_default", "heap_caps_realloc_default"))
    native = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#define HEAP_IRAM_ATTR
#define MALLOC_CAP_DEFAULT 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_SPIRAM 4
#define MALLOC_DISABLE_EXTERNAL_ALLOCS -1
static int malloc_alwaysinternal_limit=-1;
static unsigned caps_seen[4], calls;
static bool external_missing;
static char block;
static void *heap_caps_malloc_base(size_t n, unsigned caps) {
    assert(n); caps_seen[calls++]=caps;
    return external_missing && (caps & MALLOC_CAP_SPIRAM) ? NULL : &block;
}
static void *heap_caps_malloc(size_t n, unsigned caps) { return heap_caps_malloc_base(n,caps); }
static void *heap_caps_realloc_base(void *p, size_t n, unsigned caps) {
    assert(p==&block); return heap_caps_malloc_base(n,caps);
}
static void *heap_caps_realloc(void *p, size_t n, unsigned caps) { return heap_caps_realloc_base(p,n,caps); }
static void heap_caps_alloc_failed(size_t n, unsigned caps, const char *fn) { (void)n; (void)caps; (void)fn; assert(false); }
@POLICY@
int main(void) {
    const size_t sizes[]={12,24,44,92,216,352,740,892,2048,8704};
    heap_caps_malloc_extmem_enable(2048);
    for(size_t i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        calls=0; assert(heap_caps_malloc_default(sizes[i])==&block && calls==1);
        assert(caps_seen[0]==(MALLOC_CAP_DEFAULT|(sizes[i]<=2048 ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM)));
    }
    heap_caps_malloc_extmem_enable(0);
    for(size_t i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        calls=0; assert(heap_caps_malloc_default(sizes[i])==&block && calls==1);
        assert(caps_seen[0]==(MALLOC_CAP_DEFAULT|MALLOC_CAP_SPIRAM));
        calls=0; assert(heap_caps_realloc_default(&block,sizes[i])==&block && calls==1);
        assert(caps_seen[0]==(MALLOC_CAP_DEFAULT|MALLOC_CAP_SPIRAM));
    }
    external_missing=true;
    calls=0; assert(heap_caps_malloc_default(12)==&block && calls==2);
    assert(caps_seen[1]==MALLOC_CAP_DEFAULT);
    calls=0; assert(heap_caps_realloc_default(&block,24)==&block && calls==2);
    assert(caps_seen[1]==MALLOC_CAP_DEFAULT);
    calls=0; assert(heap_caps_malloc(8704,MALLOC_CAP_INTERNAL)==&block && calls==1);
    assert(caps_seen[0]==MALLOC_CAP_INTERNAL); // Explicit capability requests do not use this policy.
    puts("external_preferred_internal_unchanged=true");
}
'''.replace("@POLICY@", actual)
    with tempfile.TemporaryDirectory(prefix="warp-heap-policy-") as temporary:
        root = Path(temporary)
        (root / "policy.c").write_text(native)
        flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g", "-no-pie"] if sanitize else []
        subprocess.run(["gcc", "-std=gnu17", "-Wall", "-Wextra", "-Werror", *flags,
                        str(root / "policy.c"), "-o", str(root / "policy")], check=True, capture_output=True, text=True)
        output = subprocess.check_output([str(root / "policy")], text=True).strip()
    return {"external_preferred_internal_unchanged": output == "external_preferred_internal_unchanged=true",
            "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "sanitized": sanitize}


def check(builder, sanitize=False):
    builder = Path(builder).resolve()
    idf = (builder / "esp-idf").resolve()
    sources = {"api": idf / "components/bt/host/bluedroid/api/esp_bt_main.c",
               "btc": idf / "components/bt/common/btc/core/btc_task.c",
               "thread": idf / "components/bt/common/osi/thread.c"}
    texts = {k: p.read_text() for k, p in sources.items()}
    api = function(texts["api"], "esp_bluedroid_init") + '\n' + function(texts["api"], "esp_bluedroid_init_with_cfg")
    btc = function(texts["btc"], "btc_init")
    thread = texts["thread"].split("struct work_item {", 1)[1].split("struct osi_event_core {", 1)[0]
    thread = "struct work_item {" + thread
    thread += '\n' + '\n'.join(function(texts["thread"], name) for name in
                               ("osi_work_queue_create", "osi_work_queue_delete", "osi_thread_create"))
    init = function(texts["api"], "esp_bluedroid_init_with_cfg")
    init_start = texts["api"].index(init)
    sites = {label: [texts["api"][:init_start + m.start()].count('\n') + 1
                    for m in re.finditer(r"return " + token + r";", init)]
             for label, token in (("esp_fail", "ESP_FAIL"), ("esp_err_no_mem", "ESP_ERR_NO_MEM"))}
    with tempfile.TemporaryDirectory(prefix="warp-bluedroid-memory-") as temporary:
        directory = Path(temporary)
        abi = target_abi(builder, directory)
        native = '\n'.join(f"#define ABI_{name} {value}" for name, value in abi.items()) + '\n'
        native += NATIVE.replace("@THREAD@", thread).replace("@BTC@", btc).replace("@API@", api)
        source = directory / "native.c"
        source.write_text(native)
        executable = directory / "native"
        flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g", "-no-pie"] if sanitize else []
        subprocess.run(["gcc", "-std=gnu17", "-Wall", "-Wextra", "-Werror", *flags,
                        str(source), "-o", str(executable)], check=True, capture_output=True, text=True)
        scenarios = {"observed_stack_barrier": [4756, 3444, abi["btc_stack"], 0],
                     "idf_default_btc_still_too_large": [4756, 3444, 3072 + 512, 0],
                     "config_fail": [100000, 100000, abi["btc_stack"], 1],
                     "event_init_fail": [100000, 100000, abi["btc_stack"], 2],
                     "btc_dynamic_pool_fail": [100000, 100000, abi["btc_stack"], 3],
                     "future_allocation_no_mem_not_esp_fail": [100000, 100000, abi["btc_stack"], 4],
                     "init_transfer_fail": [100000, 100000, abi["btc_stack"], 5],
                     "async_init_future_fail": [100000, 100000, abi["btc_stack"], 6]}
        outcomes = {name: subprocess.check_output([str(executable), *map(str, values)], text=True).strip()
                    for name, values in scenarios.items()}
    stacks_tcbs = abi["btc_stack"] + abi["btu_stack"] + abi["hci_stack"] + 3 * abi["tcb"]
    # BTC queues 100+5, BTU 100, HCI 1+1; five queue headers,
    # and two persistent counting semaphores in each of the three threads.
    internal_lower = stacks_tcbs + 5 * abi["queue"] + 207 * abi["work_item"] + 6 * abi["queue"]
    return {"validation": "target_compiler_ABI_and_native_pinned_control_flow_with_explicit_dependency_doubles",
            "builder": str(builder), "idf": str(idf), "abi": abi, "return_sites": sites,
            "source_sha256": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources.values()},
            "native_function_sha256": {k: hashlib.sha256(v.encode()).hexdigest() for k, v in
                                       (("api", api), ("btc", btc), ("thread", thread))},
            "stack_and_tcb_lower_bound": stacks_tcbs, "strict_internal_lower_bound": internal_lower,
            "btc_checked_dynamic_pools": abi["btc_cb"] + abi["btc_profile_table"] + 2*abi["btc_adv"] + abi["btc_create_table"],
            "native_scenarios": outcomes, "sanitized": sanitize, "hardware_success_claimed": False,
            "limitations": "No allocator metadata charged; INTERNAL snapshot is an upper bound on INTERNAL|8BIT. Heap shape, concurrent activity, asynchronous host stack and controller blob are not simulated."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--builder", type=Path, required=True)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    try:
        print(json.dumps(check(args.builder, args.sanitize), indent=2))
    except subprocess.CalledProcessError as exc:
        raise SystemExit(exc.stderr or str(exc)) from exc
