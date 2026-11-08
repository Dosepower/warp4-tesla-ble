from pathlib import Path
import subprocess
S=Path(__file__).resolve().parents[2]

def test_authenticated_soc_requires_exact_live_generation_and_receive_time(tmp_path):
 p=tmp_path/'telemetry.cpp';p.write_text('''#include <cassert>
#include <cmath>
#include "remote_telemetry.h"
int main(){RemoteSocObservation s;unsigned value=0;uint32_t age=0;
 assert(!s.read(100,1,0,9,value,age));
 s.observe(100,1,0,9,false,true,45);assert(!s.read(101,1,0,9,value,age));
 s.observe(100,1,0,9,true,false,45);assert(!s.read(101,1,0,9,value,age));
 s.observe(100,1,0,9,true,true,45);assert(s.read(5100,1,0,9,value,age)&&value==45&&age==5000);
 assert(!s.read(5101,1,0,9,value,age));assert(!s.read(99,1,0,9,value,age));
 assert(!s.read(101,2,0,9,value,age));assert(!s.read(101,1,1,9,value,age));assert(!s.read(101,1,0,10,value,age));
 s.observe(200,2,1,10,true,true,101);assert(!s.read(200,2,1,10,value,age));
 s.observe(200,2,1,10,true,true,NAN);assert(!s.read(200,2,1,10,value,age));
 s.observe(200,2,1,0,true,true,45);assert(!s.read(200,2,1,0,value,age));
 return 0;}
''')
 exe=tmp_path/'telemetry';subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-no-pie','-I'+str(S/'src/modules/tesla_vehicle'),str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
