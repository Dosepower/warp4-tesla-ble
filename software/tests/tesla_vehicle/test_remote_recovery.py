from pathlib import Path
import subprocess
from test_remote_crypto import S,MBED,CACHE

def test_fresh_authenticated_recovery_every_durable_asymmetry_and_replay(tmp_path):
 p=tmp_path/'recovery.cpp';p.write_text('''#include <cassert>
#include <cstring>
#include "remote_recovery_link.h"
struct Store{uint8_t id[32]={1},key[32]={2};unsigned phase=1,saves=0;bool bad=false;};
bool random(void*,uint8_t*p,size_t n){static unsigned x=11;while(n--)*p++=(uint8_t)++x;return true;}
bool load(void*p,const uint8_t*id,uint8_t*out,uint8_t*key,unsigned*phase){auto&s=*(Store*)p;if(s.bad||(id&&memcmp(id,s.id,32)))return false;memcpy(out,s.id,32);memcpy(key,s.key,32);*phase=s.phase;return true;}
bool save(void*p,unsigned phase){auto&s=*(Store*)p;if(s.bad)return false;if(phase>s.phase)s.phase=phase;s.saves++;return true;}
void pump(RemoteRecoveryLink&a,RemoteRecoveryLink&b,unsigned now,bool drop=false){uint8_t f[20];while(auto n=a.frame(now,f)){if(!drop){assert(b.receive(now,f,n));assert(b.receive(now,f,n));}a.sent(now);}}
int main(){
 for(unsigned wp=1;wp<=3;wp++)for(unsigned tp=1;tp<=3;tp++){
 Store w,t;w.phase=wp;t.phase=tp;RemoteRecoveryLink a(false,{&w,random,load,save}),b(true,{&t,random,load,save});assert(a.select(w.id));
 assert(a.begin(100,7)&&b.begin(100,7));pump(a,b,110);pump(b,a,120);pump(a,b,130);
 assert(b.awaiting_consent()&&!a.complete()&&!b.complete()&&w.phase==wp&&t.phase==tp);
 assert(b.consent(200));pump(b,a,210);pump(a,b,220);pump(b,a,230);pump(a,b,240);
 pump(b,a,250,true);assert(!a.complete());pump(b,a,800);assert(a.complete()&&b.complete()&&w.phase==3&&t.phase==3);
 uint8_t old[20];b.poll(1400);auto n=b.frame(1400,old);assert(n);a.cancel();b.cancel();
 assert(a.begin(2000,7)&&b.begin(2000,7));assert(a.receive(2010,old,n));b.sent(1400);n=b.frame(2010,old); // no valid new proof can exist before hello
 assert(!a.complete());a.cancel();b.cancel();
 }
 Store w,t;t.key[0]^=1;RemoteRecoveryLink a(false,{&w,random,load,save}),b(true,{&t,random,load,save});assert(a.select(w.id));assert(a.begin(0,3)&&b.begin(0,3));pump(a,b,10);pump(b,a,20);uint8_t f[20];bool denied=false;while(auto n=a.frame(30,f)){if(!b.receive(30,f,n))denied=true;a.sent(30);}assert(denied&&!b.awaiting_consent()&&!b.complete()&&!w.saves&&!t.saves);
 a.cancel();b.cancel();assert(a.begin(100,3));a.poll(30100);assert(a.failed()&&!a.complete());
 return 0;
}
''')
 exe=tmp_path/'recovery';libs=[CACHE/n for n in ['library/libmbedcrypto.a','3rdparty/everest/libeverest.a','3rdparty/p256-m/libp256m.a']]
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-no-pie','-DWARP_REMOTE_CRYPTO_HOST=1','-I'+str(MBED/'include'),'-I'+str(S/'src/modules/tesla_vehicle'),str(p),str(S/'src/modules/tesla_vehicle/remote_crypto.cpp'),*map(str,libs),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
