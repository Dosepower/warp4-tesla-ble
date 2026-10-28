"""Execute the actual Arduino core + verbatim IDF release routines, no radio.
SDK/OS boundaries (status, heap registration, task scheduling) are controlled.
The boot Bluetooth decision block is copied verbatim from actual initArduino.
"""
from pathlib import Path
import os, subprocess, tempfile, sys, re, signal
S=Path(__file__).resolve().parents[2]; S=S.parent
root=Path(os.environ.get('TESLA_BLE_SDK_SOURCE_ROOT', '.pio/tesla-sdk'))
core=Path(os.environ.get('TESLA_CLASSIC_TEST_CORE',root/'builder/components/arduino'))
I=root/'esp-idf-secure'
header=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define _ESP32_ESP32_HAL_BT_H_
#define SOC_BT_SUPPORTED 1
#define CONFIG_BLUEDROID_ENABLED 1
#define CONFIG_BT_CONTROLLER_ENABLED 1
#ifndef CONFIG_SOC_BT_CLASSIC_SUPPORTED
#define CONFIG_SOC_BT_CLASSIC_SUPPORTED 1
#endif
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 259
#define log_e(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_ERROR_CHECK(x) do { if((x)!=0) abort(); } while(0)
#define pdMS_TO_TICKS(x) (x)
#define UC_BR_EDR_CTRL_MAX_SYNC_CONN_EFF 0
typedef int esp_err_t;
typedef enum { ESP_BT_MODE_IDLE=0,ESP_BT_MODE_BLE=1,ESP_BT_MODE_CLASSIC_BT=2,ESP_BT_MODE_BTDM=3 } esp_bt_mode_t;
typedef enum { BT_MODE_DEFAULT=0,BT_MODE_BLE=1,BT_MODE_CLASSIC_BT=2,BT_MODE_BTDM=3 } bt_mode;
enum {ESP_BT_CONTROLLER_STATUS_IDLE=0,ESP_BT_CONTROLLER_STATUS_INITED=1,ESP_BT_CONTROLLER_STATUS_ENABLED=2};
typedef struct {int mode;} esp_bt_controller_config_t;
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() ((esp_bt_controller_config_t){ESP_BT_MODE_BLE})
bool btMemRelease(bt_mode); bool btMemReleased(bt_mode);bool btStartMode(bt_mode); bool btStop(void);
bool btClassicInUse(void);bool bleInUse(void);bool btInUse(void);bool _btInUse_default(void);
int esp_bt_controller_get_status(void);int esp_bt_controller_init(esp_bt_controller_config_t*);int esp_bt_controller_enable(esp_bt_mode_t);int esp_bt_controller_disable(void);int esp_bt_controller_deinit(void);
int esp_bt_mem_release(esp_bt_mode_t);int esp_bt_controller_mem_release(esp_bt_mode_t);
void vTaskDelay(int);const char *esp_err_to_name(int);
'''
backend=r'''
#include "test_sdk.h"
typedef struct {esp_bt_mode_t mode;intptr_t start,end;} btdm_dram_available_region_t;
static int btdm_controller_status=0;
static unsigned added=0;static int add_calls=0,init_calls=0;static bool fail_heap=false;
static intptr_t first_start=0,last_end=0;
static int try_heap_caps_add_region(intptr_t a,intptr_t b) {
 assert(b>a);assert(a==SOC_MEM_BT_EM_BREDR_START && b==SOC_MEM_BT_EM_BREDR_REAL_END);
 if(fail_heap)return -1;
 ++add_calls;assert(add_calls==1);first_start=a;last_end=b;added+=(unsigned)(b-a);return 0;
}
static char _bt_bss_start,_bt_bss_end,_bt_data_start,_bt_data_end;
static char _bt_controller_bss_start,_bt_controller_bss_end,_bt_controller_data_start,_bt_controller_data_end;
'''
end=r'''
int esp_bt_controller_get_status(void){return btdm_controller_status;}
int esp_bt_controller_init(esp_bt_controller_config_t *c){assert(c->mode==ESP_BT_MODE_BLE);btdm_controller_mem_init();++init_calls;btdm_controller_status=1;return 0;}
int esp_bt_controller_enable(esp_bt_mode_t m){assert(m==ESP_BT_MODE_BLE);btdm_controller_status=2;return 0;}
int esp_bt_controller_disable(void){btdm_controller_status=1;return 0;}
int esp_bt_controller_deinit(void){btdm_controller_status=0;return 0;}
void vTaskDelay(int n){(void)n;}const char *esp_err_to_name(int n){(void)n;return "injected";}
bool bleInUse(void){return true;} // Actual production strong policy, not a release bypass.
'''
main=r'''
int main(int argc,char**argv){
 assert(argc==2);const char *mode=argv[1];
 if(!strcmp(mode,"nonclassic")){assert(btMemReleased(BT_MODE_CLASSIC_BT));assert(btMemRelease(BT_MODE_CLASSIC_BT));assert(!added);return 0;}
 assert(!btMemReleased(BT_MODE_CLASSIC_BT));assert(!btMemReleased(BT_MODE_BLE));
 if(!strcmp(mode,"inited")||!strcmp(mode,"enabled")) {
  btdm_controller_status=!strcmp(mode,"inited")?1:2;
  assert(!btMemRelease(BT_MODE_CLASSIC_BT));assert(!btMemReleased(BT_MODE_CLASSIC_BT));assert(!added);
  assert(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)==ESP_ERR_INVALID_STATE);assert(!btMemReleased(BT_MODE_CLASSIC_BT));
  btdm_controller_status=0;
 }
 if(!strcmp(mode,"heap-fail")){fail_heap=true;btMemRelease(BT_MODE_CLASSIC_BT);assert(!"IDF must fail-stop on region registration error");}
 if(!strcmp(mode,"sdk-first")){assert(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)==0);}
 else if(!strcmp(mode,"full-sdk-first")){assert(esp_bt_mem_release(ESP_BT_MODE_CLASSIC_BT)==0);}
 else {boot_bt_policy();}
 assert(added==15448 && add_calls==1);assert(btMemReleased(BT_MODE_CLASSIC_BT));assert(!btMemReleased(BT_MODE_BLE));
 for(unsigned i=0;i<sizeof(btdm_dram_available_region)/sizeof(btdm_dram_available_region[0]);++i){
  assert(!(btdm_dram_available_region[i].mode & ESP_BT_MODE_CLASSIC_BT));
  assert((btdm_dram_available_region[i].mode & ESP_BT_MODE_BLE) || i==4);
 }
 assert(first_start==0x3ffb2730 && last_end==0x3ffb6388);
 // Boot, Arduino wrapper and both external IDF entrypoints all share tracking.
 boot_bt_policy();assert(btMemRelease(BT_MODE_CLASSIC_BT));
 assert(esp_bt_mem_release(ESP_BT_MODE_CLASSIC_BT)==0);assert(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)==0);
 assert(add_calls==1 && !btMemReleased(BT_MODE_BLE));
 // Released Classic cannot initialize; BLE survives two complete start/stop cycles.
 assert(!btMemReleased(BT_MODE_BLE));
 // This request must not release BLE as a side effect of refusing Classic.
 assert(!btStartMode(BT_MODE_CLASSIC_BT));assert(init_calls==0);
 for(int i=0;i<2;++i){assert(btStartMode(BT_MODE_BLE));assert(btMemRelease(BT_MODE_CLASSIC_BT));assert(btStop());}
 assert(init_calls==2 && add_calls==1 && !btMemReleased(BT_MODE_BLE));
 assert(zero_calls==10); // five still-reserved non-data regions, two BLE starts.
 puts("CLASSIC RELEASE PASS");return 0;
}
'''
def extract(text,start,end):
 a=text.index(start);return text[a:text.index(end,a)]
with tempfile.TemporaryDirectory(prefix='classic-release-',dir=os.environ['TMPDIR']) as td:
 d=Path(td);(d/'test_sdk.h').write_text(header);(d/'esp_bt.h').write_text('#include "test_sdk.h"\n')
 # Force one include only, preserve original core translation unit as-is.
 (d/'test_sdk.h').write_text('#pragma once\n'+header)
 bt=(I/'components/bt/controller/esp32/bt.c').read_text()
 soc=(I/'components/bt/include/esp32/include/esp_bt.h').read_text()
 constants=[]
 for line in soc.splitlines():
  if re.match(r'#define\s+SOC_MEM_BT_',line):constants.append(line)
 if not constants:
  for p in (I/'components').glob('**/soc_memory_layout.h'):
   if 'esp32/' in str(p):
    constants += [x for x in p.read_text().splitlines() if re.match(r'#define\s+SOC_MEM_BT_',x)]
 assert constants,'Actual ESP32 controller constants missing'
 table=extract(bt,'static btdm_dram_available_region_t btdm_dram_available_region[]','/* Reserve the full memory')
 routines=extract(bt,'typedef struct {\n    intptr_t start;','\n#if CONFIG_BTDM_CTRL_HLI')
 mem_init=extract(bt,'static void btdm_controller_mem_init(void)','\nstatic esp_err_t try_heap_caps_add_region')
 init_shim=r'''
