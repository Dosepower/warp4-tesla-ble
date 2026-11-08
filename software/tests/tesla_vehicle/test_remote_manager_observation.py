from pathlib import Path
import subprocess
S=Path(__file__).resolve().parents[2]


def test_requested_mode_is_not_manager_commit_and_stale_echo_cannot_ack(tmp_path):
    source=tmp_path/'commit.cpp'
    source.write_text('''#include <cassert>
#include "manager_mode_observation.h"
int main(){
 ManagerModeObservation s;
 assert(!s.fresh(0));
 s.observe(100,2,true);assert(s.fresh(100)&&s.mode()==2);
 auto before=s.revision();
 /* API request mutates a different config object. Observer remains PV. */
 unsigned requested=0;(void)requested;
 assert(!s.acknowledges(before,101,0));
 s.observe(102,4,true);assert(!s.fresh(102)); // Default is not an applied mode
 s.observe(103,2,true);assert(!s.acknowledges(before,103,0));
 before=s.revision();s.observe(104,0,true);
 assert(s.acknowledges(before,104,0));
 assert(!s.acknowledges(before,5105,0));
 s.observe(6000,0,false);assert(!s.fresh(6000));
 s.observe(7000,99,true);assert(!s.fresh(7000));
 return 0;
}
''')
    exe=tmp_path/'commit'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-no-pie',
                    '-I'+str(S/'src/modules/evse_common'),str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
