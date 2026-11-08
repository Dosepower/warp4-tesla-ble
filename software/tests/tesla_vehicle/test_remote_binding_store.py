from pathlib import Path
import subprocess
S=Path(__file__).resolve().parents[2]


def test_four_slot_collection_readback_partial_enrollment_and_counter_reservation(tmp_path):
 p=tmp_path/'bindings.cpp'
 p.write_text('''#include <cassert>
#include <cstring>
#include <vector>
#include "remote_binding_store.h"
struct Disk{std::vector<uint8_t> bytes;bool fail=false,corrupt=false;};
int read(void *p,uint8_t*out,size_t n){auto&d=*static_cast<Disk*>(p);if(d.bytes.empty())return 0;if(d.bytes.size()!=n)return -1;memcpy(out,d.bytes.data(),n);if(d.corrupt)out[n-1]^=1;return 1;}
bool write(void*p,const uint8_t*b,size_t n){auto&d=*static_cast<Disk*>(p);if(d.fail)return false;d.bytes.assign(b,b+n);return true;}
int main(){
 Disk disk;RemoteBindingStore s({&disk,read,write});assert(s.load()&&s.count()==0);
 uint8_t peer[32]={1},key[32]={2};
 assert(s.prepare(peer,key,"Tag A"));assert(s.count()==0&&s.prepared_count()==1);
 RemoteBindingStore reboot({&disk,read,write});assert(reboot.load()&&reboot.count()==0);
 assert(!reboot.prepare(peer,key,"replacement")); // never overwrite prepared peer
 assert(!reboot.activate(peer));assert(reboot.commit_prepared(peer,key));
 RemoteBindingStore committing({&disk,read,write});assert(committing.load()&&committing.count()==0&&committing.prepared_count()==1);
 assert(reboot.activate(peer));assert(reboot.count()==1);
 assert(!reboot.prepare(peer,key,"replacement"));
 uint64_t sequence;assert(reboot.reserve(peer,sequence)&&sequence==1);
 RemoteBindingStore again({&disk,read,write});assert(again.load());assert(again.reserve(peer,sequence)&&sequence==2);
 for(unsigned i=2;i<=4;i++){peer[0]=i;assert(again.prepare(peer,key,"more"));assert(again.commit_prepared(peer,key));assert(again.activate(peer));}
 peer[0]=5;assert(!again.prepare(peer,key,"full"));
 peer[0]=2;assert(again.forget(peer));assert(again.count()==3);
 peer[0]=1;assert(again.find(peer)&&again.find(peer)->sequence==2);
 disk.fail=true;assert(!again.reserve(peer,sequence));assert(again.quarantined());
 disk.fail=false;RemoteBindingStore saved({&disk,read,write});assert(saved.load()&&saved.find(peer)->sequence==2);
 disk.corrupt=true;assert(!saved.reserve(peer,sequence)&&saved.quarantined());
 return 0;
}
''')
 exe=tmp_path/'bindings'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-no-pie',
                 '-I'+str(S/'src/modules/tesla_vehicle'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
