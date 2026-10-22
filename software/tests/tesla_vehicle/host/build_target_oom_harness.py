#!/usr/bin/env python3
"""Compile unchanged target WString + exact Config/UUID definitions natively.
Only unrelated SDK/platform/Config slot storage is shimmed; allocator behavior
and Config update method bodies are production. Never write framework inputs.
"""
from pathlib import Path
import os, re, subprocess, json, hashlib, sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
from bench_confinement import output_plan
repo = Path(__file__).resolve().parents[4]
arduinojson = Path(os.environ.get('TESLA_HOST_ARDUINOJSON', repo / 'software/.pio/libdeps/warp4_tesla_ble/ArduinoJson'))
fw = Path(os.environ['TESLA_BLE_HOST_FRAMEWORK_SOURCE']) / 'components/arduino'
out = Path(os.environ.get('TESLA_CLEAN_TEST_OUT', str(Path(os.environ['TMPDIR']) / 'warp-tesla-host-tests'))) / 'target-oom'
output_plan(roots=(out,)).revalidate()
out.mkdir(parents=True, exist_ok=True)
core = fw / 'cores/esp32'
# Byte-for-byte copy changes quoted-include resolution, not source behavior.
(out/'WString.cpp').write_bytes((core/'WString.cpp').read_bytes())
presence_probe = os.environ.get('TESLA_CLEAN_PRESENCE_OOM') == '1'
if presence_probe:
    # Exact ESP32 SSO layout: 32-bit pointer + two uint32 fields + 3 = 15
    # buffer bytes, flag at offset 15, capacity 14. Native heap pointers remain
    # native; do not claim this adapts the complete Xtensa ABI.
    s = (core/'WString.h').read_text()
    assert s.count('SSOSIZE = sizeof(struct _ptr) + 4 - 1') == 1
    (out/'WString.h').write_text(s.replace('SSOSIZE = sizeof(struct _ptr) + 4 - 1','SSOSIZE = 15'))
def definition(text, marker):
    start = text.index(marker)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
