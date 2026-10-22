"""Bounded production sampler/serializer: current-task only, no allocations."""
from pathlib import Path
import subprocess,tempfile,os,sys,json
S=Path(__file__).resolve().parents[2]
code=r'''
#include <cassert>
#include <atomic>
#include <cstdlib>
#include <new>
#include <thread>
#include <cstdio>
static std::atomic<unsigned> allocations{0};
void *operator new(std::size_t n){++allocations;void *p=std::malloc(n);if(!p)std::abort();return p;}
void operator delete(void *p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
#include "bluetooth_memory.h"
int main(){
 using namespace bluetooth_memory;
 static_assert(sizeof(Sample)==40,"Account instrumentation overhead explicitly");
 Sample out{};assert(!read(Stage::Count,out));
 assert(read(Stage::Boot,out)&&out.samples==0);
 auto before=allocations.load();
 for(unsigned i=0;i<200;++i)sample(Stage::WorkerActive,true);
 assert(allocations==before);
 assert(read(Stage::WorkerActive,out));assert(out.samples==200);assert(out.stack_min==2048);
 assert(out.free_internal==101 && out.largest_internal==103 && out.min_internal==102);
 assert(out.free_dma==104 && out.largest_dma==105 && out.free_psram==106 && out.largest_psram==107);
 // Main samples do not invent a watermark for a worker that does not exist.
 sample(Stage::Boot,false);assert(read(Stage::Boot,out)&&out.stack_min==0);
 char tiny[1];assert(!json(Stage::WorkerActive,tiny,sizeof(tiny)));
 char buffer[512];assert(json(Stage::WorkerActive,buffer,sizeof(buffer)));puts(buffer);
 // Fixed scalar copies remain coherent under concurrent writers/readers.
 std::thread a([]{for(int i=0;i<10000;++i)sample(Stage::BtActive,true);});
 std::thread b([]{for(int i=0;i<10000;++i){Sample v{};assert(read(Stage::BtActive,v));if(v.samples){assert(v.free_internal==101&&v.largest_psram==107&&v.stack_min==2048);}}});
 a.join();b.join();
 assert(read(Stage::BtActive,out)&&out.samples==10000);
 // No handles retained: sampling/retrieval after every simulated task teardown
 // is just scalars. The fake API asserts the only sampled handle is nullptr.
 assert(json(Stage::BtActive,buffer,sizeof(buffer)));puts(buffer);
}
'''
with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR'],prefix='memory-phase-') as td:
 d=Path(td);(d/'test.cpp').write_text(code)
 cmd=['g++','-std=c++17','-Wall','-Wextra','-Werror','-pthread','-DWARP_TESLA_MEMORY_PHASES=1','-DTESLA_MEMORY_HOST_TEST=1','-I'+str(S/'src/modules/tesla_vehicle'),'-I'+str(S/'tests/tesla_vehicle/host'),str(d/'test.cpp'),'-o',str(d/'test')]
 if 'sanitize' in sys.argv:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
 subprocess.run(cmd,check=True);r=subprocess.run([str(d/'test')],text=True,stdout=subprocess.PIPE,check=True)
 rows=[json.loads(x) for x in r.stdout.splitlines()];assert len(rows)==2 and rows[0]['samples']==200 and rows[1]['samples']==10000
 print(r.stdout);print('Phase heap/current-stack concurrency/no-allocation contracts PASS')
