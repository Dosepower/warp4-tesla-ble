from pathlib import Path
import subprocess
S=Path(__file__).resolve().parents[2]


def test_remote_scan_is_bounded_filtered_and_lease_survives_callback_and_cancel_barrier(tmp_path):
 p=tmp_path/'scan.cpp'
 p.write_text('''#include <cassert>
#include <vector>
#include "tesla_nimble_central.h"
std::vector<TeslaNimbleEvent::Kind> events;
static TeslaNimbleCentral *central;
void receive(const TeslaNimbleEvent&e){
 events.push_back(e.kind);assert(!central->idle());
 if(e.kind==TeslaNimbleEvent::RemoteCandidate){assert(e.size==7&&e.data[0]==1&&e.data[6]==1);}
}
int main(){
 TeslaNimbleCentral c;central=&c;assert(c.initialize(nullptr));ble_hs_cfg.sync_cb();
 assert(c.scan_remote(1,receive));assert(c.remote_reserved()&&!c.idle());
 uint8_t address[6]={1,2,3,4,5,6};assert(!c.open(address,0,1));
 assert(!c.scan_remote(2,receive));assert(test_scan_params.passive);
 uint8_t data[]={17,7,0xde,0x53,0x42,0x9a,0xa1,0x76,0x4f,0x7c,0xa0,0x3e,0x72,0x57,0x11,1,0x65,0x9d};
 ble_gap_event e{};e.type=BLE_GAP_EVENT_DISC;e.disc.addr.type=1;e.disc.addr.val[5]=1;e.disc.data=data;e.disc.length_data=sizeof(data);
 e.disc.event_type=BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND;test_scan(&e,test_scan_arg);assert(events.empty());
 e.disc.event_type=BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;test_scan(&e,test_scan_arg);assert(events.size()==1);
 e.type=BLE_GAP_EVENT_DISC_COMPLETE;test_scanning=false;test_scan(&e,test_scan_arg);
 assert(c.idle()&&c.remote_reserved()&&events.back()==TeslaNimbleEvent::RemoteScanDone);
 assert(c.release_remote());assert(c.scan_remote(2,receive));
 assert(c.cancel_remote_scan());assert(!c.idle());
 test_run_npl_events();assert(c.idle());assert(c.release_remote());
 assert(c.scan_remote(8,receive,true));
 events.clear();e.type=BLE_GAP_EVENT_DISC;test_scan(&e,test_scan_arg);assert(events.empty());
 data[14]=0x14;test_scan(&e,test_scan_arg);assert(events.size()==1);
 assert(c.cancel_remote_scan());test_run_npl_events();assert(c.release_remote());
 assert(c.scan_remote(9,receive));events.clear();test_scan(&e,test_scan_arg);assert(events.empty());
 assert(c.cancel_remote_scan());test_run_npl_events();assert(c.release_remote());
 assert(c.open(address,0,3));assert(!c.scan_remote(4,receive));
 ble_gap_event fail{};fail.type=BLE_GAP_EVENT_CONNECT;fail.connect.status=13;test_gap(&fail,test_gap_arg);
 assert(c.shutdown()&&c.deinitialize());
 return 0;
}
''')
 exe=tmp_path/'scan'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-pthread',
                 '-fsanitize=address,undefined','-no-pie','-DTESLA_NIMBLE_HOST_TEST=1','-DTESLA_BLE_USE_NIMBLE=1',
                 '-I'+str(S/'tests/tesla_vehicle/host'),'-I'+str(S/'src/modules/tesla_vehicle'),
                 str(p),str(S/'src/modules/tesla_vehicle/tesla_nimble_central.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
