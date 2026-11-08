from pathlib import Path
import subprocess
S=Path(__file__).resolve().parents[2]

def test_verified_operation_intent_never_reissues_after_restart(tmp_path):
 p=tmp_path/'journal.cpp';p.write_text('''#include <cassert>
#include <cstring>
#include <vector>
#include "remote_operation_journal.h"
struct Disk{std::vector<uint8_t> data;unsigned writes=0,fail=0;bool after=false,poison=false;};
int read(void*p,uint8_t*out,size_t n){auto&d=*(Disk*)p;if(d.poison)return -1;if(d.data.empty())return 0;if(d.data.size()!=n)return -1;memcpy(out,d.data.data(),n);return 1;}
bool write(void*p,const uint8_t*in,size_t n){auto&d=*(Disk*)p;bool bad=++d.writes==d.fail;if(bad&&!d.after)return false;d.data.assign(in,in+n);if(bad)d.poison=true;return true;}
int main(){using J=RemoteOperationJournal;Disk d;uint8_t id[32]={7},other[32]={8};J j({&d,read,write});assert(j.load());
 assert(j.begin(id,1,3)==J::Fresh);assert(j.find(id)->phase==1&&d.writes==1);
 J reboot({&d,read,write});assert(reboot.load()&&reboot.begin(id,1,3)==J::Existing&&d.writes==1);
 assert(reboot.begin(id,1,4)==J::Rejected&&reboot.begin(id,0,3)==J::Rejected&&reboot.begin(id,2,3)==J::Rejected);
 assert(j.issue(id,1)&&d.writes==2);assert(!j.issue(id,1)&&d.writes==2);
 J issued({&d,read,write});assert(issued.load()&&issued.begin(id,1,3)==J::Existing&&issued.find(id)->phase==2&&d.writes==2);
 assert(j.finish(id,1,1)&&d.writes==3);assert(!j.finish(id,1,1));
 J done({&d,read,write});assert(done.load()&&done.begin(id,1,3)==J::Existing&&done.find(id)->outcome==1&&d.writes==3);
 assert(done.begin(id,2,3)==J::Fresh&&done.begin(id,1,3)==J::Rejected);
 J single({&d,read,write},1);assert(single.load()&&single.begin(other,1,3)==J::Rejected);
 assert(!single.retire(id,false));assert(single.retire(id,true)&&single.begin(other,1,3)==J::Fresh);
 for(unsigned phase=1;phase<=3;phase++)for(bool after:{false,true}){
  Disk f;J x({&f,read,write});assert(x.load());if(phase>1)assert(x.begin(id,1,3)==J::Fresh);if(phase>2)assert(x.issue(id,1));
  f.fail=phase;f.after=after;bool ok=phase==1?x.begin(id,1,3)==J::Fresh:phase==2?x.issue(id,1):x.finish(id,1,1);assert(!ok&&x.quarantined());
  f.fail=0;f.poison=false;J restored({&f,read,write});assert(restored.load());if(!f.data.empty())assert(restored.begin(id,1,3)==J::Existing); // never fresh/reissued after an uncertain write
 }
 Disk isolated;J multi({&isolated,read,write});assert(multi.load()&&multi.begin(id,1,3)==J::Fresh&&multi.begin(other,9,3)==J::Fresh);assert(multi.retire(id,true)&&!multi.find(id)&&multi.find(other)->operation==9&&multi.find(other)->phase==1);
 Disk broken;broken.data.assign(J::WIRE_SIZE,0);J invalid({&broken,read,write});assert(!invalid.load()&&invalid.quarantined());
}
''')
 exe=tmp_path/'journal';subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-no-pie','-I'+str(S/'src/modules/tesla_vehicle'),str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
