from pathlib import Path
import subprocess
from test_remote_crypto import S,MBED,CACHE

def test_two_crypto_endpoints_fragment_retry_consent_and_persistence(tmp_path):
 p=tmp_path/'link.cpp';p.write_text('''#include <cassert>
#include <cstring>
#include "remote_enrollment_link.h"
struct Disk{int phase=0,writes=0;bool fail=false;};
bool random(void *,uint8_t *p,size_t n){static unsigned k=1;for(size_t i=0;i<n;i++)p[i]=(uint8_t)(++k);return true;}
bool save(void *p,const uint8_t*,const uint8_t*,unsigned state){auto&d=*static_cast<Disk*>(p);if(d.fail)return false;d.phase=state;d.writes++;return true;}
void pump(RemoteEnrollmentLink&a,RemoteEnrollmentLink&b,unsigned now,bool duplicate=false){
 uint8_t frame[20];size_t n;
 while((n=a.frame(now,frame))){assert(b.receive(now,frame,n));if(duplicate)assert(b.receive(now,frame,n));a.sent(now);}
}
int main(){
 Disk wd,td;RemoteEnrollmentLink w(false,{&wd,random,save}),t(true,{&td,random,save});
 assert(w.begin(100,9));assert(t.begin(100,9));pump(w,t,110,true);pump(t,w,120,true);
 assert(!t.awaiting_consent());pump(w,t,130,true);
 assert(t.awaiting_consent()&&!t.complete()&&!w.complete()&&td.phase==0&&wd.phase==0);
 assert(t.consent(200));pump(t,w,210);pump(w,t,220);pump(t,w,230);pump(w,t,240);
 // Drop tag final committed proof, retransmit only after retry timer.
 uint8_t f[20];assert(t.frame(250,f));t.sent(250);assert(t.frame(250,f));t.sent(250);
 assert(!w.complete());t.poll(900);pump(t,w,900,true);pump(w,t,910,true);pump(t,w,920,true);
 assert(w.complete()&&t.complete()&&wd.phase==3&&td.phase==3);
 assert(!memcmp(w.identity(),t.identity(),32));
 assert(wd.writes==3&&td.writes==3); // duplicate frames cannot repeat storage
 Disk fail;fail.fail=true;RemoteEnrollmentLink bad(false,{&fail,random,save}),other(true,{&td,random,save});
 assert(bad.begin(1000,10)&&other.begin(1000,10));pump(bad,other,1010);pump(other,bad,1020);pump(bad,other,1025);assert(other.consent(1030));
 uint8_t bytes[20];while(auto n=other.frame(1040,bytes)){bool ok=bad.receive(1040,bytes,n);other.sent(1040);if(!ok)break;}
 assert(bad.failed()&&!bad.complete());w.cancel();assert(!w.complete());
 return 0;
}
''')
 libs=[CACHE/n for n in ['library/libmbedcrypto.a','3rdparty/everest/libeverest.a','3rdparty/p256-m/libp256m.a']]
 exe=tmp_path/'link';subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-no-pie','-DWARP_REMOTE_CRYPTO_HOST=1','-I'+str(MBED/'include'),'-I'+str(S/'src/modules/tesla_vehicle'),str(p),str(S/'src/modules/tesla_vehicle/remote_crypto.cpp'),*map(str,libs),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
