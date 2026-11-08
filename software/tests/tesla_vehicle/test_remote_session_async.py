"""Production encrypted transport defers reply; admission happens exactly once."""
from test_remote_crypto import S, MBED, CACHE
import subprocess

def test_deferred_authenticated_response_retries_cancel_timeout(tmp_path):
 p=tmp_path/'async.cpp'
 p.write_text('''#include <cassert>
#include <cstring>
#include "remote_session_link.h"
struct Peer{uint8_t id[32]={9},key[32]={3};uint64_t counter=0;unsigned starts=0,polls=0,delivered=0,aborts=0;bool waiting=false,ready=false,corrupt=false;};
bool rng(void*,uint8_t*p,size_t n){static unsigned k=7;while(n--)*p++=(uint8_t)++k;return true;}
bool load(void*p,const uint8_t*w,uint8_t*id,uint8_t*key){auto&s=*(Peer*)p;if(w&&memcmp(w,s.id,32))return false;memcpy(id,s.id,32);memcpy(key,s.key,32);return true;}
bool reserve(void*p,const uint8_t*,uint64_t*v){*v=++((Peer*)p)->counter;return true;}
bool respond(void*p,const uint8_t*,uint8_t*){auto&s=*(Peer*)p;s.starts++;s.waiting=true;return false;}
bool pending(void*p){return ((Peer*)p)->waiting;}
int finish(void*p,const uint8_t*request,uint8_t*reply){auto&s=*(Peer*)p;s.polls++;if(!s.ready)return 0;memcpy(reply,request,16);if(s.corrupt)reply[1]^=1;reply[16]=1;s.waiting=false;return 1;}
void abort(void*p){auto&s=*(Peer*)p;s.aborts++;s.waiting=false;}
void deliver(void*p,const uint8_t*out){assert(out[0]==1&&out[16]==1);((Peer*)p)->delivered++;}
void pump(RemoteSessionLink&a,RemoteSessionLink&b,unsigned now){uint8_t f[20];while(auto n=a.frame(now,f)){assert(b.receive(now,f,n));assert(b.receive(now,f,n));a.sent(now);}}
void handshake(RemoteSessionLink&a,RemoteSessionLink&b,unsigned n){assert(a.begin(n,7)&&b.request(1)&&b.begin(n,7));pump(b,a,n+10);pump(a,b,n+20);pump(b,a,n+30);pump(a,b,n+40);pump(b,a,n+50);}
int main(){Peer w,t;RemoteSessionLink a(false,{&w,rng,load,reserve,respond,deliver,pending,finish,abort}),b(true,{&t,rng,load,reserve,respond,deliver});
 handshake(a,b,100);assert(w.starts==1&&t.delivered==0&&w.waiting);pump(b,a,700);pump(b,a,1300);assert(w.starts==1&&!a.failed()&&t.delivered==0);
 w.ready=true;pump(a,b,1400);pump(b,a,1410);assert(a.complete()&&b.complete()&&t.delivered==1&&w.starts==1&&w.aborts==0);a.cancel();b.cancel();assert(w.aborts==0);
 w.ready=false;handshake(a,b,2000);assert(w.starts==2);a.cancel();a.cancel();assert(w.aborts==1&&!w.waiting);b.cancel();
 handshake(a,b,4000);uint8_t f[20];assert(a.frame(14000,f)==0&&a.failed()&&w.aborts==2&&t.delivered==1);a.cancel();b.cancel();assert(w.aborts==2);
 handshake(a,b,15000);w.ready=true;w.corrupt=true;assert(a.frame(15100,f)==0&&a.failed()&&t.delivered==1&&w.aborts==3);
}
''')
 exe=tmp_path/'async'
 libs=[CACHE/n for n in ['library/libmbedcrypto.a','3rdparty/everest/libeverest.a','3rdparty/p256-m/libp256m.a']]
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-no-pie','-DWARP_REMOTE_CRYPTO_HOST=1','-I'+str(MBED/'include'),'-I'+str(S/'src/modules/tesla_vehicle'),str(p),str(S/'src/modules/tesla_vehicle/remote_crypto.cpp'),*map(str,libs),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
