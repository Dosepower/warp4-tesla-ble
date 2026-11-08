from pathlib import Path
import os
import subprocess
S=Path(__file__).resolve().parents[2]

def test_shared_peer_wire_and_protocol_headers_are_identical():
 tag=Path(os.environ.get('REMOTE_TEST_TAG_ROOT', S.parents[1]/'tag'))/'firmware/src'
 for name in ['remote_crypto.h','remote_enrollment.h','remote_enrollment_link.h','remote_enrollment_port.h','remote_fragments.h','remote_recovery_link.h','remote_session_link.h','remote_operation_journal.h']:
  assert (tag/name).read_bytes()==(S/'src/modules/tesla_vehicle'/name).read_bytes(),name

def test_attempt_bound_fragmentation_rejects_interleave_truncation_and_changed_duplicate(tmp_path):
 p=tmp_path/'frag.cpp'
 p.write_text('''#include <cassert>
#include <cstring>
#include "remote_fragments.h"
int main(){
 RemoteFragments rx;uint8_t hello[96],frame[20];for(unsigned i=0;i<96;i++)hello[i]=i;
 size_t offset=0;while(offset<96){size_t n=RemoteFragments::encode(7,1,hello,96,offset,frame);assert(n>0&&n<=20);assert(rx.feed(frame,n,7)!=RemoteFragments::Invalid);offset+=n-6;}
 assert(rx.complete()&&rx.size()==96&&!memcmp(rx.data(),hello,96));
 auto n=RemoteFragments::encode(7,1,hello,96,0,frame);assert(rx.feed(frame,n,7)==RemoteFragments::Duplicate);
 frame[6]^=1;assert(rx.feed(frame,n,7)==RemoteFragments::Invalid);
 rx.reset();RemoteFragments::encode(7,1,hello,96,0,frame);assert(rx.feed(frame,n,8)==RemoteFragments::Invalid);
 rx.reset();assert(rx.feed(frame,n,7)==RemoteFragments::Partial);
 frame[2]=2;assert(rx.feed(frame,n,7)==RemoteFragments::Invalid);
 rx.reset();RemoteFragments::encode(7,1,hello,96,14,frame);assert(rx.feed(frame,20,7)==RemoteFragments::Invalid);
 rx.reset();RemoteFragments::encode(7,1,hello,96,0,frame);frame[4]=97;assert(rx.feed(frame,20,7)==RemoteFragments::Invalid);
 return 0;
}
''')
 exe=tmp_path/'frag';subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-no-pie','-I'+str(S/'src/modules/tesla_vehicle'),str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
