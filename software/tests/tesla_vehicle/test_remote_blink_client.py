from pathlib import Path
import os,subprocess
S=Path(__file__).resolve().parents[2]


def test_real_shared_host_client_only_acknowledges_matching_physical_click(tmp_path):
    source=tmp_path/'client.cpp'
    source.write_text('''#include <cassert>
#include "remote_blink_client.h"
static void nimble_force_disconnect(){ble_gap_event e{};e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.conn_handle=4;test_gap(&e,test_gap_arg);}
int main(){
 TeslaNimbleCentral central;assert(central.initialize(nullptr));ble_hs_cfg.sync_cb();
 RemoteBlinkClient client;
 uint8_t address[]={1,2,3,4,5,6},token[16]={42};
 assert(client.begin(central,100,address,0,token));
 auto gen=client.generation();assert(!client.begin(central,101,address,0,token));
 ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect.conn_handle=4;test_gap(&e,test_gap_arg);
 client.poll(110);assert(client.phase()==RemoteBlinkClient::Discovering);
 ble_gatt_error ok{},done{BLE_HS_EDONE};ble_gatt_svc svc{1,40};
 test_service(4,&ok,&svc,test_proc_arg);test_service(4,&done,nullptr,test_proc_arg);
 auto uuid=[](uint8_t suffix){ble_uuid_any_t u{};u.u.type=128;const uint8_t a[16]={0xde,0x53,0x42,0x9a,0xa1,0x76,0x4f,0x7c,0xa0,0x3e,0x72,0x57,suffix,1,0x65,0x9d};memcpy(u.value,a,16);return u;};
 ble_gatt_chr tx{9,10,8,uuid(0x12)},rx{19,20,16,uuid(0x13)};
 test_char(4,&ok,&tx,test_proc_arg);test_char(4,&ok,&rx,test_proc_arg);test_char(4,&done,nullptr,test_proc_arg);
 ble_gatt_dsc d{};d.handle=21;d.uuid.u.type=16;d.uuid.value[0]=2;d.uuid.value[1]=0x29;
 test_descriptor(4,&ok,20,&d,test_proc_arg);test_descriptor(4,&done,20,nullptr,test_proc_arg);
 client.poll(120);assert(client.phase()==RemoteBlinkClient::Subscribing);
 ble_gatt_attr attr{21};test_written(4,&ok,&attr,test_proc_arg);client.poll(130);
 assert(test_write.size()==17&&test_write[0]==1&&test_write[1]==42);
 attr.handle=10;test_written(4,&ok,&attr,test_proc_arg);client.poll(140);
 assert(client.phase()==RemoteBlinkClient::WaitingClick);
 os_mbuf bad{{2,43,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}};
 e={};e.type=BLE_GAP_EVENT_NOTIFY_RX;e.notify_rx.conn_handle=4;e.notify_rx.attr_handle=20;e.notify_rx.om=&bad;test_gap(&e,test_gap_arg);client.poll(150);
 assert(client.phase()==RemoteBlinkClient::WaitingClick&&!client.consent_received());
 bad.bytes[1]=42;test_gap(&e,test_gap_arg);client.poll(160);
 assert(test_write[0]==3&&test_write[1]==42&&!client.consent_received());
 attr.handle=10;test_written(4,&ok,&attr,test_proc_arg);client.poll(170);
 assert(client.consent_received()); // provisional consent only, NEVER binding success
 nimble_force_disconnect();client.poll(180);
 assert(central.idle()&&!central.remote_reserved()&&client.phase()==RemoteBlinkClient::Complete);
 assert(client.begin(central,200,address,0,token)&&client.generation()!=gen);
 client.cancel();client.poll(300);
 e={};e.type=BLE_GAP_EVENT_CONNECT;e.connect.status=13;test_gap(&e,test_gap_arg);client.poll(310);
 assert(!central.remote_reserved()&&client.phase()==RemoteBlinkClient::Failed);
 assert(central.shutdown()&&central.deinitialize());
 return 0;
}
''')
    exe=tmp_path/'client'
    flags=['-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-pthread',
           '-fsanitize=address,undefined','-no-pie','-DTESLA_NIMBLE_HOST_TEST=1','-DTESLA_BLE_USE_NIMBLE=1',
           '-I'+str(S/'tests/tesla_vehicle'),'-I'+str(S/'tests/tesla_vehicle/host'),
           '-I'+str(S/'src/modules/tesla_vehicle')]
    subprocess.run(['g++',*flags,str(source),str(S/'src/modules/tesla_vehicle/tesla_nimble_central.cpp'),
                    str(S/'src/modules/tesla_vehicle/remote_blink_client.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