static char data_copy[8],data_rom[8];static unsigned zero_calls;
#define _data_start_btdm data_copy[0]
#define _data_end_btdm data_copy[4]
#define _data_start_btdm_rom ((intptr_t)data_rom)
static void *observe_memcpy(void *dest,const void *source,size_t n){assert(dest==data_copy&&source==data_rom&&n==4);return dest;}
static void *observe_memset(void *dest,int value,size_t n){
 intptr_t start=(intptr_t)dest,end=start+n;
 assert(value==0 && n>0 && (end<=SOC_MEM_BT_EM_BREDR_START || start>=SOC_MEM_BT_EM_BREDR_REAL_END));
 ++zero_calls;return dest;
}
#define memcpy observe_memcpy
#define memset observe_memset
'''
 misc=(core/'cores/esp32/esp32-hal-misc.c').read_text()
 boot=extract(misc,'  bool userOverriddenBtInUse','\n#endif\n  init();')
 # External callers are a different TU, just like consumers of SDK archives.
 # Calls resolved within the defining TU are NOT intercepted by GNU --wrap.
 external='int external_mem_release(esp_bt_mode_t);int external_controller_release(esp_bt_mode_t);\n'
 main_calls=main.replace('esp_bt_controller_mem_release(', 'external_controller_release(').replace('esp_bt_mem_release(', 'external_mem_release(')
 (d/'requests.c').write_text('#include "test_sdk.h"\nint external_mem_release(esp_bt_mode_t m){return esp_bt_mem_release(m);}\nint external_controller_release(esp_bt_mode_t m){return esp_bt_controller_mem_release(m);}\n')
 (d/'backend.c').write_text('\n'.join(constants)+'\n'+backend+table+routines+init_shim+mem_init+'\n#undef memcpy\n#undef memset\n'+end+'\nvoid boot_bt_policy(void){\n'+boot+'\n}\n'+external+main_calls)
 flags=['-std=gnu11','-Wall','-Wextra','-Wno-sign-compare','-Wno-enum-conversion','-Wno-unused-parameter','-I'+str(d),'-include',str(d/'test_sdk.h')]
 if 'sanitize' in sys.argv:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
 for variant,extra,modes in [('esp32',[],['boot','sdk-first','full-sdk-first','inited','enabled','heap-fail']),('classic-host',['-DCONFIG_BT_CLASSIC_ENABLED=1'],['boot']),('nonclassic',['-DCONFIG_SOC_BT_CLASSIC_SUPPORTED=0'],['nonclassic'])]:
  obj=d/(variant+'.o');exe=d/variant
  subprocess.run(['gcc',*flags,*extra,'-c',str(core/'cores/esp32/esp32-hal-bt.c'),'-o',str(obj)],check=True)
  subprocess.run(['gcc',*flags,*extra,str(d/'backend.c'),str(d/'requests.c'),str(obj),'-Wl,--wrap=esp_bt_mem_release','-Wl,--wrap=esp_bt_controller_mem_release','-o',str(exe)],check=True)
  for mode in modes:
   print('CORE MODE',variant,mode,flush=True)
   run=subprocess.run([str(exe),mode])
   assert (run.returncode==-signal.SIGABRT) if mode=='heap-fail' else (run.returncode==0),(variant,mode,run.returncode)
print('Actual core + IDF Classic-only release contracts passed')
