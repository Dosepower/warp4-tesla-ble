"""Execute the actual EV identity publication seam with injected/SLAC inputs."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_iram_permanent_allocation import function

S = Path(__file__).resolve().parents[2]

class SLACIdentity(unittest.TestCase):
    def test_injected_profile_is_not_physical_slac(self):
        body = function((S / 'src/modules/ev/ev.h').read_text(), 'void publish_live_identity()')
        source = '''#include <atomic>
#include <cstdint>
#include <cassert>
struct Ev {
 uint64_t session_generation=1, active_ev_mac=0x112233445500ULL;
 int active_ev_index=0;
 bool active_ev_injected=false;
 std::atomic<uint32_t> live_revision{1};
 std::atomic<uint64_t> live_generation{0}, live_mac{0};
 std::atomic<int> live_index{-1};
''' + body + '''};
int main() {
 Ev ev; ev.publish_live_identity();
 assert(ev.live_generation==1 && ev.live_index==0 && ev.live_mac==ev.active_ev_mac);
 ev.live_revision=1; ev.active_ev_injected=true; ev.publish_live_identity();
 assert(ev.live_index==-1 && ev.live_mac==0 && "Injected EV is not physical SLAC authority");
 assert(ev.active_ev_index==0 && ev.active_ev_mac!=0);
}
'''
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as td:
            p=Path(td); (p/'test.cpp').write_text(source)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__ == '__main__':
    unittest.main()
