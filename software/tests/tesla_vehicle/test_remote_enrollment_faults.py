from pathlib import Path
import subprocess
from test_remote_crypto import S,MBED,CACHE
TAG=S.parents[1]/'tag'

def test_production_peer_engines_real_stores_fault_reboot_and_transcript_matrix(tmp_path):
 p=tmp_path/'matrix.cpp';p.write_text('''#include <cassert>
#include <cstring>
#include <vector>
#include "remote_enrollment_link.h"
#include "remote_binding_store.h"
#include "remote_pair_store.h"
struct Disk{std::vector<uint8_t> bytes;unsigned phase=0,fail_phase=0;bool after_write=false,read_fail=false;};
int read(void*p,uint8_t*out,size_t n){auto&d=*(Disk*)p;if(d.read_fail)return -1;if(d.bytes.empty())return 0;if(n!=d.bytes.size())return -1;memcpy(out,d.bytes.data(),n);return 1;}
bool write(void*p,const uint8_t*in,size_t n){auto&d=*(Disk*)p;if(d.phase==d.fail_phase&&!d.after_write)return false;d.bytes.assign(in,in+n);if(d.phase==d.fail_phase)d.read_fail=true;return true;}
bool rng(void*,uint8_t*p,size_t n){static uint8_t k=11;while(n--)*p++=++k;return true;}
struct Peer{
 Disk disk;bool tag;RemoteBindingStore warp;remote_pair_store nordic{};RemoteEnrollmentLink link;
 Peer(bool tag):tag(tag),warp({&disk,read,write}),link(tag,{this,rng,save}){assert(warp.load());assert(remote_pair_load(&nordic,{&disk,read,write}));}
 static bool save(void*p,const uint8_t*id,const uint8_t*k,unsigned phase){auto&s=*(Peer*)p;s.disk.phase=phase;
  if(phase==s.disk.fail_phase&&!s.disk.after_write)return false;
  if(s.tag)return remote_pair_save(&s.nordic,id,k,phase);
  return phase==1?s.warp.prepare(id,k,"Test remote"):phase==2?s.warp.commit_prepared(id,k):s.warp.verify_prepared(id,k)&&s.warp.activate(id);
 }
 void reboot_check(){
  disk.read_fail=false;
  if(tag){remote_pair_store loaded;assert(remote_pair_load(&loaded,{&disk,read,write}));if(loaded.phase<3)assert(!remote_pair_trusted(&loaded));if(loaded.phase)assert(!remote_pair_save(&loaded,(const uint8_t*)"00000000000000000000000000000000",loaded.key,1));}
  else{RemoteBindingStore loaded({&disk,read,write});assert(loaded.load());for(unsigned i=0;i<4;i++)if(loaded.slot(i).state==1||loaded.slot(i).state==3)assert(loaded.count()==0);}
 }
};
bool pump(Peer&a,Peer&b,uint64_t now,int corrupt=0,bool drop=false){uint8_t f[20];size_t n;while((n=a.link.frame(now,f))){if(corrupt==1)f[1]^=1;if(corrupt==2)f[n-1]^=1;bool ok=drop||b.link.receive(now,f,n);a.link.sent(now);if(!ok)return false;}return true;}
void hello(Peer&w,Peer&t){assert(w.link.begin(100,23)&&t.link.begin(100,23));assert(pump(w,t,110));assert(pump(t,w,120));assert(pump(w,t,130));assert(t.link.awaiting_consent()&&t.link.consent(200));}
int main(){
 unsigned cases=0;
 for(unsigned target=0;target<2;target++)for(unsigned phase=1;phase<=3;phase++)for(unsigned after=0;after<2;after++){
  Peer w(false),t(true);auto&fault=target?t:w;fault.disk.fail_phase=phase;fault.disk.after_write=after;
  hello(w,t);
  for(unsigned i=0;i<12&&!w.link.failed()&&!t.link.failed();i++){pump(t,w,210+i*20);pump(w,t,220+i*20);}
  assert(!w.link.complete()||!t.link.complete());
  w.link.cancel();t.link.cancel();w.reboot_check();t.reboot_check();cases++;
 }
 // One lost final Tag receipt: WARP remains unpaired, Tag retransmits exact
 // ciphertext (no new nonce/plaintext, no extra persistent activation).
 {Peer w(false),t(true);hello(w,t);for(unsigned i=0;i<3;i++){assert(pump(t,w,210+i*20));assert(pump(w,t,220+i*20));}
  assert(t.disk.phase==3&&w.disk.phase==2);assert(pump(t,w,300,0,true));assert(!w.link.complete());
  assert(pump(t,w,900));assert(w.link.complete()&&t.link.complete());cases++;}
 for(int corruption=1;corruption<=2;corruption++){
  Peer w(false),t(true);hello(w,t);assert(!pump(t,w,210,corruption));assert(w.link.failed()&&!w.link.complete()&&w.warp.count()==0);cases++;
 }
 {Peer w(false),t(true);hello(w,t);w.link.cancel();uint8_t f[20];auto n=t.link.frame(210,f);assert(n&&!w.link.receive(210,f,n));assert(w.link.begin(300,23));assert(w.link.receive(301,f,n));t.link.sent(210);n=t.link.frame(211,f);assert(n&&!w.link.receive(302,f,n));assert(!w.link.complete());cases++;}
 {Peer w(false),t(true);hello(w,t);w.link.poll(30100);t.link.poll(30100);assert(w.link.failed()&&t.link.failed()&&!w.link.complete()&&!t.link.complete());cases++;}
 // Wrong peer/transcript: mutate just the remote hello nonce (valid P-256
 // public key preserved), then prove the Tag refuses hello receipt/consent.
 {Peer w(false),t(true);assert(w.link.begin(100,23)&&t.link.begin(100,23));assert(pump(w,t,110));
  uint8_t f[20];size_t n;while((n=t.link.frame(120,f))){if(f[3]>=70)f[n-1]^=1;assert(w.link.receive(120,f,n));t.link.sent(120);}
  assert(!pump(w,t,130));assert(t.link.failed()&&!t.link.consent(200)&&t.disk.bytes.empty());cases++;}
 assert(cases==18);return 0;
}
''')
 obj=tmp_path/'store.o';subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I'+str(TAG/'firmware/src'),'-c',str(TAG/'firmware/src/remote_pair_store.c'),'-o',str(obj)],check=True)
 exe=tmp_path/'matrix';libs=[CACHE/n for n in ['library/libmbedcrypto.a','3rdparty/everest/libeverest.a','3rdparty/p256-m/libp256m.a']]
 subprocess.run(['g++','-std=c++17','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-no-pie','-DWARP_REMOTE_CRYPTO_HOST=1','-I'+str(MBED/'include'),'-I'+str(S/'src/modules/tesla_vehicle'),'-I'+str(TAG/'firmware/src'),str(p),str(obj),str(S/'src/modules/tesla_vehicle/remote_crypto.cpp'),*map(str,libs),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
