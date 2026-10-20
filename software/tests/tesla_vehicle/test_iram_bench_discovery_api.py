#!/usr/bin/env python3
import os
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_iram_bench_discovery import DSDK, HARNESS, MODULE, pairing_link_args
from test_iram_bench_smoke_api import PLATFORM

start=PLATFORM.index('struct FakeServer {')
end=PLATFORM.index('inline FakeServer server;')+len('inline FakeServer server;')
PLATFORM=PLATFORM[:start]+r'''
constexpr int HTTP_GET=2;
struct FakeServer {
 bool effective_auth=true,verified_request=true;
 bool authenticate_strict_HTTPThread(WebServerRequest) { return effective_auth&&verified_request; } // Real middleware/digest code covered separately.
 std::map<std::string,std::function<WebServerRequestReturnProtect(WebServerRequest)>> routes;
 void on(const char *path,int method,std::function<WebServerRequestReturnProtect(WebServerRequest)> cb) {
  assert(method==(std::string(path)=="/tesla_vehicle/bench_discovery_state"?HTTP_GET:HTTP_PUT)); routes[path]=cb;
 }
};
inline FakeServer server;
'''+PLATFORM[end:]
PLATFORM=PLATFORM.replace('struct WebServerRequest {','inline std::string last_json;\nstruct WebServerRequest {').replace('IPAddress getPeerAddress()', 'WebServerRequestReturnProtect send_json(int code,const char *s) { last_json=s; return {code}; }\n IPAddress getPeerAddress()')
PLATFORM=PLATFORM.replace('struct WebServerRequest {','struct WebServerRequest {\n bool strict_evidence=true; bool is_strictly_authenticated() const {return strict_evidence;}')
MAIN=r'''
#include "platform.h"
#include <cstdio>
int main(int argc,char **argv) {
 const char *scenario=argc>1?argv[1]:"success";
 char synthetic_vin[18]; memset(synthetic_vin,'A',17); synthetic_vin[17]=0;
 if(!strcmp(scenario,"pairing")||!strcmp(scenario,"rehearsal")) assert(BenchPairing::vehicle_name(synthetic_vin,fixture_name));
 if(!strcmp(scenario,"overflow")) mode="overflow";
 if(!strcmp(scenario,"non_connectable")) mode="non_connectable";
 tesla_ble_bench_discovery_register_urls();
 auto &routes=server.routes;
 assert(routes.size()==9 && calls.empty() && !worker);
 for(bool enabled:{false,true}) for(bool valid:{false,true}) {
   if(enabled&&valid) {continue;}
   server.effective_auth=enabled;server.verified_request=valid;
   for(auto &entry:routes) {WebServerRequest denied;denied.strict_evidence=enabled&&valid;assert(entry.second(denied).code==403&&calls.empty()&&!worker);}
 }
 server.effective_auth=true;server.verified_request=true;
 for(const char *path:{"/tesla_vehicle/bench_pair_prepare","/tesla_vehicle/bench_pair_start","/tesla_vehicle/bench_pair_verify","/tesla_vehicle/bench_pair_delete","/tesla_vehicle/bench_pair_revoke_confirm"}) {
  WebServerRequest denied; denied.peer=0xc0a80202; assert(routes.at(path)(denied).code==403 && calls.empty() && !worker);
 }
 WebServerRequest req;
 auto scan=routes.at("/tesla_vehicle/bench_scan");
 auto probe_route=routes.at("/tesla_vehicle/bench_probe");
 auto state=routes.at("/tesla_vehicle/bench_discovery_state");
 assert(probe_route(req).code==400 && !worker);
 assert(state(req).code==200 && calls.empty());
 assert(last_json.find("\"status\":\"idle\"")!=std::string::npos);
 if(!strcmp(scenario,"off_subnet")) req.peer=0xc0a80202;
 if(!strcmp(scenario,"wrong_local")) req.local=0x0a7b0002;
 if(!strcmp(scenario,"ipv6")) req.v4=false;
 if(!strcmp(scenario,"netif_down")) netif_up=false;
 if(!strcmp(scenario,"broadcast")) req.peer=0xc0a801ff;
 if(!strcmp(scenario,"network")) req.peer=0xc0a80100;
 if(!strcmp(scenario,"self")) req.peer=req.local;
 if(!strcmp(scenario,"bad_payload")) req.body="{\"vin\":\"fake\"}";
 if(!strcmp(scenario,"empty")) req.body="";
 if(strcmp(scenario,"success") && strcmp(scenario,"empty") && strcmp(scenario,"overflow") && strcmp(scenario,"non_connectable") && strcmp(scenario,"pairing") && strcmp(scenario,"rehearsal")) {
  const int expected=!strcmp(scenario,"bad_payload")?400:403;
  assert(scan(req).code==expected && !worker && calls.empty());
  if(expected==403) { assert(state(req).code==403); req.body="{\"target\":0}"; assert(probe_route(req).code==403); }
  return 0;
 }
 assert(scan(req).code==202 && scan(req).code==409 && calls.empty()); worker(context);
 if(!strcmp(scenario,"pairing")||!strcmp(scenario,"rehearsal")) {
  auto prepare=routes.at("/tesla_vehicle/bench_pair_prepare"), start=routes.at("/tesla_vehicle/bench_pair_start"), verify=routes.at("/tesla_vehicle/bench_pair_verify"), remove=routes.at("/tesla_vehicle/bench_pair_delete"), revoke=routes.at("/tesla_vehicle/bench_pair_revoke_confirm");
  calls.clear();
  for(const char *bad:{"{}","{\"target\":0}","{\"target\":0,\"vin\":\"\"}"}) {req.body=bad; assert(prepare(req).code==400 && calls.empty());}
  req.body="{\"target\":0,\"vin\":\""+std::string(synthetic_vin)+"\"}"; assert(prepare(req).code==202 && prepare(req).code==409); worker(context);
  assert(state(req).code==200); printf("%s\n",last_json.c_str());
  if(!strcmp(scenario,"rehearsal")){
   auto rehearsal=routes.at("/tesla_vehicle/bench_pair_rehearsal");req.body="{\"send\":true}";assert(rehearsal(req).code==400);
   calls.clear();req.body="{}";assert(rehearsal(req).code==202&&rehearsal(req).code==409);worker(context);
   assert(state(req).code==200&&sent_wire.empty());printf("%s\n",last_json.c_str());return 0;
  }
  for(auto &action:{start,verify,remove,revoke}) {req.body="{\"unexpected\":true}"; assert(action(req).code==400);}
  req.body="{\"confirm_card_and_display\":true}"; assert(start(req).code==202 && start(req).code==409); worker(context);
  assert(state(req).code==200); printf("%s\n",last_json.c_str());
  req.body="{}"; assert(verify(req).code==202 && verify(req).code==409); worker(context);
  assert(state(req).code==200); printf("%s\n",last_json.c_str());
  calls.clear(); req.body="{\"tesla_removed_by_owner\":true}"; assert(revoke(req).code==200 && calls.empty());
  req.body="{\"ack_unresolved\":true}"; assert(remove(req).code==202 && remove(req).code==409);
  assert(state(req).code==200 && last_json.find("\"key_effect\":\"local_delete_pending\"")!=std::string::npos && last_json.find("\"local_key_presence\":\"pending\"")!=std::string::npos);
  worker(context);
  assert(calls.empty() && state(req).code==200); printf("%s\n",last_json.c_str()); return 0;
 }
 assert(state(req).code==200);
 assert(last_json.find("01:02:03:04:05:06")!=std::string::npos);
 assert(last_json.find("\"pairing_available\":true")!=std::string::npos);
 printf("%s\n",last_json.c_str());
 if(!strcmp(scenario,"non_connectable")) { calls.clear(); req.body="{\"target\":0}"; assert(probe_route(req).code==409 && calls.empty()); assert(state(req).code==200); printf("%s\n",last_json.c_str()); return 0; }
 for(const char *bad:{"", "{}", "{\"target\":-1}", "{\"target\":00}", "{\"target\":8}", "{\"target\":0,\"pair\":true}"}) { req.body=bad; assert(probe_route(req).code==400); }
 if(strcmp(scenario,"overflow")) { req.body="{\"target\":7}"; assert(probe_route(req).code==409); }
 req.body="{\"target\":0}"; assert(probe_route(req).code==202); worker(context);
 assert(state(req).code==200 && last_json.find("\"transport_ready\":true")!=std::string::npos);
 printf("%s\n",last_json.c_str());
 assert(probe_route(req).code==409);
}
'''
class BenchDiscoveryApiTests(unittest.TestCase):
    def test_http_gate_explicit_selection_and_state(self):
        with tempfile.TemporaryDirectory() as directory:
            t=Path(directory)
            (t/'sdk.h').write_text(DSDK); (t/'platform.h').write_text(PLATFORM)
            (t/'freertos').mkdir()
            for h in ('esp_bt.h','esp_bt_main.h','esp_gattc_api.h','esp_gap_ble_api.h','esp_heap_caps.h','esp_rom_sys.h','esp_random.h','freertos/FreeRTOS.h','freertos/task.h'):
                (t/h).write_text('#include "sdk.h"\n')
            for h in ('main_dependencies.h','esp_netif.h'):
                (t/h).write_text('#include "platform.h"\n')
            harness=HARNESS.replace('static std::vector<uint8_t> sent_wire;','static std::vector<uint8_t> sent_wire;\nstatic char fixture_name[19];').replace('memcpy(p.scan_rst.ble_adv+5,"S0123456789abcdefC",18);','memcpy(p.scan_rst.ble_adv+5,fixture_name[0]?fixture_name:"S0123456789abcdefC",18);')
            (t/'test.cpp').write_text(harness+MAIN)
            (t/'api.cpp').write_bytes((MODULE/'tesla_ble_bench_discovery_api.cpp').read_bytes())
            cmd=['g++','-std=c++17','-Wall','-Wextra','-Werror','-pthread','-DTESLA_BLE_BENCH_ONLY=1','-DTESLA_BLE_IRAM_DIAGNOSTICS=1','-I'+str(t),'-I'+str(MODULE),str(t/'test.cpp'),str(MODULE/'tesla_ble_bench_discovery.cpp'),str(t/'api.cpp'),'-o',str(t/'test')]
            cmd+=pairing_link_args(t)
            if os.environ.get('TESLA_BLE_SMOKE_SANITIZE')=='1':
                cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
            p=subprocess.run(cmd,capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stderr)
            for scenario in ('success','empty','overflow','non_connectable','off_subnet','wrong_local','ipv6','netif_down','broadcast','network','self','bad_payload','pairing','rehearsal'):
                with self.subTest(scenario=scenario):
                    p=subprocess.run([str(t/'test'),scenario],capture_output=True,text=True)
                    self.assertEqual(p.returncode,0,p.stderr)
                    if scenario in ('success','empty','overflow','non_connectable'):
                        states=[json.loads(line) for line in p.stdout.splitlines()]
                        self.assertEqual(len(states),2)
                        for state in states:
                            self.assertEqual(state['target_count'],len(state['targets']))
                            self.assertEqual(state['target_count'],8 if scenario=='overflow' else 1)
                            self.assertTrue(state['measurements_valid'])
                            self.assertEqual(state['worker_stack_min_bytes'],4096)
                            self.assertEqual(state['key_effect'],'untouched')
                            self.assertEqual(state['pair_state'],'no_key')
                            self.assertEqual(state['local_key_presence'],'not_inspected')
                            self.assertFalse(state['vin_bound'])
                            self.assertEqual(state['scan_seconds'],30)
                            self.assertEqual(state['scan_type'],'active')
                            for target in state['targets']:
                                self.assertEqual(target['local_name'],'S0123456789abcdefC')
                                self.assertEqual(target['connectable'],scenario!='non_connectable')
                                self.assertEqual(target['address_type'],1)
                                self.assertEqual(target['rssi'],-45)
                                self.assertEqual(target['evidence'],'tesla_local_name_shape')
                        self.assertEqual(states[-1]['selected_target'],-1 if scenario=='non_connectable' else 0)
                        if scenario=='non_connectable':
                            self.assertFalse(states[-1]['probe_attempted'])
                            self.assertFalse(states[-1]['transport_ready'])
                    if scenario=='pairing':
                        states=[json.loads(line) for line in p.stdout.splitlines()]
                        self.assertEqual([s['pair_state'] for s in states],['key_ready','authorization_unverified','error','no_key'])
                        self.assertEqual(states[-1]['tesla_side_revoke'],'owner_claim_unverified')
                        self.assertEqual(states[-1]['local_key_presence'],'absent')
                        for s in states:
                            self.assertTrue(s['vin_bound'])
                            self.assertNotIn('A'*17,json.dumps(s))
                            self.assertNotIn('PRIVATE',json.dumps(s))
                        self.assertEqual(states[0]['public_key_hex'],states[2]['public_key_hex'])
                    if scenario=='rehearsal':
                        states=[json.loads(line) for line in p.stdout.splitlines()]
                        self.assertEqual(len(states),2)
                        s=states[-1]
                        self.assertTrue(s['rehearsal_attempted'] and s['restart_required'] and s['teardown_complete'])
                        self.assertFalse(s['request_may_have_reached_tesla'] or s['transport_ready'])
                        self.assertEqual(s['resource_samples_dropped'],0)
                        self.assertGreater(len(s['resource_samples']),10)
                        self.assertTrue(any(row['stage']=='rehearsal.local' for row in s['resource_samples']))
                        for row in s['resource_samples']:
                            self.assertEqual((row['internal8_free'],row['internal8_largest'],row['internal8_min'],row['worker_stack_min_bytes']),(32768,16384,32768,4096))
if __name__=='__main__': unittest.main()
