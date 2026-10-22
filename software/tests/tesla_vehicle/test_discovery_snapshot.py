"""Deterministic contention and injected-clock TTL checks, no timing sleeps."""
from pathlib import Path
import os, subprocess, tempfile, unittest
S=Path(__file__).resolve().parents[2]
class DiscoverySnapshot(unittest.TestCase):
    def test_contended_publication_and_exact_ttl(self):
        code=r'''
#include <atomic>
#include <mutex>
#include <thread>
#include <future>
#include <cassert>
#include <cstdint>
#include <cstring>
std::promise<void> *snapshot_lock_attempt=nullptr;
#define private public
#include "tesla_ble_discovery.h"
#undef private
int main() {
 using D=TeslaBleDiscovery;D d;D::Scope scope;scope.index=0;scope.mac=1;scope.generation=7;
 strcpy(scope.name,"S0000000000000000C");
 assert(d.request(100,scope));assert(d.step(100)==D::Action::Params);
 d.event(D::Event::Params,true);assert(d.step(100)==D::Action::Start);
 d.event(D::Event::Start,true);
 uint8_t adv[20]={19,9};memcpy(adv+2,scope.name,18);uint8_t addr[6]={1,2,3,4,5,6};
 d.observe(adv,20,0,addr,1,-50,0);d.event(D::Event::Done,true);d.step(100);
 D::Snapshot s;bool accepted=false;std::promise<void> entering;auto ready=entering.get_future();
 // The worker owns the discovery lock when main attempts publication.
 std::unique_lock<std::mutex> held(d.mutex);
 snapshot_lock_attempt=&entering;
 std::thread main_task([&]{accepted=d.snapshot(s);});
 ready.wait();held.unlock();main_task.join();snapshot_lock_attempt=nullptr;
 assert(accepted && s.count==1 && s.status==D::Status::Complete);
 assert(d.matches(100+D::RESULT_TTL_MS-1,scope,s.revision,addr,1));
 assert(!d.matches(100+D::RESULT_TTL_MS,scope,s.revision,addr,1));
 auto changed=scope;++changed.generation;
 assert(!d.matches(100,changed,s.revision,addr,1));
 assert(!d.matches(100,scope,s.revision+1,addr,1));
 d.step(100+D::RESULT_TTL_MS);assert(d.snapshot(s)&&s.count==0&&s.status==D::Status::Idle);
}
'''
        # Observe the REAL try_lock result before releasing the writer lock.
        # For blocking lock, signal entry then wait for the explicit release.
        # No lock outcome, clock value or returned snapshot is substituted.
        header=(S/'src/modules/tesla_vehicle/tesla_ble_discovery.h').read_text()
        header=header.replace('held=owner.mutex.try_lock();','held=owner.mutex.try_lock();if(snapshot_lock_attempt) { snapshot_lock_attempt->set_value(); }')
        header=header.replace('owner.mutex.lock();','if(snapshot_lock_attempt) { snapshot_lock_attempt->set_value(); }owner.mutex.lock();')
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as td:
            p=Path(td);(p/'test.cpp').write_text(code);(p/'tesla_ble_discovery.h').write_text(header)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pthread','-I'+td,str(p/'test.cpp'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
