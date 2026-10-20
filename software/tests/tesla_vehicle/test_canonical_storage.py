#!/usr/bin/env python3
"""Compile the actual canonical storage policy with native IDF/FS boundaries."""
from pathlib import Path
import json,os,subprocess,tempfile
R=Path(__file__).resolve().parents[3];O=Path(os.environ.get('TESLA_TEST_REPORT_DIR',str(R/'multi-tesla-canonical-sol')))
with tempfile.TemporaryDirectory(prefix='canonical-mount-',dir=os.environ['TMPDIR']) as td:
 d=Path(td)
 (d/'sdkconfig.h').write_text('#pragma once\n#ifndef CONFIG_PARTITION_TABLE_OFFSET\n#define CONFIG_PARTITION_TABLE_OFFSET 0x8000\n#endif\n')
 (d/'esp_partition.h').write_text('''#pragma once
#include <cstdint>
#include <cstring>
using esp_partition_type_t=uint8_t;using esp_partition_subtype_t=uint8_t;
constexpr int ESP_PARTITION_SUBTYPE_DATA_SPIFFS=0x82;
struct esp_partition_t { const char *name; uint8_t type,subtype; uint32_t address,size; bool encrypted; };
inline esp_partition_t fixture[]={
 {"nvs",1,2,0x9000,0x5000,false},{"otadata",1,0,0xe000,0x2000,false},
 {"app0",0,0x10,0x10000,0x640000,false},{"app1",0,0x11,0x650000,0x640000,false},
 {"spiffs",1,0x82,0xc90000,0x360000,false},{"coredump",1,3,0xff0000,0x10000,false}};
inline int missing=-1;
inline const esp_partition_t *esp_partition_find_first(uint8_t t,uint8_t s,const char *n){
 for(int i=0;i<6;i++)if(i!=missing && fixture[i].type==t && fixture[i].subtype==s && strcmp(n,fixture[i].name)==0)return &fixture[i];
 return nullptr;
}
''')
 (d/'LittleFS.h').write_text('''#pragma once
#include <cassert>
#include <cstring>
inline int begins=0;inline bool mount_ok=true;
struct FS { bool begin(bool format,const char *path,int files,const char *label){
 assert(!format);assert(strcmp(path,"/data")==0);assert(files==10);assert(strcmp(label,"spiffs")==0);begins++;return mount_ok;
}};inline FS LittleFS;
''')
 (d/'esp_littlefs.h').write_text('''#pragma once
using esp_err_t=int;
constexpr int ESP_ERR_INVALID_ARG=258;
struct esp_vfs_littlefs_conf_t {const char *base_path;const char *partition_label;const void *partition;unsigned format_if_mount_failed:1;unsigned read_only:1;unsigned dont_mount:1;unsigned grow_on_mount:1;};
''')
 (d/'test.cpp').write_text('''#include "tools/canonical_bench_storage.h"
#include "tools/canonical_bench_vfs_guard.h"
#include <cstdio>
static int registrations=0;
extern "C" esp_err_t __real_esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *conf){
 assert(!conf->format_if_mount_failed && !conf->grow_on_mount);
 assert(conf->read_only && !conf->dont_mount);
 assert(strcmp(conf->base_path,"/data")==0 && strcmp(conf->partition_label,"spiffs")==0);
 registrations++;return 73;
}
int main(){
 assert(__wrap_esp_vfs_littlefs_register(nullptr)==ESP_ERR_INVALID_ARG && registrations==0);
 esp_vfs_littlefs_conf_t conf={"/data","spiffs",nullptr,1,1,0,1};
 assert(__wrap_esp_vfs_littlefs_register(&conf)==73 && registrations==1);
 assert(conf.format_if_mount_failed && conf.grow_on_mount); // Caller not mutated.
 assert(mount_canonical_bench_data());assert(begins==1);
 mount_ok=false;assert(!mount_canonical_bench_data());assert(begins==2);mount_ok=true;
 for(int i=0;i<6;i++){
  auto original=fixture[i];
  for(int fault=0;fault<6;fault++){
   begins=0;
   if(fault==0)missing=i;
   if(fault==1)fixture[i].type^=1;
   if(fault==2)fixture[i].subtype^=1;
   if(fault==3)fixture[i].address+=4096;
   if(fault==4)fixture[i].size-=4096;
   if(fault==5)fixture[i].encrypted=true;
   assert(!mount_canonical_bench_data());assert(begins==0);
   fixture[i]=original;missing=-1;
  }
 }
 puts("canonical actual mount policy: success, no-format mount failure, 36 metadata failures PASS");
}
''')
 flags=['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie','-DTESLA_BLE_MANUAL_WEB_BENCH=1','-DTESLA_BLE_CANONICAL_LAYOUT_BENCH=1','-I'+str(d),'-I'+str(R/'software/src'),str(d/'test.cpp'),'-o',str(d/'test')]
 subprocess.run(flags,check=True);subprocess.run([d/'test'],check=True)
 vehicle_flags=[f for f in flags if f!='-DTESLA_BLE_MANUAL_WEB_BENCH=1']+['-DTESLA_BLE_MANUAL_VEHICLE_UI=1']
 subprocess.run(vehicle_flags,check=True);subprocess.run([d/'test'],check=True)
 negative=subprocess.run([*flags,'-DCONFIG_PARTITION_TABLE_OFFSET=0xe000'],capture_output=True,text=True)
 assert negative.returncode!=0 and 'Canonical SDK required' in negative.stderr
 assert 'while (true) { delay(1000); }' in (R/'software/src/main.cpp').read_text()
 source=(R/'software/src/tools/canonical_bench_storage.h').read_text()
 assert 'format(' not in source and 'SPIFFS.begin' not in source
 (O/'CANONICAL-STORAGE-TEST.json').write_text(json.dumps({'exit':0,'actual_production_header_compiled':True,'actual_vfs_wrapper_compiled':True,'vfs_wrapper_disables_format_and_growth':True,'vfs_wrapper_preserves_other_options_and_return_value':True,'sanitizers':['address','undefined'],'metadata_fault_cases':36,'mount_success':True,'mount_failure_no_retry_no_format':True,'noncanonical_sdk_compile_rejected':True,'startup_halts_on_failure_before_module_setup':True},indent=2)+'\n')