(out/'pgmspace.h').write_text('#pragma once\n#include <cstring>\n#define PROGMEM\n#define PSTR(s) (s)\n#define memcpy_P memcpy\n')
(out/'Arduino.h').write_text('#pragma once\n#include <cstdio>\n#include <cmath>\n#include <algorithm>\n#include <WString.h>\n')
(out/'esp32-hal-log.h').write_text('#pragma once\n#define log_e(...) ((void)0)\n#define log_w(...) ((void)0)\n')
(out/'stdlib_noniso.h').write_text('#pragma once\nchar *utoa(unsigned int,char *,int); char *itoa(int,char *,int); char *ltoa(long,char *,int); char *ultoa(unsigned long,char *,int); char *lltoa(long long,char *,int); char *ulltoa(unsigned long long,char *,int); char *dtostrf(double,signed char,unsigned char,char *);\n')
config_cpp=(repo/'software/src/config.cpp').read_text()
config_h=(repo/'software/src/config.h').read_text()
header='''#include <WString.h>
#include "cool_string.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <utility>
inline void esp_system_abort(const char *) { std::abort(); }
struct Config {
 struct ConfString { CoolString text; CoolString *getVal(){return &text;} };
 struct { unsigned char updated=0; } value;
 ConfString slot;
 template<class T> bool is() const {return true;}
 template<class T> T *get() {return &slot;}
 bool updateString(const String &);
'''
checked='bool Config::updateStringChecked' in config_cpp
if checked: header+='bool updateStringChecked(const char *);\n'
header+=definition(config_h,'template<typename T, typename ConfigT>\n    bool update_value')+'\n};\n'
# Type-error diagnostics are an unexercised wrong-type path; preserve body.
header=header.replace('struct Config {','inline void config_abort_on_type_error(const char *, const void *, const char *, const String * = nullptr) { std::abort(); }\nstruct Config {')
header+=definition(config_cpp,'bool Config::updateString(const String &val)')+'\n'
if checked: header+=definition(config_cpp,'bool Config::updateStringChecked')+'\n'
uuid=(fw/'libraries/BLE/src/BLEUUID.cpp').read_text()
header+='''struct esp_bt_uuid_t { uint16_t len; union {uint16_t uuid16;uint32_t uuid32;uint8_t uuid128[16];} uuid; };
#define ESP_UUID_LEN_128 16
#define BLE_UUID_16_BITS 2
#define BLE_UUID_32_BITS 4
#define BLE_UUID_128_BITS 16
#define UUID_LEN(x) ((x).len)
#define UUID_VAL_16(x) ((x).uuid.uuid16)
#define UUID_VAL_32(x) ((x).uuid.uuid32)
#define UUID_VAL_128(x) ((x).uuid.uuid128)
#define log_e(...) ((void)0)
#define log_v(...) ((void)0)
inline void memrcpy(uint8_t *to,uint8_t *from,size_t n){for(size_t i=0;i<n;++i){to[i]=from[n-1-i];}}
class BLEUUID {public: BLEUUID(String value);esp_bt_uuid_t *getNative();private:esp_bt_uuid_t m_uuid{};bool m_valueSet=false;};
'''
header+=definition(uuid,'BLEUUID::BLEUUID(String value)')+'\n'+definition(uuid,'esp_bt_uuid_t *BLEUUID::getNative()')+'\n'
helper=repo/'software/src/modules/tesla_vehicle/tesla_ble_uuid.h'
if helper.exists(): header+=f'#include "{helper}"\n'
gatt=(repo/'software/src/modules/tesla_vehicle/tesla_ble_gatt.cpp').read_text()
header+='\n'.join(re.findall(r'static constexpr const char \*TESLA_(?:SERVICE|WRITE|NOTIFY).*?;',gatt))+'\n'
lines=re.findall(r'    auto (?:service|write|notify)_uuid = .*?;',gatt)
assert len(lines)==3
header+='esp_bt_uuid_t operational_uuid(unsigned which) {\n'+'\n'.join(lines)+'\nreturn which==0?service_uuid:which==1?write_uuid:notify_uuid;\n}\n'
(out/'target_oom_definitions.inc').write_text(header)
if presence_probe:
    header=header.replace('CoolString *getVal(){return &text;}','CoolString *getVal(){return &text;} const CoolString *getVal() const {return &text;}')
    header += '#include <ArduinoJson.h>\n'
    visitors=repo/'software/src/config/visitors.h'
    header += 'struct PresenceVisitor { JsonVariant insertHere;\n'+definition(visitors.read_text(),'void operator()(const Config::ConfString &x)')+'\n};\n'
    helper=repo/'software/src/modules/tesla_vehicle/tesla_ble_presence.h'
    if helper.exists():
        module=(repo/'software/src/modules/tesla_vehicle/tesla_vehicle.cpp').read_text()
        assert module.count('tesla_ble_publish_presence(tesla->state.get("key_presence")') == 3
        assert 'Config::Str("unknown", 0, TESLA_BLE_PRESENCE_MAX_LENGTH)' in module
        header += f'#include "{helper}"\n'
        header += 'void presence_site(Config &c,bool present,bool known,bool cleanup) { tesla_ble_publish_presence(&c,present,known,cleanup); }\n'
    else:
        module=(repo/'software/src/modules/tesla_vehicle/tesla_vehicle.cpp').read_text()
        expressions=re.findall(r'state.get\("key_presence"\)->updateString\(((?:outcome|pair)->cleanup_required.*?absent".*?)\);',module,re.S)
        assert len(expressions)==3
        normalized=[]
        for expression in expressions:
            expression=re.sub(r'(outcome|pair)->cleanup_required','cleanup',expression)
            expression=re.sub(r'(outcome|pair)->key_known','known',expression)
            expression=re.sub(r'(outcome|pair)->key_present','present',expression)
            normalized.append(' '.join(expression.split()))
        assert len(set(normalized))==1
        header += 'void presence_site(Config &c,bool present,bool known,bool cleanup) { c.updateString('+normalized[0]+'); }\n'
    (out/'target_oom_definitions.inc').write_text(header)
inputs=[core/'WString.cpp',core/'WString.h',fw/'libraries/BLE/src/BLEUUID.cpp',repo/'software/src/config.cpp',repo/'software/src/config.h',repo/'software/src/modules/tesla_vehicle/tesla_ble_gatt.cpp']
if presence_probe:
    inputs += [repo/'software/src/modules/tesla_vehicle/tesla_vehicle.cpp']
    inputs += [repo/'software/src/config/visitors.h',arduinojson/'src/ArduinoJson.h']
    if helper.exists(): inputs += [helper]
    (out/'layout-adaptation.json').write_text(json.dumps({'SSOSIZE':15,'capacity':14,'sizeof_String':16,'flag_offset':15,'adaptation':'ESP32 SSO geometry only; native heap pointers retained; original WString.cpp and Config update method bodies unmodified','schema_max_length':7,'presence_sites':3},indent=2))
(out/'source-hashes.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},indent=2)+'\n')
probe=Path(os.environ.get('TESLA_CLEAN_TARGET_OOM_TEST',str(repo/'software/tests/tesla_vehicle/test_tesla_ble_target_oom.cpp')))
instrument=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie'] if os.environ.get('TESLA_CLEAN_HARNESS_SANITIZE')=='1' else []
cmd=['g++',*instrument,'-std=c++20','-fno-exceptions','-ffunction-sections','-fdata-sections','-Wl,--gc-sections,--wrap=realloc','-Wno-sign-compare','-I'+str(out),'-I'+str(core),'-I'+str(repo/'software/src'),str(out/'WString.cpp'),str(probe),'-o',str(out/'test_tesla_ble_target_oom')]
if presence_probe: cmd += ['-I'+str(arduinojson/'src')]
subprocess.run(cmd,check=True)
subprocess.run([str(out/'test_tesla_ble_target_oom')],check=True)
