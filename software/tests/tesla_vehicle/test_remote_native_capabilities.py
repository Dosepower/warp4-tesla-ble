"""Execute actual native preflight body; only hardware/config boundaries shimmed."""
from pathlib import Path
import subprocess
S=Path(__file__).resolve().parents[2]

def test_actual_native_manager_supported_authorization_gate(tmp_path):
 text=(S/'src/modules/evse_common/evse_common.cpp').read_text()
 body=text.split('uint8_t EvseCommon::get_remote_mode_capabilities()',1)[1].split('\nuint32_t EvseCommon::get_evse_version()',1)[0]
 p=tmp_path/'caps.cpp';p.write_text('''#include <cassert>
#include <cstdint>
#include <vector>
#include "manager_mode_observation.h"
#include "'''+str(S/'src/modules/cm_networking/generated/config_charge_mode.enum.h')+'''"
#define MODULE_CM_NETWORKING_AVAILABLE() 1
#define CHARGER_STATE_NOT_PLUGGED_IN 0
#define CHARGER_STATE_ERROR 4
#define CHARGING_SLOT_USER 6
struct millis_t{};struct Time{uint64_t us;template<class T>Time to()const{return {us/1000};}template<class T>T as()const{return (T)us;}};static uint64_t clock_ms=1000;static Time now_us(){return {clock_ms*1000};}
struct Node{uint32_t v=0;bool asBool()const{return v!=0;}uint32_t asUint()const{return v;}template<class T>T asEnum()const{return (T)v;}};
struct Object{Node value;Node*get(const char*){return &value;}};
struct Modes{std::vector<Node> values{{0},{1},{2}};size_t count()const{return values.size();}Node*get(size_t i){return &values.at(i);}};
struct Backend{bool initialized=true,active=false;uint16_t current=0;int error=0;bool slow=false;bool is_initialized(){return initialized;}int get_charging_slot(unsigned slot,uint16_t*c,bool*a,bool*){assert(slot==6);*c=current;*a=active;if(slow)clock_ms+=6000;return error;}};
struct EvseCommon{Backend hardware;Backend*backend=&hardware;Object state{{1}},management_state{{0}};Modes supported_charge_modes;ManagerModeObservation manager_mode_observation;bool enabled=true;bool get_management_enabled(){return enabled;}uint8_t get_remote_mode_capabilities();};
uint8_t EvseCommon::get_remote_mode_capabilities()'''+body+'''
int main(){EvseCommon e;e.manager_mode_observation.observe(1000,2,true);assert(e.get_remote_mode_capabilities()==7);
 e.supported_charge_modes.values={{1}};assert(e.get_remote_mode_capabilities()==1);e.supported_charge_modes.values={{0},{2},{3},{4},{9}};assert(e.get_remote_mode_capabilities()==6);e.supported_charge_modes.values={{3},{4}};assert(!e.get_remote_mode_capabilities());e.supported_charge_modes.values={{0},{1},{2}};
 e.hardware.active=true;assert(!e.get_remote_mode_capabilities());e.hardware.current=6000;assert(e.get_remote_mode_capabilities()==7);e.hardware.error=1;assert(!e.get_remote_mode_capabilities());e.hardware.error=0;
 e.management_state.value.v=1;assert(!e.get_remote_mode_capabilities());e.management_state.value.v=0;
 e.state.value.v=0;assert(!e.get_remote_mode_capabilities());e.state.value.v=4;assert(!e.get_remote_mode_capabilities());e.state.value.v=1;
 e.enabled=false;assert(!e.get_remote_mode_capabilities());e.enabled=true;e.hardware.initialized=false;assert(!e.get_remote_mode_capabilities());e.hardware.initialized=true;
 clock_ms=6001;assert(!e.get_remote_mode_capabilities());clock_ms=999;assert(!e.get_remote_mode_capabilities());clock_ms=1000;e.hardware.slow=true;assert(!e.get_remote_mode_capabilities());e.backend=nullptr;assert(!e.get_remote_mode_capabilities());
}
''')
 exe=tmp_path/'caps';subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(S/'src/modules/evse_common'),'-fsanitize=address,undefined','-no-pie',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
