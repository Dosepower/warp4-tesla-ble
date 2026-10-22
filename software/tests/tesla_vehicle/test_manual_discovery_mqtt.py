#!/usr/bin/env python3
"""Execute unchanged extracted production MQTT methods at SDK/network boundaries."""
from pathlib import Path
import os,subprocess,tempfile
R=Path(__file__).resolve().parents[3]
source=(R/'software/src/modules/mqtt/mqtt.cpp').read_text()
def function(signature):
 start=source.index(signature);opening=source.index('{',start);depth=1;end=opening+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
methods='\n'.join(function(x) for x in ('bool Mqtt::pushStateUpdate(','bool Mqtt::pushRawStateUpdate(','IAPIBackend::WantsStateUpdate Mqtt::wantsStateUpdate('))
fixture=r'''
#include <string>
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdio>
using String=std::string;
struct IAPIBackend { enum class WantsStateUpdate { No,Later,AsString }; };
enum class MqttConnectionState { Connected,Disconnected };
struct FakeConfig { MqttConnectionState value=MqttConnectionState::Connected;FakeConfig *get(const char *) {return this;} template<class T>T asEnum(){return value;} };
struct { struct Entry {const char *path;};std::vector<Entry> states; } api;
static bool elapsed=true;static int now_us(){return 123;}static bool deadline_elapsed(int){return elapsed;}
class Mqtt {
public:
 std::vector<int> state_last_send=std::vector<int>(2,0);int send_interval=10;FakeConfig state;
 unsigned publications=0;bool succeed=true;
 bool publish_with_prefix(const String &,const String &) { ++publications;return succeed; }
 bool pushStateUpdate(size_t,const String &,const String &);
 bool pushRawStateUpdate(const String &,const String &);
 IAPIBackend::WantsStateUpdate wantsStateUpdate(size_t);
};
'''
main=r'''
int main() {
 api.states={{"tesla_vehicle/discovery"},{"mqtt/state"}};Mqtt m;
 assert(m.wantsStateUpdate(0)==IAPIBackend::WantsStateUpdate::No);
 assert(m.pushStateUpdate(0,"PRIVATE_RADIO_IDENTITIES","tesla_vehicle/discovery"));
 assert(m.pushRawStateUpdate("PRIVATE_RADIO_IDENTITIES","tesla_vehicle/discovery"));assert(m.publications==0);
 assert(m.wantsStateUpdate(1)==IAPIBackend::WantsStateUpdate::AsString);
 elapsed=false;assert(m.wantsStateUpdate(1)==IAPIBackend::WantsStateUpdate::Later);
 assert(!m.pushStateUpdate(1,"{}","mqtt/state"));assert(m.publications==0);
 elapsed=true;m.succeed=false;assert(!m.pushStateUpdate(1,"{}","mqtt/state"));assert(m.state_last_send[1]==0);
 m.succeed=true;assert(m.pushStateUpdate(1,"{}","mqtt/state"));assert(m.state_last_send[1]==123);
 assert(m.pushRawStateUpdate("{}","mqtt/state"));assert(m.publications==3);
 m.state.value=MqttConnectionState::Disconnected;assert(m.wantsStateUpdate(1)==IAPIBackend::WantsStateUpdate::No);
 std::puts("MANUAL_DISCOVERY_MQTT_PASS identities never queued/published; ordinary telemetry unchanged");
}
'''
with tempfile.TemporaryDirectory(prefix='discovery-mqtt-',dir=os.environ['TMPDIR']) as td:
 d=Path(td);(d/'test.cpp').write_text(fixture+methods+main)
 for name,flags in [('native',[]),('sanitize',['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'])]:
  subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',*flags,str(d/'test.cpp'),'-o',str(d/name)],check=True)
  subprocess.run([str(d/name)],check=True,timeout=15)
