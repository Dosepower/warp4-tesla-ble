#!/usr/bin/env python3
"""Host execution of the actual bench HTTP binding; no sockets/devices used."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_iram_bench_smoke import SDK, HARNESS, MODULE

PLATFORM = r'''
#pragma once
#include "sdk.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <cstring>
#include <cassert>
struct Config {
 std::string text; int number=0;
 std::shared_ptr<std::map<std::string,Config>> fields;
 static Config Str(const char *s,int,int) { Config c; c.text=s; return c; }
 static Config Int(int32_t i,int32_t low,int32_t high) { assert(i>=low && i<=high); Config c; c.number=i; return c; }
 static Config Bool(bool b) { return Int(b,0,1); }
 static Config Object(std::initializer_list<std::pair<const std::string,Config>> init) {
  Config c; c.fields=std::make_shared<std::map<std::string,Config>>(init); return c;
 }
 Config *get(const char *key) { return &fields->at(key); }
 void updateString(const char *s) { text=s; }
 void updateInt(int i) { number=i; }
 void updateBool(bool b) { number=b; }
};
using ConfigRoot=Config;
struct FakeApi {
 ConfigRoot *state=nullptr;
 void addState(const char *path,ConfigRoot *root) {
  assert(std::string(path)=="tesla_vehicle/bench_stack_smoke_state"); state=root;
 }
};
inline FakeApi api;
enum IPType { IPv4, IPv6 };
struct IPAddress {
 uint32_t addr; bool v4=true;
 IPType type() const { return v4 ? IPv4 : IPv6; }
 operator uint32_t() const { return addr; }
};
struct WebServerRequestReturnProtect { int code; };
struct WebServerRequest {
 uint32_t peer=0xc0a80102, local=0xc0a80103; bool v4=true;
 std::string body="{}";
 IPAddress getPeerAddress() { return {peer,v4}; }
 IPAddress getLocalAddress() { return {local,true}; }
 size_t contentLength() { return body.size(); }
 int receive(char *out,size_t size) { assert(body.size()<=size); memcpy(out,body.data(),body.size()); return body.size(); }
 WebServerRequestReturnProtect send_plain(int code,const char * = "") { return {code}; }
};
constexpr int HTTP_PUT=1;
struct FakeServer {
 std::function<WebServerRequestReturnProtect(WebServerRequest)> handler;
 void on(const char *path,int method,std::function<WebServerRequestReturnProtect(WebServerRequest)> cb) {
  assert(std::string(path)=="/tesla_vehicle/bench_stack_smoke" && method==HTTP_PUT); handler=cb;
 }
};
inline FakeServer server;
struct esp_netif_t {};
struct esp_netif_ip_info_t { struct { uint32_t addr; } ip,netmask; };
inline esp_netif_t eth;
inline bool netif_up=true;
inline esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) { assert(std::string(key)=="ETH_DEF"); return &eth; }
inline bool esp_netif_is_netif_up(esp_netif_t *) { return netif_up; }
inline int esp_netif_get_ip_info(esp_netif_t *,esp_netif_ip_info_t *info) { *info={{0xc0a80103},{0xffffff00}}; return ESP_OK; }
'''
API_MAIN = r'''
int main(int argc,char **argv) {
 mode=argc>1 ? argv[1] : "success";
 if (!std::strcmp(mode,"failure_controller")) fail_at=0;
 tesla_ble_bench_smoke_pre_setup(); tesla_ble_bench_smoke_register_urls();
 for (int i=0;i<100;++i) tesla_ble_bench_smoke_loop();
 assert(api.state && server.handler && worker==nullptr && calls.empty());
 assert(api.state->get("status")->text=="idle" && api.state->get("failure_code")->number==0);
 WebServerRequest req;
 if (!std::strcmp(mode,"off_subnet")) req.peer=0xc0a80202;
 if (!std::strcmp(mode,"wrong_local")) req.local=0x0a7b0002;
 if (!std::strcmp(mode,"ipv6")) req.v4=false;
 if (!std::strcmp(mode,"netif_down")) netif_up=false;
 if (!std::strcmp(mode,"broadcast")) req.peer=0xc0a801ff;
 if (!std::strcmp(mode,"network")) req.peer=0xc0a80100;
 if (!std::strcmp(mode,"bad_payload")) req.body="x";
 if (!std::strcmp(mode,"array_payload")) req.body="[]";
 if (!std::strcmp(mode,"identity_payload")) req.body="{\"vin\":\"NOT ALLOWED\"}";
 if (!std::strcmp(mode,"empty")) req.body="";
 const int rc=server.handler(req).code;
 if (!std::strcmp(mode,"off_subnet") || !std::strcmp(mode,"wrong_local") || !std::strcmp(mode,"ipv6") ||
     !std::strcmp(mode,"netif_down") || !std::strcmp(mode,"broadcast") || !std::strcmp(mode,"network")) {
  assert(rc==403 && worker==nullptr && calls.empty()); return 0;
 }
 if (!std::strcmp(mode,"bad_payload") || !std::strcmp(mode,"array_payload") || !std::strcmp(mode,"identity_payload")) {
  assert(rc==400 && worker==nullptr && calls.empty()); return 0;
 }
 if (!std::strcmp(mode,"task_fail")) {
  assert(rc==503 && calls.empty() && api.state->get("status")->text=="failed");
  assert(api.state->get("failure_stage")->text=="task.create" && api.state->get("failure_code")->number==ESP_ERR_NO_MEM);
  assert(api.state->get("task_create_code")->number==0 && !api.state->get("worker_active")->number);
  assert(server.handler(req).code==409); return 0;
 }
 assert(rc==202 && calls.empty() && worker);
 assert(api.state->get("status")->text=="running");
 assert(server.handler(req).code==409 && calls.empty());
 worker(context); tesla_ble_bench_smoke_loop();
 if (!std::strcmp(mode,"failure_controller") || !std::strcmp(mode,"register_event_fail")) {
  assert(api.state->get("status")->text=="failed" && !api.state->get("initialization_ok")->number);
  assert(api.state->get("failure_stage")->text==(!std::strcmp(mode,"failure_controller") ? "controller.init" : "gatt.register_event"));
  assert(api.state->get("failure_code")->number==(!std::strcmp(mode,"failure_controller") ? -77 : 133));
  assert(server.handler(req).code==409); return 0;
 }
 if (!std::strcmp(mode,"register_timeout") || !std::strcmp(mode,"unregister_timeout")) {
  assert(api.state->get("status")->text=="restart_required" && api.state->get("failure_code")->number==ESP_ERR_TIMEOUT);
  assert(api.state->get("failure_stage")->text==(!std::strcmp(mode,"register_timeout") ? "gatt.register_event" : "gatt.unregister_event"));
  if (!std::strcmp(mode,"unregister_timeout")) {
   assert(api.state->get("teardown_code")->number==ESP_ERR_TIMEOUT && !api.state->get("teardown_complete")->number);
  }
  assert(!api.state->get("worker_active")->number && api.state->get("restart_required")->number);
  assert(server.handler(req).code==409); return 0;
 }
 assert(api.state->get("status")->text=="succeeded");
 assert(api.state->get("task_create_code")->number==pdPASS);
 assert(api.state->get("initialization_ok")->number && api.state->get("teardown_complete")->number);
 assert(api.state->get("restart_required")->number && !api.state->get("worker_active")->number);
 assert(api.state->get("failure_stage")->text=="none" && api.state->get("teardown_code")->number==0);
 assert(server.handler(req).code==409);
}
'''


class BenchSmokeApiTests(unittest.TestCase):
    def test_actual_http_only_binding_admission_payload_and_state(self):
        with tempfile.TemporaryDirectory() as directory:
            temp=Path(directory)
            (temp/'sdk.h').write_text(SDK)
            for subdir in ('freertos','generated'):
                (temp/subdir).mkdir()
            for header in ('esp_bt.h','esp_bt_main.h','esp_gattc_api.h','freertos/FreeRTOS.h','freertos/task.h'):
                (temp/header).write_text('#include "sdk.h"\n')
            (temp/'platform.h').write_text(PLATFORM)
            for header in ('config.h','esp_netif.h','main_dependencies.h'):
                (temp/header).write_text('#include "platform.h"\n')
            (temp/'test.cpp').write_text('#include "platform.h"\n'+HARNESS.split('int main(int argc, char **argv)')[0]+API_MAIN)
            # Relocate the byte-identical binding to resolve its quoted generated
            # platform includes to the test shims, not the target's neighbour.
            (temp/'api.cpp').write_bytes((MODULE/'tesla_ble_bench_smoke_api.cpp').read_bytes())
            command=['g++','-std=c++17','-Wall','-Wextra','-Werror','-fno-exceptions','-fno-rtti','-pthread',
                     '-DTESLA_BLE_BENCH_ONLY=1','-DTESLA_BLE_IRAM_DIAGNOSTICS=1',
                     '-I'+str(temp),'-I'+str(MODULE),str(temp/'test.cpp'),
                     str(MODULE/'tesla_ble_bench_smoke.cpp'), str(temp/'api.cpp'),'-o',str(temp/'test')]
            if os.environ.get('TESLA_BLE_SMOKE_SANITIZE')=='1':
                command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            for scenario in ('success','empty','off_subnet','wrong_local','ipv6','netif_down','broadcast','network',
                             'bad_payload','array_payload','identity_payload','task_fail','failure_controller',
                             'register_event_fail','register_timeout','unregister_timeout'):
                with self.subTest(scenario=scenario):
                    result=subprocess.run([str(temp/'test'),scenario],capture_output=True,text=True)
                    self.assertEqual(result.returncode,0,result.stderr)

    def test_module_hooks_and_bench_runtime_lockout(self):
        main=(MODULE/'tesla_vehicle.cpp').read_text()
        for hook in ('pre_setup','register_urls','loop'):
            self.assertIn('tesla_ble_bench_smoke_'+hook+'();',main)
        runtime=main.split('bool TeslaVehicle::start_runtime(bool storage_only)',1)[1].split('bool TeslaVehicle::create_vehicle()',1)[0]
        self.assertIn('return false; // Bench smoke must never create a Tesla runtime.',runtime)
        self.assertIn('Tesla runtime is unavailable in bench stack-smoke firmware',main)
        engine=(MODULE/'tesla_ble_bench_smoke.cpp').read_text()
        for forbidden in ('BLEDevice','start_runtime(', 'esp_ble_gattc_open(', 'esp_ble_gap_', 'Vehicle(', 'xQueueCreate', 'esp_bt_mem_release(', 'esp_bt_controller_mem_release('):
            self.assertNotIn(forbidden,engine)
        api_source=(MODULE/'tesla_ble_bench_smoke_api.cpp').read_text()
        self.assertNotIn('addCommand(',api_source) # Not invocable via MQTT, automation or GET.
        self.assertNotIn('HTTP_GET',api_source)


if __name__=='__main__':
    unittest.main()
