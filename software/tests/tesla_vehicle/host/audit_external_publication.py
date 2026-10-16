#!/usr/bin/env python3
"""Reproduce bounded external blockers; PASS means defect reproduced, not fixed.
Exact Config/API and SDK function bodies, native platform/type boundaries.
Framework files are read-only. Executable/artifacts go exclusively to TMPDIR.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
from bench_confinement import output_plan

repo = Path(__file__).resolve().parents[4]
fw = Path('/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder')
core = fw / 'components/arduino/cores/esp32'
out = Path(os.environ['TMPDIR']) / 'tesla-phase1-eleventh-external'
output_plan(roots=(out,)).revalidate()
out.mkdir(exist_ok=True)
def definition(text, marker):
    start = text.index(marker)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
config = repo / 'software/src/config.cpp'
api = repo / 'software/src/modules/api/api.cpp'
sdk_copy = fw / 'esp-idf/components/bt/host/bluedroid/btc/profile/std/gatt/btc_gattc.c'
sdk_transfer = fw / 'esp-idf/components/bt/common/btc/core/btc_task.c'
# Use already generated unchanged native WString platform shims.
shims = Path(os.environ['TMPDIR']) / 'warp-tesla-host-tests/target-oom'
assert (shims / 'WString.cpp').read_bytes() == (core / 'WString.cpp').read_bytes()
cpp = r'''
#include <WString.h>
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#include <ArduinoJson.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static bool watching=false;
static unsigned malloc_calls=0,realloc_calls=0,fail_malloc=0,fail_realloc=0;
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_realloc(void *,size_t);
extern "C" void *__wrap_malloc(size_t n) {if(watching&&++malloc_calls==fail_malloc)return nullptr;return __real_malloc(n);}
extern "C" void *__wrap_realloc(void *p,size_t n) {if(watching&&++realloc_calls==fail_realloc)return nullptr;return __real_realloc(p,n);}
struct Logger {template<class... T>void printfln(T...){}void print_plain(const char*,size_t){}} logger;
struct Config {
 struct to_json_result {DynamicJsonDocument doc;size_t failed_allocation_size;to_json_result():doc(512),failed_allocation_size(doc.capacity()?0:512){if(!failed_allocation_size)doc["last_error"]="Prior valid long diagnostic to force Arduino String serialization allocation";}};
 unsigned char updated=1;
 to_json_result to_json(const char *const *,size_t)const{return {};}
 String to_string_except(const char *const *,size_t)const;
 unsigned char was_updated(unsigned char mask)const{return updated&mask;}
 void clear_updated(unsigned char mask){updated&=~mask;}
};
struct StateRegistration {
 Config *config;
 const char *path="tesla_vehicle/state";
 const char *const *keys_to_censor=nullptr;
 bool get_low_latency()const{return true;}
 size_t get_keys_to_censor_len()const{return 0;}
};
struct IAPIBackend {
 enum class WantsStateUpdate {Later,No,AsString,AsConfig};
 unsigned calls=0;size_t bytes=0;bool valid_path=false;
 char received[256]{};
 WantsStateUpdate wantsStateUpdate(size_t){return WantsStateUpdate::AsString;}
 bool pushStateUpdate(size_t,const String &payload,const String &path){++calls;bytes=payload.length();assert(bytes<sizeof(received));std::memcpy(received,payload.c_str(),bytes);valid_path=path=="tesla_vehicle/state";return true;}
};
struct API {
 unsigned state_update_counter=0;
 std::array<StateRegistration,1> states;
 std::array<IAPIBackend*,1> backends;
 void push_state_updates();
};
'''
cpp += definition(config.read_text(), 'String Config::to_string_except(const char *const *keys_to_censor, size_t keys_to_censor_len) const')+'\n'
cpp += definition(api.read_text(), 'void API::push_state_updates()')+'\n'
cpp += r'''
// SDK allocator and post boundary; the copied message is captured instead of
// scheduling BTC. The production transfer/deep-copy/free bodies are unchanged.
struct btc_msg_t {unsigned sig=0,pid=0,act=0;unsigned char arg[0];};
struct Value {uint8_t *value=nullptr;unsigned value_len=0;};
struct btc_ble_gattc_args_t {Value write_char,write_descr,prep_write,prep_write_descr;};
using bt_status_t=int;
using btc_arg_deep_copy_t=void(*)(btc_msg_t*,void*,void*);
using btc_arg_deep_free_t=void(*)(btc_msg_t*);
constexpr unsigned BTC_GATTC_ACT_WRITE_CHAR=1,BTC_GATTC_ACT_WRITE_CHAR_DESCR=2,BTC_GATTC_ACT_PREPARE_WRITE=3,BTC_GATTC_ACT_PREPARE_WRITE_CHAR_DESCR=4,BTC_PID_NUM=8;
constexpr int BT_STATUS_SUCCESS=0,BT_STATUS_PARM_INVALID=1,BT_STATUS_NOMEM=2,OSI_THREAD_MAX_TIMEOUT=0;
#define BTC_TRACE_ERROR(...) ((void)0)
#define BTC_TRACE_DEBUG(...) ((void)0)
#define BTC_TRACE_WARNING(...) ((void)0)
static unsigned sdk_calls=0,sdk_fault=0;
static btc_msg_t *posted=nullptr;
void *osi_malloc(size_t n){if(++sdk_calls==sdk_fault)return nullptr;return std::malloc(n);}
void osi_free(void *p){std::free(p);}
bt_status_t btc_task_post(btc_msg_t *msg,int){assert(!posted);posted=msg;return BT_STATUS_SUCCESS;}
'''
for marker in ['void btc_gattc_arg_deep_copy(', 'void btc_gattc_arg_deep_free(']:
    cpp += definition(sdk_copy.read_text(), marker)+'\n'
cpp += definition(sdk_transfer.read_text(), 'bt_status_t btc_transfer_context(')+'\n'
cpp += r'''
int main(){
 Config healthy_config;IAPIBackend healthy;API baseline{{},{StateRegistration{&healthy_config}},{&healthy}};
 baseline.push_state_updates();assert(healthy.calls==1&&healthy.bytes>80&&healthy.valid_path);
 for(unsigned which=0;which<2;++which){
  Config c;IAPIBackend backend;API instance{{},{StateRegistration{&c}},{&backend}};
  malloc_calls=realloc_calls=0;fail_malloc=which==0?1:0;fail_realloc=which==1?1:0;watching=true;
  instance.push_state_updates();watching=false;
  std::fprintf(stderr,"case=%u malloc=%u realloc=%u backend_calls=%u bytes=%zu dirty=%u\n",which,malloc_calls,realloc_calls,backend.calls,backend.bytes,c.updated);
  assert(backend.calls==1&&c.updated==0);
  if(which==0){assert(backend.bytes==0);}else{assert(backend.bytes==healthy.bytes-1&&std::strcmp(backend.received,healthy.received)!=0);}
  instance.push_state_updates();assert(backend.calls==1); // Dirty flag lost: no retry.
  std::printf("EXTERNAL BLOCKER reproduced: %s OOM publishes %s Tesla payload, clears dirty flag, and never retries\n",which==0?"JSON document":"WString growth",which==0?"empty":"corrupted");
 }
 const uint8_t intended[]={0x12,0x34,0x56};
 for(unsigned act: {BTC_GATTC_ACT_WRITE_CHAR,BTC_GATTC_ACT_WRITE_CHAR_DESCR}){
  btc_msg_t msg;msg.act=act;btc_ble_gattc_args_t args;
  Value &v=act==BTC_GATTC_ACT_WRITE_CHAR?args.write_char:args.write_descr;
  v.value=const_cast<uint8_t*>(intended);v.value_len=sizeof(intended);
  sdk_calls=0;sdk_fault=1;
  assert(btc_transfer_context(&msg,&args,sizeof(args),btc_gattc_arg_deep_copy,btc_gattc_arg_deep_free)==BT_STATUS_NOMEM&&!posted);
  sdk_calls=0;sdk_fault=2;
  assert(btc_transfer_context(&msg,&args,sizeof(args),btc_gattc_arg_deep_copy,btc_gattc_arg_deep_free)==BT_STATUS_SUCCESS&&posted);
  auto *copy=reinterpret_cast<btc_ble_gattc_args_t*>(posted->arg);
  Value &copied=act==BTC_GATTC_ACT_WRITE_CHAR?copy->write_char:copy->write_descr;
  assert(!copied.value&&copied.value_len==0&&v.value_len==sizeof(intended));
  btc_gattc_arg_deep_free(posted);osi_free(posted);posted=nullptr;
  sdk_calls=0;sdk_fault=0;
  assert(btc_transfer_context(&msg,&args,sizeof(args),btc_gattc_arg_deep_copy,btc_gattc_arg_deep_free)==BT_STATUS_SUCCESS&&posted);
  copy=reinterpret_cast<btc_ble_gattc_args_t*>(posted->arg);
  Value &retry=act==BTC_GATTC_ACT_WRITE_CHAR?copy->write_char:copy->write_descr;
  assert(retry.value_len==sizeof(intended)&&!std::memcmp(retry.value,intended,sizeof(intended)));
  btc_gattc_arg_deep_free(posted);osi_free(posted);posted=nullptr;
  std::printf("EXTERNAL SDK BLOCKER reproduced: action=%u outer envelope OOM refuses, deep-copy OOM returns SUCCESS with length zero; healthy retry copies intended bytes\n",act);
 }
}
'''
(out / 'external_contract.cpp').write_text(cpp)
inputs = [config, api, sdk_copy, sdk_transfer, core/'WString.cpp', core/'WString.h']
(out / 'source-hashes.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},indent=2)+'\n')
cmd=['g++','-std=c++20','-fno-exceptions','-ffunction-sections','-fdata-sections','-Wl,--gc-sections,--wrap=malloc,--wrap=realloc','-Wno-sign-compare','-I'+str(shims),'-I'+str(core),'-I'+str(repo/'software/.pio/libdeps/warp4_tesla_ble/ArduinoJson/src'),str(shims/'WString.cpp'),str(out/'external_contract.cpp'),'-o',str(out/'external_contract')]
subprocess.run(cmd,check=True)
subprocess.run([str(out/'external_contract')],check=True)
