import os
"""Exercise actual WARP API -> shared NimBLE -> Tag GATT/input -> receipt.
Only radio/RTOS/storage boundaries are synthetic; neither peer is a protocol fake.
"""
from pathlib import Path
import subprocess,os
S=Path(__file__).resolve().parents[2];TAG=Path(os.environ.get('REMOTE_TEST_TAG_ROOT', S.parents[1]/'tag'))
MBED=Path(os.environ.get('REMOTE_TEST_MBEDTLS_SOURCE', '.remote-test-deps/remote_test_mbedtls_source'))
CACHE=Path(os.environ.get('REMOTE_TEST_MBEDTLS_BUILD', '.remote-test-deps/remote_test_mbedtls_build'))


def test_actual_native_api_to_tag_consent_never_saves_trust(tmp_path):
 _run_peer(tmp_path,False)

def test_actual_api_shared_nimble_tag_gatt_secure_enrollment(tmp_path):
 _run_peer(tmp_path,True)

def _run_peer(tmp_path,secure):
 for name in ['kernel.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h','bluetooth/hci.h','bluetooth/uuid.h']:
  p=tmp_path/'zephyr'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "host_remote_platform.h"\n')
 bridge=tmp_path/'tag.c'
 bridge.write_text('''#include "host_remote_platform.h"
#include "remote_transport.c"
#include "remote_input.h"
static struct remote_input input;
static struct bt_conn link={2,1,0};
static uint32_t generation;
void tag_tick(unsigned long long now,int pressed){
 test_now=now;remote_transport_poll(now);
 uint32_t current=remote_transport_generation();if(current!=generation){remote_input_cancel(&input,now);generation=current;}
 remote_transport_gesture(now,remote_input_step(&input,now,pressed,remote_transport_context()));
}
void tag_init(void){assert(remote_transport_init(NULL)==0);remote_input_init(&input);generation=remote_transport_generation();}
void tag_connect(void){connected(&link,0);}
int tag_write(const unsigned char *data,unsigned n){return (int)write_request(&link,NULL,data,n,0,0);}
unsigned tag_notifications(unsigned char *out){memcpy(out,test_response,17);return test_notifies;}
int tag_blue(unsigned long long now){return remote_transport_blue(now);}
void tag_disconnect(void){disconnected(&link,0);}
''')
 if secure:
  bridge.write_text(bridge.read_text().replace('#include "remote_transport.c"','#undef REMOTE_SECURE_PREVIEW_FIXTURE\n#include "remote_transport.c"').replace('memcpy(out,test_response,17);return test_notifies;','memcpy(out,test_response,test_response_size);return test_notifies;')+'\nunsigned tag_notification_size(void){return test_response_size;}\nvoid tag_poll(unsigned long long now){test_now=now;remote_transport_poll(now);}\nint tag_unavailable(void){return ui.phase==REMOTE_UI_UNAVAILABLE&&!actual_mode.valid&&!actual_car.soc_valid&&!actual_car.power_valid;}\nint tag_mode_pv(void){return ui.phase==REMOTE_UI_MODE&&actual_mode.valid&&actual_mode.mode==REMOTE_MODE_PV&&!ui.pending;}\n')
 obj=tmp_path/'tag.o'
 if secure:
  with bridge.open('a') as f:f.write('''\nvoid tag_diagnostic_request(unsigned long long now){test_now=now;open_normal(now,3);}
unsigned tag_capabilities(void){return actual_mode_capabilities;}
int tag_reject_unsupported(unsigned long long now){test_now=now;remote_transport_poll(now);remote_transport_gesture(now,REMOTE_INPUT_SHORT);return ui.phase==REMOTE_UI_UNAVAILABLE&&!ui.pending;}
void tag_shake_request(unsigned long long now){test_now=now;remote_transport_shake(now);}
int tag_partial_car(void){return actual_car.authenticated&&actual_car.soc_valid&&actual_car.soc_percent==67&&actual_car.power_valid&&actual_car.watts==4200&&!actual_car.max_watts&&ui.phase==REMOTE_UI_UNAVAILABLE;}
''')
 subprocess.run(['cc','-std=c11','-g','-fsanitize=address,undefined','-I'+str(tmp_path),'-I'+str(TAG/'tests'),'-I'+str(TAG/'firmware/src'),'-c',str(bridge),'-o',str(obj)],check=True)
 source=tmp_path/'e2e.cpp'
 source.write_text('''#include "module_platform.h"
#include <fstream>
#include <iterator>
#include "'''+str(S/'src/modules/tesla_vehicle/tesla_vehicle.cpp')+'''"
#include "host/nimble_fixture.h"
extern "C" {void tag_init(void);void tag_tick(unsigned long long,int);void tag_connect(void);int tag_write(const unsigned char*,unsigned);unsigned tag_notifications(unsigned char*);int tag_blue(unsigned long long);void tag_disconnect(void);}
struct TeslaBleModuleTest {
 static void run(){
 TeslaVehicle m;m.pre_setup();m.setup();m.register_urls();
 assert(m.remote_setup.set_enrollment({})); // explicit untrusted preview fixture only
 m.bluetooth_config.get("enable")->updateBool(true);
 HostNimbleDriver driver(m.bluetooth,TeslaVehicle::gatt_callback);
 tag_init();tag_tick(0,0);
 for(unsigned i=0;i<3;i++){unsigned t=100+200*i;tag_tick(t,1);tag_tick(t+25,1);tag_tick(t+75,0);tag_tick(t+100,0);}
 tag_tick(1000,0);clock_ms=1000;
 String error;m.remote_update.get("enable")->updateBool(true);api.commands.at("bluetooth/remote_config_update")(0,error);assert(!error.length());
 assert(test_connects==0);api.commands.at("bluetooth/remote_start")(0,error);assert(!error.length());
 uint8_t ad[]={17,7,0xde,0x53,0x42,0x9a,0xa1,0x76,0x4f,0x7c,0xa0,0x3e,0x72,0x57,0x11,1,0x65,0x9d};
 ble_gap_event advertisement{};advertisement.type=BLE_GAP_EVENT_DISC;advertisement.disc.data=ad;advertisement.disc.length_data=sizeof(ad);advertisement.disc.addr.val[5]=1;
 test_scan(&advertisement,test_scan_arg);m.remote_loop();
 assert(m.remote_candidates.get("entries")->count()==1);
 auto uuid=[](uint8_t suffix){ble_uuid_any_t u{};u.u.type=128;const uint8_t v[16]={0xde,0x53,0x42,0x9a,0xa1,0x76,0x4f,0x7c,0xa0,0x3e,0x72,0x57,suffix,1,0x65,0x9d};memcpy(u.value,v,16);return u;};
 test_submit=[&](int stage){
  ble_gatt_error ok{},done{BLE_HS_EDONE};
  if(stage==1){tag_connect();nimble_complete_setup();}
  if(stage==2){ble_gatt_svc svc{1,40};test_service(4,&ok,&svc,test_proc_arg);test_service(4,&done,nullptr,test_proc_arg);}
  if(stage==3){ble_gatt_chr tx{9,10,8,uuid(0x12)},rx{19,20,16,uuid(0x13)};test_char(4,&ok,&tx,test_proc_arg);test_char(4,&ok,&rx,test_proc_arg);test_char(4,&done,nullptr,test_proc_arg);}
  if(stage==4){ble_gatt_dsc d{};d.handle=21;d.uuid.u.type=16;d.uuid.value[0]=2;d.uuid.value[1]=0x29;test_descriptor(4,&ok,20,&d,test_proc_arg);test_descriptor(4,&done,20,nullptr,test_proc_arg);}
  if(stage==5){if(test_handle==10)assert(tag_write(test_write.data(),test_write.size())==17);ble_gatt_attr a{test_handle};test_written(4,&ok,&a,test_proc_arg);}
  if(stage==6){tag_disconnect();nimble_force_disconnect();}
 };
 m.remote_select.get("index")->updateUint(0);m.remote_select.get("generation")->updateUint(m.remote_setup.generation());
 api.commands.at("bluetooth/remote_select")(0,error);assert(!error.length());
 test_run_npl_events();for(unsigned i=0;i<6;i++)m.remote_loop();
 tag_tick(1200,0);assert(tag_blue(1200));tag_tick(1700,0);
 tag_tick(1800,1);tag_tick(1825,1);tag_tick(1875,0);tag_tick(1900,0);tag_tick(2300,0);tag_tick(2325,0);
 unsigned char response[17];assert(tag_notifications(response)==1&&response[0]==2);
 os_mbuf mb{{response,response+17}};ble_gap_event event{};event.type=BLE_GAP_EVENT_NOTIFY_RX;event.notify_rx.conn_handle=4;event.notify_rx.attr_handle=20;event.notify_rx.om=&mb;test_gap(&event,test_gap_arg);
 for(unsigned i=0;i<6;i++)m.remote_loop();
 assert(m.remote_setup.phase()==RemoteSetup::ConsentReceived);
 assert(m.remote_state.get("phase")->asString()=="consent_untrusted");
 assert(!m.remote_state.get("trusted")->asBool()&&m.remote_bindings.get("entries")->count()==0);
 assert(host_charge_calls==0&&reboot_calls==0&&m.bluetooth.central()->idle());
 test_submit={};
 }
};
int main(){TeslaBleModuleTest::run();puts("WARP API -> shared NimBLE -> actual Tag triple/blue/confirm -> matching receipt PASS; no trusted binding/charge");}
''')
 if secure:
  text=source.read_text().replace(' assert(m.remote_setup.set_enrollment({})); // explicit untrusted preview fixture only','')
  text=text.replace('struct TeslaBleModuleTest {','extern "C" {unsigned tag_notification_size(void);int tag_disk_phase(void);}\nstruct TeslaBleModuleTest {')
  text=text.replace('assert(tag_write(test_write.data(),test_write.size())==17)','assert(tag_write(test_write.data(),test_write.size())==(int)test_write.size())')
  text=text.replace('test_run_npl_events();for(unsigned i=0;i<6;i++)m.remote_loop();','test_run_npl_events();m.remote_loop();')
  start=text.index(' tag_tick(1200,0);');end=text.index(' test_submit={};',start)
  text=text[:start]+''' unsigned notified=0;
 auto step=[&](unsigned now,int pressed=0){
  clock_ms=now;tag_tick(now,pressed);
  unsigned char response[20];unsigned count=tag_notifications(response);
  if(count!=notified){
   assert(count==notified+1);notified=count;unsigned n=tag_notification_size();
   os_mbuf mb{{response,response+n}};ble_gap_event event{};event.type=BLE_GAP_EVENT_NOTIFY_RX;event.notify_rx.conn_handle=4;event.notify_rx.attr_handle=20;event.notify_rx.om=&mb;test_gap(&event,test_gap_arg);
  }
  m.remote_loop();
 };
 for(unsigned t=1100;t<1600;t+=25)step(t);
 assert(tag_blue(1600)&&!m.remote_state.get("trusted")->asBool()&&tag_disk_phase()==0);
 step(2100);step(2200,1);step(2225,1);step(2275,0);step(2300,0);step(2700,0);
 for(unsigned t=2725;t<4500;t+=25)step(t);
 fprintf(stderr,"secure E2E phase=%u tag_store=%d notifications=%u bindings=%u\\n",(unsigned)m.remote_setup.phase(),tag_disk_phase(),notified,m.remote_store.count());
 assert(m.remote_setup.phase()==RemoteSetup::Paired);
 assert(m.remote_state.get("phase")->asString()=="paired_tofu");
 assert(m.remote_state.get("trusted")->asBool()&&m.remote_bindings.get("entries")->count()==1&&tag_disk_phase()==3);
 assert(host_charge_calls==0&&reboot_calls==0&&m.bluetooth.central()->idle());
 assert(m.remote_store.count()==1&&m.remote_store.prepared_count()==0);
 auto id=m.remote_bindings.get("entries")->get(size_t(0))->get("id")->asString();
 m.remote_forget.get("id")->updateString(id);m.remote_forget.get("confirmed")->updateBool(true);
 api.commands.at("bluetooth/remote_forget")(0,error);assert(!error.length());m.remote_loop();
 assert(m.remote_store.count()==0&&!m.remote_state.get("trusted")->asBool());
 '''+text[end:]
  text=text.replace('matching receipt PASS; no trusted binding/charge','fragmented enrollment + durable verified mutual proofs PASS; no charge')
  text=text.replace('unsigned tag_notification_size(void);','unsigned tag_notification_size(void);void tag_poll(unsigned long long);')
  relay='''unsigned notified=0,receipt_drops=0;
 auto relay=[&](){
  unsigned char response[20];unsigned count=tag_notifications(response);
  if(count!=notified){assert(count==notified+1);notified=count;unsigned n=tag_notification_size();
   if(((test_fault==13&&response[2]==8&&receipt_drops<2)||(test_fault==31&&response[2]==20&&receipt_drops<3))){receipt_drops++;assert(!m.remote_state.get("trusted")->asBool());return;}
   os_mbuf mb{{response,response+n}};ble_gap_event event{};event.type=BLE_GAP_EVENT_NOTIFY_RX;event.notify_rx.conn_handle=4;event.notify_rx.attr_handle=20;event.notify_rx.om=&mb;test_gap(&event,test_gap_arg);if(test_fault==14||test_fault==32)test_gap(&event,test_gap_arg);}
 };
 '''
  text=text.replace(' unsigned notified=0;','')
  text=text.replace(' test_submit=[&](int stage){',' '+relay+'test_submit=[&](int stage){')
  # Deliberately deliver BOTH response fragments before the ATT write callback.
  # Real BLE peers can notify before the central main queue processes write ACK.
  text=text.replace('ble_gatt_attr a{test_handle};test_written','if(test_handle==10){tag_poll(clock_ms);relay();tag_poll(clock_ms);relay();}ble_gatt_attr a{test_handle};test_written')
  begin=text.index('  unsigned char response[20];unsigned count=tag_notifications(response);',text.index(' auto step='))
  end=text.index('  m.remote_loop();',begin)
  text=text[:begin]+'  relay();\n'+text[end:]
  text=text.replace('struct TeslaBleModuleTest {','extern "C" void tag_test_fail(int);static int test_fault;\nstruct TeslaBleModuleTest {')
  text=text.replace('int main(){TeslaBleModuleTest::run();','int main(int argc,char**argv){test_fault=argc>1?atoi(argv[1]):0;tag_test_fail(test_fault<=6?test_fault:test_fault==36?3:0);if(test_fault>6&&test_fault<=12){RemoteBindingBackend::fail_write_at=(test_fault-7)%3+1;RemoteBindingBackend::fail_after_write=test_fault>=10;}TeslaBleModuleTest::run();')
  text=text.replace('if(test_handle==10)assert(tag_write(test_write.data(),test_write.size())==(int)test_write.size());','if(test_handle==10){int rc=tag_write(test_write.data(),test_write.size());if(rc<0){ble_gatt_error err{1};ble_gatt_attr a{test_handle};test_written(4,&err,&a,test_proc_arg);return;}}')
  text=text.replace(' assert(m.remote_setup.phase()==RemoteSetup::Paired);',' if(test_fault&&test_fault<=12){for(unsigned t=4500;t<32000;t+=100)step(t);assert(!m.remote_state.get("trusted")->asBool()&&m.remote_store.count()==0&&m.remote_setup.phase()!=RemoteSetup::Paired);assert(host_charge_calls==0);test_submit={};return;}\n assert(m.remote_setup.phase()==RemoteSetup::Paired);')
  text=text.replace('tag_disk_phase()==0','(test_fault>=20||tag_disk_phase()==0)')
  text=text.replace('api.commands.at("bluetooth/remote_start")(0,error);assert(!error.length());', '''if(test_fault>=20){
   auto entries=m.remote_bindings.get("entries");assert(entries->count()==1);
   m.remote_recover.get("id")->updateString(entries->get(size_t(0))->get("id")->asString());
   m.remote_recover.get("confirmed")->updateBool(false);api.commands.at("bluetooth/remote_recover")(0,error);assert(error.length()&&test_scans==0);error.clear();
   m.remote_recover.get("confirmed")->updateBool(true);api.commands.at("bluetooth/remote_recover")(0,error);assert(!error.length());
  }else{api.commands.at("bluetooth/remote_start")(0,error);assert(!error.length());}''')
  text=text.replace(' auto id=m.remote_bindings', ''' if(test_fault==0){
   for(unsigned i=1;i<=3;i++){auto &d=RemoteBindingBackend::snapshots[i];std::ofstream file("w"+std::to_string(i),std::ios::binary);file.write((char*)d.data(),d.size());}
   tag_dump();
  }
  auto id=m.remote_bindings''')
  text=text.replace('extern "C" void tag_test_fail(int);','extern "C" {void tag_test_fail(int);void tag_dump(void);}')
  text=text.replace('TeslaBleModuleTest::run();puts(', '''if(test_fault>=20){std::ifstream f("w"+std::to_string(1+(test_fault-20)/3),std::ios::binary);RemoteBindingBackend::disk.assign(std::istreambuf_iterator<char>(f),{});assert(!RemoteBindingBackend::disk.empty());}TeslaBleModuleTest::run();puts(''')
  text=text.replace('if(test_fault>=20){std::ifstream f("w"+std::to_string(1+(test_fault-20)/3)', 'if(test_fault>=20){std::ifstream f("w"+std::to_string(test_fault>=30?2:1+(test_fault-20)/3)')
  text=text.replace(' for(unsigned i=0;i<3;i++){unsigned t=100+200*i;tag_tick(t,1);tag_tick(t+25,1);tag_tick(t+75,0);tag_tick(t+100,0);}', ''' if(test_fault>=30){tag_tick(100,1);tag_tick(125,1);tag_tick(175,0);tag_tick(200,0);tag_tick(750,0);}
 else{for(unsigned i=0;i<3;i++){unsigned t=100+200*i;tag_tick(t,1);tag_tick(t+25,1);tag_tick(t+75,0);tag_tick(t+100,0);}}''')
  text=text.replace('if(test_fault>=20){\n   auto entries=', 'if(test_fault>=30){m.remote_next_scan=1000;m.remote_loop();assert(m.remote_normal&&m.remote_setup.busy());}else if(test_fault>=20){\n   auto entries=')
  text=text.replace(' assert(tag_blue(1600)', ' if(test_fault<30)assert(tag_blue(1600)')
  text=text.replace(' step(2100);step(2200,1);step(2225,1);step(2275,0);step(2300,0);step(2700,0);', ' if(test_fault<30){step(2100);step(2200,1);step(2225,1);step(2275,0);step(2300,0);step(2700,0);}')
  text=text.replace('for(unsigned t=2725;t<4500;t+=25)step(t);', 'for(unsigned t=test_fault>=30?1600:2725;t<(test_fault==36?12000:4500);t+=25){if(test_fault>=30&&(m.remote_setup.phase()==RemoteSetup::SessionComplete||m.remote_setup.phase()==RemoteSetup::Failed))break;step(t);}')
  text=text.replace(' assert(m.remote_setup.phase()==RemoteSetup::Paired);', ''' if(test_fault==33||test_fault==35||test_fault==36){assert(m.remote_setup.phase()==RemoteSetup::Failed&&m.remote_store.count()==1&&!m.remote_state.get("trusted")->asBool()&&host_charge_calls==0);if(test_fault==35||test_fault==36)assert(m.remote_store.slot(0).sequence==0);test_submit={};return;}
 if(test_fault>=30){assert(m.remote_setup.phase()==RemoteSetup::SessionComplete&&m.remote_store.count()==1);assert(m.remote_store.slot(0).sequence>0&&tag_disk_counter()>0&&tag_unavailable()==1);assert(host_charge_calls==0&&m.bluetooth.central()->idle());test_submit={};return;}
 assert(m.remote_setup.phase()==RemoteSetup::Paired);''')
  text=text.replace('int tag_disk_phase(void);','int tag_disk_phase(void);unsigned long long tag_disk_counter(void);int tag_unavailable(void);')
  text=text.replace('api.commands.at("bluetooth/remote_select")(0,error);assert(!error.length());','if(test_fault<30){api.commands.at("bluetooth/remote_select")(0,error);assert(!error.length());}')
  text=text.replace('test_scan(&advertisement,test_scan_arg);m.remote_loop();','if(test_fault>=30)ad[14]=0x14;test_scan(&advertisement,test_scan_arg);m.remote_loop();')
  text=text.replace('TeslaVehicle m;m.pre_setup();m.setup();m.register_urls();', '''TeslaVehicle m;m.pre_setup();m.setup();m.register_urls();
 if(test_fault==37){uint8_t other[32]={81},key[32]={91};assert(m.remote_store.prepare(other,key,"other"));assert(m.remote_store.commit_prepared(other,key));assert(m.remote_store.activate(other));}''')
  text=text.replace('RemoteSetup::SessionComplete&&m.remote_store.count()==1','RemoteSetup::SessionComplete&&m.remote_store.count()==(test_fault==37?2:1)')
  text=text.replace('assert(host_charge_calls==0&&m.bluetooth.central()->idle());test_submit={};return;', 'assert(host_charge_calls==0&&m.bluetooth.central()->idle());if(test_fault==37)assert(m.remote_store.slot(1).sequence==0&&m.remote_store.slot(1).key[0]==91);test_submit={};return;')
  text=text.replace(' m.bluetooth_config.get("enable")->updateBool(true);',''' if(test_fault==38){uint8_t request[16]={1},reply[64];
  host_remote_manager.observe(10,2,true);clock_ms=5011;assert(TeslaVehicle::remote_session_snapshot(&m,request,reply)&&reply[16]==0);
  host_remote_manager.observe(99999,2,true);clock_ms=1000;assert(TeslaVehicle::remote_session_snapshot(&m,request,reply)&&reply[16]==0);
  host_remote_manager.observe(1000,3,true);assert(TeslaVehicle::remote_session_snapshot(&m,request,reply)&&reply[16]==0);
  host_remote_manager.observe(1000,2,false);assert(TeslaVehicle::remote_session_snapshot(&m,request,reply)&&reply[16]==0);
  host_remote_manager.observe(1000,2,true);clock_ms=0;}
 m.bluetooth_config.get("enable")->updateBool(true);''')
  text=text.replace('tag_disk_counter()>0&&tag_unavailable()==1','tag_disk_counter()>0&&(test_fault==38?tag_mode_pv():tag_unavailable())==1')
  text=text.replace('int tag_unavailable(void);','int tag_unavailable(void);int tag_mode_pv(void);')
  text=text.replace('if(test_fault==37)assert(m.remote_store.slot(1).sequence==0&&m.remote_store.slot(1).key[0]==91);test_submit={};return;', '''if(test_fault==37)assert(m.remote_store.slot(1).sequence==0&&m.remote_store.slot(1).key[0]==91);
 if(test_fault==39){
  auto prior_warp=m.remote_store.slot(0).sequence;auto prior_tag=tag_disk_counter();
  step(8000);step(8500,1);step(8525,1);step(8575,0);step(8600,0);step(9100,0);
  assert(m.remote_setup.busy());test_scan(&advertisement,test_scan_arg);m.remote_loop();test_run_npl_events();
  for(unsigned now=9125;now<11000;now+=25){step(now);if(m.remote_setup.phase()==RemoteSetup::SessionComplete)break;}
  assert(m.remote_setup.phase()==RemoteSetup::SessionComplete&&m.remote_store.slot(0).sequence>prior_warp&&tag_disk_counter()>prior_tag);
  assert(tag_unavailable()&&m.bluetooth.central()->idle()&&host_charge_calls==0&&reboot_calls==0);
  puts("NORMAL_SAME_RUNTIME_SECOND_GATT_EXCHANGE_PASS");
 }
 test_submit={};return;''')
  text=text.replace('unsigned notified=0,receipt_drops=0;', 'unsigned notified=0,receipt_drops=0,replay_index=0,replay_frames=0;bool replay_exchange=false;std::vector<std::vector<uint8_t>> captured_response;')
  text=text.replace('if(stage==5){if(test_handle==10)', '''if(stage==5){
   if(test_fault==42&&test_handle==10&&test_write.size()>=7&&test_write[0]==0xa6&&test_write[2]==21){
    if(!replay_exchange)captured_response.push_back(test_write);
    else{if(test_write[3]==0)replay_index=0;assert(replay_index<captured_response.size());auto cookie=test_write[1];test_write=captured_response[replay_index++];test_write[1]=cookie;replay_frames++;}
   }
   if(test_handle==10)''')
  text=text.replace('if(test_fault==39){', 'if(test_fault==39||test_fault==40||test_fault==42){')
  text=text.replace('step(8000);step(8500,1);', 'replay_exchange=test_fault==42;step(8000);step(8500,1);')
  text=text.replace('assert(m.remote_setup.busy());test_scan(&advertisement,test_scan_arg);', '''assert(m.remote_setup.busy());
  if(test_fault==40){
   m.remote_update.get("enable")->updateBool(false);api.commands.at("bluetooth/remote_config_update")(0,error);assert(!error.length());
   test_run_npl_events();m.remote_loop();assert(!m.remote_setup.busy()&&!m.bluetooth.central()->remote_reserved());
   step(22000);assert(host_charge_calls==0&&m.remote_store.count()==1&&tag_disk_counter()==prior_tag);test_submit={};puts("NORMAL_DISABLE_DRAINS_NO_RESERVATION_PASS");return;
  }
  test_scan(&advertisement,test_scan_arg);''')
  text=text.replace('for(unsigned now=9125;now<11000;now+=25){step(now);if(m.remote_setup.phase()==RemoteSetup::SessionComplete)break;}', '''for(unsigned now=9125;now<(test_fault==42?22000:11000);now+=25){step(now);if(m.remote_setup.phase()==RemoteSetup::SessionComplete||m.remote_setup.phase()==RemoteSetup::Failed)break;}
  if(test_fault==42){assert(replay_frames>=captured_response.size()&&m.remote_setup.phase()==RemoteSetup::Failed&&m.remote_store.count()==1&&host_charge_calls==0);test_submit={};puts("NORMAL_FULL_GATT_OLD_CIPHERTEXT_REPLAY_REJECTED_PASS");return;}''')
  text=text.replace('test_submit={};return;', 'test_submit={};return;')
  text=text.replace('extern "C" {void tag_test_fail(int);void tag_dump(void);}', 'extern "C" {void tag_test_fail(int);void tag_dump(void);void tag_save_current(const char*);}')
  text=text.replace('if(test_handle==10){tag_poll(clock_ms);relay();tag_poll(clock_ms);relay();}', '''if(test_handle==10){tag_poll(clock_ms);relay();tag_poll(clock_ms);relay();
   if(test_fault==43&&test_write.size()==17&&test_write[0]==4){
    assert(tag_disk_counter()==1&&m.remote_store.slot(0).sequence==0);
    tag_save_current("tag-reserved");auto&d=RemoteBindingBackend::disk;
    std::ofstream file("warp-before-reserved-peer-hello",std::ios::binary);file.write((char*)d.data(),d.size());file.close();
    puts("NORMAL_ACTUAL_GATT_POWERLOSS_AFTER_TAG_RESERVATION_PASS");fflush(stdout);_Exit(0);
   }
  }''')
  text=text.replace('std::ifstream f("w"+std::to_string(test_fault>=30?2:1+(test_fault-20)/3),std::ios::binary);','std::ifstream f(test_fault==44?std::string("warp-before-reserved-peer-hello"):"w"+std::to_string(test_fault>=30?2:1+(test_fault-20)/3),std::ios::binary);')
  text=text.replace('assert(host_charge_calls==0&&m.bluetooth.central()->idle());if(test_fault==37)', 'assert(host_charge_calls==0&&m.bluetooth.central()->idle());if(test_fault==44)assert(tag_disk_counter()==2&&m.remote_store.slot(0).sequence==1);if(test_fault==37)')
  text=text.replace('bool replay_exchange=false;', 'bool replay_exchange=false,cancel_due=false;')
  text=text.replace('test_gap(&event,test_gap_arg);if(test_fault==14||test_fault==32)', 'test_gap(&event,test_gap_arg);if(test_fault==45&&response[2]==18&&response[3]==14)cancel_due=true;if(test_fault==14||test_fault==32)')
  text=text.replace('  relay();\n  m.remote_loop();', '''  relay();
  if(test_fault==45&&cancel_due){cancel_due=false;m.remote_setup.cancel();m.remote_update.get("enable")->updateBool(false);api.commands.at("bluetooth/remote_config_update")(0,error);assert(!error.length());}
  m.remote_loop();''')
  text=text.replace('m.remote_setup.phase()==RemoteSetup::Failed))break;', 'm.remote_setup.phase()==RemoteSetup::Failed||m.remote_setup.phase()==RemoteSetup::Cancelled))break;')
  text=text.replace(' if(test_fault>=30){assert(m.remote_setup.phase()==RemoteSetup::SessionComplete', ''' if(test_fault==45){assert(m.remote_setup.phase()==RemoteSetup::Cancelled&&m.remote_store.slot(0).sequence==1&&tag_disk_counter()==1&&!m.bluetooth.central()->remote_reserved()&&m.bluetooth.central()->idle()&&host_charge_calls==0);puts("NORMAL_CANCEL_AFTER_BOTH_RESERVATIONS_DRAINS_PASS");test_submit={};return;}
 if(test_fault>=30){assert(m.remote_setup.phase()==RemoteSetup::SessionComplete''')
  text=text.replace('struct TeslaBleModuleTest {','extern "C" {void tag_shake_request(unsigned long long);int tag_partial_car(void);}\nstruct TeslaBleModuleTest {')
  text=text.replace('tag_tick(100,1);tag_tick(125,1);tag_tick(175,0);tag_tick(200,0);tag_tick(750,0);','if(test_fault>=46&&test_fault<=48)tag_shake_request(750);else{tag_tick(100,1);tag_tick(125,1);tag_tick(175,0);tag_tick(200,0);tag_tick(750,0);}')
  text=text.replace(' m.bluetooth_config.get("enable")->updateBool(true);',''' if(test_fault==46){const auto live=current_ev_identity();m.remote_soc.observe(1000,live.generation,live.index,live.mac,true,true,67);host_remote_power=4200;host_remote_power_received=1000;}
 m.bluetooth_config.get("enable")->updateBool(true);''')
  text=text.replace('test_fault==38?tag_mode_pv():tag_unavailable()', 'test_fault==46?tag_partial_car():test_fault==38?tag_mode_pv():tag_unavailable()')
  text=text.replace(' m.bluetooth_config.get("enable")->updateBool(true);',''' if(test_fault==47){const auto live=current_ev_identity();m.remote_soc.observe(99999,live.generation,live.index,live.mac,true,true,67);host_remote_power=4200;host_remote_power_received=99999;}
 if(test_fault==48){const auto live=current_ev_identity();m.remote_soc.observe(1000,live.generation+1,live.index,live.mac,true,true,67);host_remote_power=NAN;host_remote_power_received=1000;}
 m.bluetooth_config.get("enable")->updateBool(true);''')
  text=text.replace('TeslaVehicle m;m.pre_setup();','bool saw_pending=false;TeslaVehicle m;m.pre_setup();')
  text=text.replace('  relay();\n', '''  relay();
  if((test_fault==49||test_fault==50||test_fault==51)&&m.remote_response_waiting&&now>=(test_fault==51?m.remote_response_until:1600)){
   saw_pending=true;assert(!tag_mode_pv()&&host_charge_calls==0);
   if(test_fault==49||test_fault==51)host_remote_manager.observe(now,2,true);
   else{m.remote_setup.cancel();m.remote_update.get("enable")->updateBool(false);api.commands.at("bluetooth/remote_config_update")(0,error);assert(!error.length());}
  }
''')
  text=text.replace('test_fault==38?tag_mode_pv()', '(test_fault==38||test_fault==49)?tag_mode_pv()')
  text=text.replace(' if(test_fault==45){assert(', ' if(test_fault==50){assert(saw_pending&&!m.remote_response_waiting&&m.remote_setup.phase()==RemoteSetup::Cancelled&&m.bluetooth.central()->idle()&&!m.bluetooth.central()->remote_reserved()&&host_charge_calls==0);test_submit={};puts("NORMAL_ASYNC_CANCEL_DRAIN_PASS");return;}\n if(test_fault==49||test_fault==51)assert(saw_pending&&!m.remote_response_waiting);\n if(test_fault==45){assert(')
  text=text.replace('if(test_handle==10){tag_poll(clock_ms);relay();tag_poll(clock_ms);relay();', '''if(test_handle==10){tag_poll(clock_ms);relay();tag_poll(clock_ms);relay();
   if(test_fault==52&&test_write.size()>=7&&test_write[0]==0xa6&&test_write[2]==17&&test_write[3]==0){
    assert(tag_disk_counter()==1&&m.remote_store.slot(0).sequence==1);
    tag_save_current("tag-before-warp-reserved-hello");auto&d=RemoteBindingBackend::disk;
    std::ofstream file("warp-reserved",std::ios::binary);file.write((char*)d.data(),d.size());file.close();
    puts("NORMAL_ACTUAL_GATT_POWERLOSS_AFTER_WARP_RESERVATION_PASS");fflush(stdout);_Exit(0);
   }
''')
  text=text.replace('test_fault==44?std::string("warp-before-reserved-peer-hello"):', 'test_fault==53?std::string("warp-reserved"):test_fault==44?std::string("warp-before-reserved-peer-hello"):')
  text=text.replace('if(test_fault==44)assert(tag_disk_counter()==2&&m.remote_store.slot(0).sequence==1);', 'if(test_fault==53)assert(tag_disk_counter()==2&&m.remote_store.slot(0).sequence==2);if(test_fault==44)assert(tag_disk_counter()==2&&m.remote_store.slot(0).sequence==1);')
  text=text.replace('bool saw_pending=false;TeslaVehicle m;', '''bool saw_pending=false;
 auto cut=[&](){assert(tag_disk_counter()==1);tag_save_current(("tag-cut-"+std::to_string(test_fault)).c_str());auto&d=RemoteBindingBackend::disk;std::ofstream f("warp-cut-"+std::to_string(test_fault),std::ios::binary);f.write((char*)d.data(),d.size());f.close();puts("NORMAL_ACTUAL_GATT_POWERLOSS_AT_FRAME_PASS");fflush(stdout);_Exit(0);};
 TeslaVehicle m;''')
  text=text.replace('os_mbuf mb{{response,response+n}};', '''if(response[3]==0&&((test_fault==54&&response[2]==18)||(test_fault==58&&response[2]==20)||(test_fault==62&&response[2]==22)))cut();
   os_mbuf mb{{response,response+n}};''')
  text=text.replace('if(test_fault==52&&test_write.size()>=7', '''if(test_write.size()>=7&&test_write[0]==0xa6&&test_write[3]==0&&((test_fault==56&&test_write[2]==19)||(test_fault==60&&test_write[2]==21)))cut();
   if(test_fault==52&&test_write.size()>=7''')
  text=text.replace('test_fault==53?std::string("warp-reserved"):', 'test_fault>=55&&test_fault<=63&&test_fault%2?"warp-cut-"+std::to_string(test_fault-1):test_fault==53?std::string("warp-reserved"):')
  text=text.replace('if(test_fault==53)assert(', 'if(test_fault>=55&&test_fault<=63&&test_fault%2)assert(tag_disk_counter()==2&&m.remote_store.slot(0).sequence==2);if(test_fault==53)assert(')
  text=text.replace(' auto relay=[&](){', ' uint8_t first_hello[20]{};auto relay=[&](){')
  text=text.replace('unsigned n=tag_notification_size();', '''unsigned n=tag_notification_size();
   if(response[2]==17){
    if(test_fault==64&&response[3]==0)response[4]=97;
    if(test_fault==65&&response[3]==14){response[2]=18;response[3]=0;response[4]=17;}
    if(test_fault==66&&response[3]==0)response[3]=1;
    if(test_fault==67){if(response[3]==0)memcpy(first_hello,response,20);else if(response[3]==14){memcpy(response,first_hello,20);response[6]^=1;}}
    if(test_fault==68&&response[3]==0)response[2]=95;
   }
''')
  text=text.replace('test_fault==36?12000:4500', '(test_fault==36||test_fault>=64)?12000:4500')
  text=text.replace(' if(test_fault==33||test_fault==35||test_fault==36){assert(', ''' if(test_fault>=64&&test_fault<=68){assert(m.remote_setup.phase()==RemoteSetup::Failed&&m.remote_store.count()==1&&m.remote_store.slot(0).sequence==0&&tag_disk_counter()==1&&m.remote_bindings_ok&&!m.remote_store.quarantined()&&!m.bluetooth.central()->remote_reserved()&&m.bluetooth.central()->idle()&&host_charge_calls==0);test_submit={};puts("NORMAL_GATT_INVALID_FRAGMENT_REJECTED_PASS");return;}
 if(test_fault==33||test_fault==35||test_fault==36){assert(''')
  text=text.replace('struct TeslaBleModuleTest {','extern "C" {void tag_diagnostic_request(unsigned long long);void tag_operation_save(const char*);int tag_operation_phase(void);unsigned tag_operation_writes(void);unsigned long long tag_operation_id(void);}\nstruct TeslaBleModuleTest {')
  text=text.replace('if(test_fault>=46&&test_fault<=48)tag_shake_request(750);', 'if(test_fault>=69)tag_diagnostic_request(750);else if(test_fault>=46&&test_fault<=48)tag_shake_request(750);')
  text=text.replace('test_fault>=55&&test_fault<=63&&test_fault%2?"warp-cut-"', 'test_fault==70?std::string("operation-warp-binding"):test_fault>=55&&test_fault<=63&&test_fault%2?"warp-cut-"')
  text=text.replace('TeslaBleModuleTest::run();puts(', '''if(test_fault==70){std::ifstream f("operation-warp-journal",std::ios::binary);RemoteOperationBackend::disk.assign(std::istreambuf_iterator<char>(f),{});assert(!RemoteOperationBackend::disk.empty());}if(test_fault==73)RemoteOperationBackend::fail=true;TeslaBleModuleTest::run();puts(''')
  text=text.replace('if(test_fault==52&&test_write.size()>=7', '''if(test_fault==69&&test_write.size()>=7&&test_write[0]==0xa6&&test_write[2]==21&&test_write[3]==0){
    auto*r=m.remote_operations.find(m.remote_session.identity());assert(r&&r->phase==3&&r->operation==1&&tag_operation_phase()==2&&tag_disk_counter()==2&&m.remote_store.slot(0).sequence==1&&RemoteOperationBackend::writes==3);
    tag_save_current("operation-tag-binding");tag_operation_save("operation-tag-journal");
    for(auto item:{std::make_pair("operation-warp-binding",&RemoteBindingBackend::disk),std::make_pair("operation-warp-journal",&RemoteOperationBackend::disk)}){std::ofstream f(item.first,std::ios::binary);f.write((char*)item.second->data(),item.second->size());}
    puts("NORMAL_DURABLE_OPERATION_CUT_BEFORE_TAG_RESULT_PASS");fflush(stdout);_Exit(0);
   }
   if(test_fault==52&&test_write.size()>=7''')
  text=text.replace(' if(test_fault>=64&&test_fault<=68){assert(', ''' if(test_fault==72||test_fault==73){assert(m.remote_setup.phase()==RemoteSetup::Failed&&m.remote_store.count()==1&&m.remote_bindings_ok&&tag_operation_phase()==2&&m.bluetooth.central()->idle()&&!m.bluetooth.central()->remote_reserved()&&host_charge_calls==0);if(test_fault==73)assert(m.remote_operations.quarantined());test_submit={};puts("NORMAL_DURABLE_OPERATION_FAULT_NO_RECEIPT_PASS");return;}
 if(test_fault==70||test_fault==71){auto*r=m.remote_operations.find(m.remote_session.identity());assert(m.remote_setup.phase()==RemoteSetup::SessionComplete&&r&&r->phase==3&&r->operation==1&&r->outcome==1&&tag_operation_phase()==3&&tag_operation_id()==1&&tag_unavailable()&&host_charge_calls==0&&m.bluetooth.central()->idle());if(test_fault==70)assert(RemoteOperationBackend::writes==0&&tag_operation_writes()==1&&tag_disk_counter()==3&&m.remote_store.slot(0).sequence==2);else{assert(RemoteOperationBackend::writes==3&&tag_operation_writes()==3&&tag_disk_counter()==2&&m.remote_store.slot(0).sequence==1);uint8_t saved_peer[32];memcpy(saved_peer,r->peer,32);auto id=m.remote_bindings.get("entries")->get(size_t(0))->get("id")->asString();m.remote_forget.get("id")->updateString(id);m.remote_forget.get("confirmed")->updateBool(false);api.commands.at("bluetooth/remote_forget")(0,error);assert(error.length()&&m.remote_operations.find(saved_peer));error.clear();m.remote_forget.get("confirmed")->updateBool(true);api.commands.at("bluetooth/remote_forget")(0,error);assert(!error.length()&&m.remote_store.count()==0&&!m.remote_operations.find(saved_peer)&&tag_disk_phase()==3);}test_submit={};puts("NORMAL_DURABLE_OPERATION_VERIFIED_BOTH_PEERS_PASS");return;}
 if(test_fault==69)assert(false&&"missing operation-cut checkpoint");
 if(test_fault>=64&&test_fault<=68){assert(''')
  text=text.replace('struct TeslaBleModuleTest {','extern "C" int tag_operation_outcome(void);\nstruct TeslaBleModuleTest {')
  text=text.replace('m.setup();m.register_urls();', '''m.setup();m.register_urls();
 if(test_fault==74||test_fault==76){RemoteOperationBackend::after_write=[&](){
  if(RemoteOperationBackend::disk[45]!=(test_fault==74?2:1))return;
  assert(tag_disk_counter()==2&&tag_operation_phase()==2&&m.remote_store.slot(0).sequence==1);
  tag_save_current(("op-binding-tag-"+std::to_string(test_fault)).c_str());tag_operation_save(("op-journal-tag-"+std::to_string(test_fault)).c_str());
  for(auto item:{std::make_pair("op-binding-warp-",&RemoteBindingBackend::disk),std::make_pair("op-journal-warp-",&RemoteOperationBackend::disk)}){std::ofstream f(item.first+std::to_string(test_fault),std::ios::binary);f.write((char*)item.second->data(),item.second->size());}
  puts("NORMAL_DURABLE_OPERATION_POWERLOSS_PENDING_PASS");fflush(stdout);_Exit(0);
 };}
''')
  text=text.replace('test_fault==70?std::string("operation-warp-binding"):', '(test_fault==75||test_fault==77)?"op-binding-warp-"+std::to_string(test_fault-1):test_fault==70?std::string("operation-warp-binding"):')
  text=text.replace('if(test_fault==70){std::ifstream f("operation-warp-journal"', 'if(test_fault==75||test_fault==77){std::ifstream f("op-journal-warp-"+std::to_string(test_fault-1),std::ios::binary);RemoteOperationBackend::disk.assign(std::istreambuf_iterator<char>(f),{});assert(!RemoteOperationBackend::disk.empty());}if(test_fault==70){std::ifstream f("operation-warp-journal"')
  text=text.replace(' if(test_fault==72||test_fault==73){assert(', ''' if(test_fault==75||test_fault==77){auto*r=m.remote_operations.find(m.remote_session.identity());assert(m.remote_setup.phase()==RemoteSetup::SessionComplete&&r&&r->operation==1&&r->phase==(test_fault==75?2:1)&&RemoteOperationBackend::writes==0&&tag_operation_phase()==3&&tag_operation_outcome()==0&&tag_operation_id()==1&&tag_operation_writes()==1&&tag_disk_counter()==3&&m.remote_store.slot(0).sequence==2&&host_charge_calls==0&&m.bluetooth.central()->idle());test_submit={};puts("NORMAL_DURABLE_OPERATION_UNKNOWN_NOT_REISSUED_PASS");return;}
 if(test_fault==74||test_fault==76)assert(false&&"missing pending operation cut");
 if(test_fault==72||test_fault==73){assert(''')
  text=text.replace('struct TeslaBleModuleTest {','extern "C" {unsigned tag_capabilities(void);int tag_reject_unsupported(unsigned long long);}\nstruct TeslaBleModuleTest {')
  text=text.replace('if(test_fault>=69)tag_diagnostic_request', 'if(test_fault>=69&&test_fault<=77)tag_diagnostic_request')
  text=text.replace(' m.bluetooth_config.get("enable")->updateBool(true);', ''' if(test_fault>=78&&test_fault<=81){host_remote_manager.observe(1000,2,true);host_remote_capabilities=test_fault==81?1:7;if(test_fault==79)ev.profiles[ev.index]^=1;if(test_fault==80)host_profile_mac_probe=[](){++ev.generation;};}
 m.bluetooth_config.get("enable")->updateBool(true);''')
  text=text.replace(' if(test_fault==75||test_fault==77){auto*r=', ''' if(test_fault>=78&&test_fault<=81){assert(m.remote_setup.phase()==RemoteSetup::SessionComplete&&tag_mode_pv()&&tag_capabilities()==(test_fault==78?7:test_fault==81?1:0)&&host_charge_calls==0&&m.bluetooth.central()->idle());host_profile_mac_probe={};if(test_fault==81)assert(tag_reject_unsupported(clock_ms+25));test_submit={};puts("NORMAL_CAPABILITY_PROFILE_GATE_AUTHENTICATED_PASS");return;}
 if(test_fault==75||test_fault==77){auto*r=''')
  text=text.replace('if(test_fault>=69&&test_fault<=77)tag_diagnostic_request', 'if((test_fault>=69&&test_fault<=77)||test_fault==82)tag_diagnostic_request')
  text=text.replace('if(test_handle==10){int rc=tag_write', '''if(test_fault==82&&clock_ms<14000&&test_handle==10&&test_write.size()>=7&&test_write[0]==0xa6&&test_write[2]==21){ble_gatt_error ok{};ble_gatt_attr a{test_handle};test_written(4,&ok,&a,test_proc_arg);return;}
 if(test_handle==10){int rc=tag_write''')
  text=text.replace(' if(test_fault>=78&&test_fault<=81){assert(', ''' if(test_fault==82){
  assert(m.remote_setup.phase()==RemoteSetup::Failed&&RemoteOperationBackend::writes==3&&tag_operation_phase()==2&&tag_operation_id()==1&&tag_operation_writes()==2&&host_charge_calls==0&&m.bluetooth.central()->idle());
  step(14000);tag_diagnostic_request(14100);step(14100);m.remote_next_scan=14100;m.remote_loop();assert(m.remote_setup.busy());test_scan(&advertisement,test_scan_arg);m.remote_loop();test_run_npl_events();
  for(unsigned now=14125;now<17500;now+=25){step(now);if(m.remote_setup.phase()==RemoteSetup::SessionComplete)break;}
  assert(m.remote_setup.phase()==RemoteSetup::SessionComplete&&RemoteOperationBackend::writes==3&&tag_operation_phase()==3&&tag_operation_id()==1&&tag_operation_writes()==3&&tag_disk_counter()==3&&m.remote_store.slot(0).sequence==2&&host_charge_calls==0&&m.bluetooth.central()->idle());test_submit={};puts("NORMAL_SAME_RUNTIME_LOST_RESULT_STABLE_ID_NO_REEXECUTION_PASS");return;
 }
 if(test_fault>=78&&test_fault<=81){assert(''')
  text=text.replace('if(test_fault>=46&&test_fault<=48)tag_shake_request(750)', 'if((test_fault>=46&&test_fault<=48)||test_fault==83||test_fault==84)tag_shake_request(750)')
  text=text.replace(' m.bluetooth_config.get("enable")->updateBool(true);', ''' if(test_fault==83||test_fault==84){const auto live=current_ev_identity();m.remote_soc.observe(1000,live.generation,live.index,live.mac,true,true,67);host_remote_power=4200;host_remote_power_received=1000;if(test_fault==83)ev.profiles[ev.index]^=1;else host_profile_mac_probe=[](){++ev.generation;};}
 m.bluetooth_config.get("enable")->updateBool(true);''')
  text=text.replace(' if(test_fault==82){\n  assert(', ''' if(test_fault==83||test_fault==84){assert(m.remote_setup.phase()==RemoteSetup::SessionComplete&&tag_unavailable()&&host_charge_calls==0&&m.bluetooth.central()->idle());host_profile_mac_probe={};test_submit={};puts("NORMAL_STATUS_PROFILE_RACE_INVALIDATED_PASS");return;}
 if(test_fault==82){
  assert(''')
  source.write_text(text)
 else:
  source.write_text(source.read_text().replace('uint8_t ad[]={','assert(m.remote_setup.set_enrollment({}));uint8_t ad[]={'))
 exe=tmp_path/'e2e'
 flags=['-std=c++17','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-pthread','-fno-rtti','-fno-exceptions','-fsanitize=address,undefined','-no-pie',
 '-DTESLA_NIMBLE_HOST_TEST=1','-DTESLA_BLE_USE_NIMBLE=1','-DTESLA_BLE_MODULE_HOST_TEST','-DTESLA_BLE_HOST_FILESYSTEM','-DTESLA_BLE_MANUAL_VEHICLE_UI=1',
 '-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192','-DCONFIG_BT_NIMBLE_PINNED_TO_CORE=0',
 '-DBLUETOOTH_CONFIG_PATH="'+str(tmp_path/'bt-config')+'"','-DWARP_REMOTE_CONFIG_PATH="'+str(tmp_path/'remote-config')+'"',
 '-I'+str(S/'tests/tesla_vehicle'),'-I'+str(S/'tests/tesla_vehicle/host'),'-I'+str(S/'lib/TeslaBLEClean/include'),'-I'+str(MBED/'include')]
 src=[S/'src/modules/tesla_vehicle'/n for n in ['tesla_ble_storage.cpp','tesla_ble_transport.cpp','tesla_ble_gatt.cpp','bluetooth_lifecycle.cpp','tesla_nimble_central.cpp','remote_blink_client.cpp','remote_crypto.cpp','remote_setup.cpp']]+list((S/'lib/TeslaBLEClean/src').glob('*.cpp'))
 tag_src=[TAG/'firmware/src'/n for n in ['remote_input.c','remote_consent.c','remote_ui.c','remote_status.c']]
 if secure:
  tag_src.append(TAG/'firmware/src/remote_pair_store.c')
  for name,content in {
   'settings/settings.h':'#include <stddef.h>\n#ifdef __cplusplus\nextern "C" {\n#endif\nint settings_load_one(const char*,void*,size_t);int settings_save_one(const char*,const void*,size_t);\n#ifdef __cplusplus\n}\n#endif\n',
   'random/random.h':'#include <stddef.h>\n#ifdef __cplusplus\nextern "C" {\n#endif\nint sys_csrand_get(void*,size_t);\n#ifdef __cplusplus\n}\n#endif\n'}.items():
   p=tmp_path/'zephyr'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(content)
  platform=tmp_path/'tag-settings.c';platform.write_text('''#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/random.h>
static uint8_t disk[77],snapshots[4][77];static int present,fail_phase,fail_after,poisoned;
static uint8_t operation_disk[180];static int operation_present,operation_fail_phase;static unsigned operation_writes;
void tag_operation_save(const char*path){FILE*f=fopen(path,"wb");assert(f&&fwrite(operation_disk,1,180,f)==180);fclose(f);}
int tag_operation_phase(void){return operation_present?operation_disk[45]:0;}
int tag_operation_outcome(void){return operation_present?operation_disk[46]:-1;}
unsigned tag_operation_writes(void){return operation_writes;}
unsigned long long tag_operation_id(void){unsigned long long v=0;for(unsigned i=0;i<8;i++)v=(v<<8)|operation_disk[36+i];return v;}
void tag_save_current(const char*path){FILE*f=fopen(path,"wb");assert(f&&fwrite(disk,1,77,f)==77);fclose(f);}
void tag_dump(void){for(unsigned i=1;i<=3;i++){char path[3]={'t',(char)('0'+i),0};FILE*f=fopen(path,"wb");assert(f);assert(fwrite(snapshots[i],1,77,f)==77);fclose(f);}}
void tag_test_fail(int n){const char*op=getenv("TAG_OP_RECORD");if(op){FILE*f=fopen(op,"rb");assert(f&&fread(operation_disk,1,180,f)==180);fclose(f);operation_present=1;}const char*fail=getenv("TAG_OP_FAIL");if(fail)operation_fail_phase=atoi(fail);fail_phase=n?(n-1)%3+1:0;fail_after=n>3;const char*path=getenv("TAG_RECORD");if(path){FILE*f=fopen(path,"rb");assert(f&&fread(disk,1,77,f)==77);fclose(f);present=1;}const char*mutation=getenv("TAG_MUTATE");if(mutation){if(atoi(mutation)==1)disk[37]^=1;else disk[5]^=1;}}
int settings_load_one(const char*k,void*out,size_t n){if(!strcmp(k,"warp_remote/operation_v1")){assert(n==180);if(!operation_present)return -ENOENT;memcpy(out,operation_disk,n);return (int)n;}assert(!strcmp(k,"warp_remote/binding_v1")&&n==77);if(poisoned)return -EIO;if(!present)return -ENOENT;memcpy(out,disk,n);return (int)n;}
int settings_save_one(const char*k,const void*in,size_t n){if(!strcmp(k,"warp_remote/operation_v1")){assert(n==180);operation_writes++;if(((const uint8_t*)in)[45]==operation_fail_phase)return -EIO;memcpy(operation_disk,in,n);operation_present=1;return 0;}assert(!strcmp(k,"warp_remote/binding_v1")&&n==77);int failing=((const uint8_t*)in)[4]==fail_phase;if(failing&&!fail_after)return -EIO;memcpy(disk,in,n);memcpy(snapshots[disk[4]],in,n);present=1;if(failing)poisoned=1;return 0;}
int sys_csrand_get(void*out,size_t n){return getrandom(out,n,0)==(ssize_t)n?0:-1;}
int tag_disk_phase(void){return present?disk[4]:0;}
unsigned long long tag_disk_counter(void){unsigned long long c=0;for(unsigned i=0;i<8;i++)c=(c<<8)|disk[69+i];return c;}
''');tag_src.append(platform)
  src.append(TAG/'firmware/src/remote_secure.cpp')
  flags.extend(['-I'+str(tmp_path),'-I'+str(TAG/'firmware/src')])
 objects=[obj]
 for item in tag_src:
  out=tmp_path/(item.stem+'.o');subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I'+str(item.parent),'-c',str(item),'-o',str(out)],check=True);objects.append(out)
 libs=[CACHE/n for n in ['library/libmbedcrypto.a','3rdparty/everest/libeverest.a','3rdparty/p256-m/libp256m.a']]
 subprocess.run(['g++',*flags,str(source),*map(str,src),*map(str,objects),*map(str,libs),'-o',str(exe)],check=True)
 for mode in list(range(15))+list(range(20,29))+[30,31,32,33,35,36,37,38,39,40,42,43,44,45,46,47,48,49,50,51,52,53]+list(range(54,85)) if secure else [0]:
  env=dict(os.environ)
  if mode>=20:env['TAG_RECORD']='t'+str(3 if mode>=30 else 1+(mode-20)%3)
  if 55<=mode<=63 and mode%2:env['TAG_RECORD']='tag-cut-'+str(mode-1)
  if mode==53:env['TAG_RECORD']='tag-before-warp-reserved-hello'
  if mode==70:env.update(TAG_RECORD='operation-tag-binding',TAG_OP_RECORD='operation-tag-journal')
  if mode in (75,77):env.update(TAG_RECORD='op-binding-tag-'+str(mode-1),TAG_OP_RECORD='op-journal-tag-'+str(mode-1))
  if mode==72:env['TAG_OP_FAIL']='3'
  if mode==44:env['TAG_RECORD']='tag-reserved'
  if mode==33:env['TAG_MUTATE']='1'
  if mode==35:env['TAG_MUTATE']='2'
  subprocess.run([str(exe),str(mode)],check=True,timeout=30,cwd=tmp_path,env=env)
