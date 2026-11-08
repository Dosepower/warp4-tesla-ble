"""Execute exact production timestamp accessor; meter/clock boundaries only."""
from pathlib import Path
import subprocess
S=Path(__file__).resolve().parents[2]

def test_actual_meter_accessor_preserves_complete_update_time(tmp_path):
 source=(S/'src/modules/meters/meters.cpp').read_text()
 body=source.split('MeterValueAvailability Meters::get_power_observation(',1)[1].split('\nbool Meters::meter_is_fresh',1)[0]
 p=tmp_path/'meter.cpp'
 p.write_text('''#include <cassert>
#include <cstdint>
#include <cmath>
struct millis_t{};
struct micros_t {int64_t v;template<class T> micros_t to()const{return {v/1000};}template<class T>T as()const{return (T)v;}};
bool operator<(micros_t a,micros_t b){return a.v<b.v;}
constexpr micros_t operator""_us(unsigned long long v){return {(int64_t)v};}
static micros_t clock_value{1000000};static micros_t now_us(){return clock_value;}
#define OPTIONS_METERS_MAX_SLOTS() 2
// Values match source names; no simulated firmware response is substituted.
enum class MeterValueAvailability {Unavailable,CurrentlyUnknown,Fresh,Stale};
struct Meters {struct Slot{micros_t values_last_updated_at;};Slot *meter_slots=nullptr;unsigned calls=0;float sample=42;MeterValueAvailability available=MeterValueAvailability::Fresh;
 MeterValueAvailability get_power(uint32_t slot,float *out,micros_t max_age){assert(slot<2);calls++;*out=sample;return available==MeterValueAvailability::Fresh&&clock_value.v-meter_slots[slot].values_last_updated_at.v>max_age.v?MeterValueAvailability::Stale:available;}
 MeterValueAvailability get_power_observation(uint32_t,float*,uint64_t*,micros_t);
};
MeterValueAvailability Meters::get_power_observation('''+body+'''
int main(){Meters m;float power=NAN;uint64_t received=999;
 assert(m.get_power_observation(0,&power,&received,{5000000})==MeterValueAvailability::Unavailable&&m.calls==0);
 Meters::Slot slots[2]={{{750000}},{{-1}}};m.meter_slots=slots;
 assert(m.get_power_observation(2,&power,&received,{5000000})==MeterValueAvailability::Unavailable&&m.calls==0);
 assert(m.get_power_observation(0,nullptr,&received,{5000000})==MeterValueAvailability::Unavailable);
 assert(m.get_power_observation(0,&power,nullptr,{5000000})==MeterValueAvailability::Unavailable);
 assert(m.get_power_observation(0,&power,&received,{5000000})==MeterValueAvailability::Fresh&&received==750&&power==42);
 clock_value={2000000};assert(m.get_power_observation(0,&power,&received,{5000000})==MeterValueAvailability::Fresh&&received==750);
 clock_value={5750001};assert(m.get_power_observation(0,&power,&received,{5000000})==MeterValueAvailability::Stale&&received==0);
 slots[0].values_last_updated_at={6000000};assert(m.get_power_observation(0,&power,&received,{5000000})==MeterValueAvailability::Stale&&received==0);
 assert(m.get_power_observation(1,&power,&received,{5000000})==MeterValueAvailability::Stale&&received==0);
 slots[0].values_last_updated_at={5750000};m.available=MeterValueAvailability::CurrentlyUnknown;assert(m.get_power_observation(0,&power,&received,{5000000})==MeterValueAvailability::CurrentlyUnknown&&received==0);
}
''')
 exe=tmp_path/'meter'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-no-pie',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
