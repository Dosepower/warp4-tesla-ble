from pathlib import Path
import subprocess
from test_remote_crypto import S,MBED,CACHE

def test_bound_directional_sessions_reserve_before_use_retry_correlate_and_reboot(tmp_path):
 p=tmp_path/'session.cpp';p.write_text('''#include <cassert>
#include <cstring>
#include <vector>
#include "remote_session_link.h"
struct Peer{uint8_t id[32]={9},key[32]={3};uint64_t counter=0;bool active=true,fail=false;unsigned calls=0,delivered=0;};
bool rng(void*,uint8_t*p,size_t n){static unsigned k=7;while(n--)*p++=(uint8_t)++k;return true;}
bool load(void*p,const uint8_t*w,uint8_t*id,uint8_t*key){auto&s=*(Peer*)p;if(!s.active||(w&&memcmp(w,s.id,32)))return false;memcpy(id,s.id,32);memcpy(key,s.key,32);return true;}
bool reserve(void*p,const uint8_t*,uint64_t*v){auto&s=*(Peer*)p;if(s.fail)return false;*v=++s.counter;return true;}
bool respond(void*p,const uint8_t*in,uint8_t*out){auto&s=*(Peer*)p;s.calls++;memcpy(out,in,16);out[16]=0;return true;}
void deliver(void*p,const uint8_t*out){auto&s=*(Peer*)p;assert((out[0]==1||out[0]==3)&&out[16]==0);s.delivered++;}
void pump(RemoteSessionLink&a,RemoteSessionLink&b,unsigned now,bool drop=false){uint8_t f[20];while(auto n=a.frame(now,f)){if(!drop){assert(b.receive(now,f,n));assert(b.receive(now,f,n));}a.sent(now);}}
int main(){Peer w,t;RemoteSessionLink a(false,{&w,rng,load,reserve,respond,deliver}),b(true,{&t,rng,load,reserve,respond,deliver});
 assert(b.request(1)&&a.begin(100,7)&&b.begin(100,7));assert(w.counter==0&&t.counter==1);pump(b,a,110);assert(w.counter==1);pump(a,b,120);pump(b,a,130);pump(a,b,140);pump(b,a,150);assert(w.calls==1);
 std::vector<std::vector<uint8_t>> old;uint8_t f[20];while(auto n=a.frame(160,f)){old.emplace_back(f,f+n);a.sent(160);}assert(!b.complete());pump(a,b,700);pump(b,a,710);assert(a.complete()&&b.complete()&&t.delivered==1&&w.calls==1);
 a.cancel();b.cancel();assert(b.request(1)&&a.begin(2000,7)&&b.begin(2000,7));assert(t.counter==2);pump(b,a,2010);pump(a,b,2020);pump(b,a,2030);pump(a,b,2040);pump(b,a,2050);
 bool rejected=false;for(auto&v:old)if(!b.receive(2060,v.data(),v.size()))rejected=true;assert(rejected&&b.failed()&&t.delivered==1&&w.calls==2);a.cancel();b.cancel();
 t.active=false;assert(!b.begin(3000,1));t.active=true;t.fail=true;assert(!b.begin(3000,1));t.fail=false;t.key[0]^=1;
 assert(a.begin(4000,1)&&b.begin(4000,1));pump(b,a,4010);pump(a,b,4020);bool bad=false;while(auto z=b.frame(4030,f)){if(!a.receive(4030,f,z))bad=true;b.sent(4030);}assert(bad&&!a.complete()&&w.calls==2);
 a.cancel();b.cancel();t.key[0]^=1;
 // Repeat real production session objects in one device lifetime. Abort after
 // durable reservation but before sending, then simulate process reset by
 // constructing fresh endpoints over the retained persistence boundary.
 uint64_t reserved=t.counter;
 {RemoteSessionLink interrupted(true,{&t,rng,load,reserve,respond,deliver});assert(interrupted.begin(5000,7));assert(t.counter==reserved+1);}
 unsigned before_calls=w.calls,before_delivery=t.delivered;
 for(unsigned i=0;i<20;i++){
  unsigned now=6000+i*2000;
  assert(b.request(1)&&a.begin(now,7)&&b.begin(now,7));
  pump(b,a,now+10);pump(a,b,now+20);pump(b,a,now+30);pump(a,b,now+40);pump(b,a,now+50);
  pump(a,b,now+60,i%2==0);if(i%2==0){assert(!b.complete());pump(a,b,now+600);}
  pump(b,a,now+700);assert(a.complete()&&b.complete());
  assert(w.calls==before_calls+i+1&&t.delivered==before_delivery+i+1);
  a.cancel();b.cancel();
 }
 assert(t.counter==reserved+21);
 assert(!b.request(1,1)&&!b.request(4,1)); // charging remains prohibited
 assert(b.request(3,1)&&a.begin(50000,7)&&b.begin(50000,7));
 pump(b,a,50010);pump(a,b,50020);pump(b,a,50030);pump(a,b,50040);pump(b,a,50050);pump(a,b,50060);pump(b,a,50070);assert(a.complete()&&b.complete());
 a.cancel();b.cancel();assert(b.request(3,UINT64_MAX)&&!b.begin(52000,7));
 return 0;}
''')
 exe=tmp_path/'session';libs=[CACHE/n for n in ['library/libmbedcrypto.a','3rdparty/everest/libeverest.a','3rdparty/p256-m/libp256m.a']]
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-no-pie','-DWARP_REMOTE_CRYPTO_HOST=1','-I'+str(MBED/'include'),'-I'+str(S/'src/modules/tesla_vehicle'),str(p),str(S/'src/modules/tesla_vehicle/remote_crypto.cpp'),*map(str,libs),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
