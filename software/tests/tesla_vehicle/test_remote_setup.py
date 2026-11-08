from pathlib import Path
import subprocess
S=Path(__file__).resolve().parents[2]


def test_setup_controller_real_scan_selection_blink_receipt_disable_and_no_trust(tmp_path):
 p=tmp_path/'setup.cpp'
 p.write_text('''#include <cassert>
#include "remote_setup.h"
int main(){
 TeslaNimbleCentral c;assert(c.initialize(nullptr));ble_hs_cfg.sync_cb();
 RemoteSetup setup;uint8_t nonce[16]={8};
 assert(setup.start(c,100));assert(setup.busy());assert(!setup.start(c,101));
 uint8_t data[]={17,7,0xde,0x53,0x42,0x9a,0xa1,0x76,0x4f,0x7c,0xa0,0x3e,0x72,0x57,0x11,1,0x65,0x9d};
 ble_gap_event e{};e.type=BLE_GAP_EVENT_DISC;e.disc.addr.type=1;e.disc.addr.val[5]=1;e.disc.data=data;e.disc.length_data=sizeof(data);
 test_scan(&e,test_scan_arg);setup.poll(200);assert(setup.count()==1);
 assert(!setup.select(201,setup.generation()-1,0,nonce));
 assert(setup.select(201,setup.generation(),0,nonce));assert(!setup.select(202,setup.generation(),0,nonce));
 assert(test_connects==0);test_run_npl_events();setup.poll(210);assert(test_connects==1);
 setup.cancel();setup.poll(220);
 e={};e.type=BLE_GAP_EVENT_CONNECT;e.connect.status=13;test_gap(&e,test_gap_arg);setup.poll(230);
 assert(!setup.busy()&&!c.remote_reserved()&&setup.count()==0);
 assert(setup.start(c,1000));setup.poll(31000);test_run_npl_events();setup.poll(31001);
 assert(!setup.busy()&&setup.phase()==RemoteSetup::Expired);
 assert(setup.start(c,32000));
 e={};e.type=BLE_GAP_EVENT_DISC;e.disc.addr.type=1;e.disc.addr.val[5]=1;e.disc.data=data;e.disc.length_data=sizeof(data);
 test_scan(&e,test_scan_arg);setup.poll(32001);test_api_rc=7;
 assert(!setup.select(32002,setup.generation(),0,nonce));assert(setup.busy());
 test_api_rc=0;setup.poll(32003);test_run_npl_events();setup.poll(32004);
 assert(!setup.busy()&&!c.remote_reserved());
 assert(c.shutdown()&&c.deinitialize());
 return 0;
}
''')
 exe=tmp_path/'setup'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-pthread',
                 '-fsanitize=address,undefined','-no-pie','-DTESLA_NIMBLE_HOST_TEST=1','-DTESLA_BLE_USE_NIMBLE=1',
                 '-I'+str(S/'tests/tesla_vehicle/host'),'-I'+str(S/'src/modules/tesla_vehicle'),
                 str(p),*[str(S/'src/modules/tesla_vehicle'/n) for n in ['tesla_nimble_central.cpp','remote_blink_client.cpp','remote_setup.cpp']],'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
