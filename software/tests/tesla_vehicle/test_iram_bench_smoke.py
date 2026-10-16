#!/usr/bin/env python3
"""Execute the real bench smoke lifecycle with injected SDK outcomes, NOT target measurements."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
MODULE = ROOT / "software/src/modules/tesla_vehicle"
SDK = r'''
#pragma once
#include <cstdint>
#include <mutex>
using esp_err_t = int;
using esp_gatt_if_t = uint8_t;
using esp_bt_mode_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=0x101, ESP_ERR_INVALID_STATE=0x103, ESP_ERR_TIMEOUT=0x107;
constexpr int ESP_BT_MODE_BLE=1, ESP_BT_CONTROLLER_STATUS_IDLE=0, ESP_BLUEDROID_STATUS_UNINITIALIZED=0;
constexpr int ESP_GATT_OK=0, ESP_GATT_IF_NONE=0xff;
enum esp_gattc_cb_event_t { ESP_GATTC_REG_EVT, ESP_GATTC_UNREG_EVT };
struct esp_ble_gattc_cb_param_t { struct { int status; uint16_t app_id; } reg; };
using esp_gattc_cb_t = void (*)(esp_gattc_cb_event_t, esp_gatt_if_t, esp_ble_gattc_cb_param_t *);
struct esp_bt_controller_config_t { int marker; };
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() {12345}
extern "C" {
int esp_bt_controller_get_status(); int esp_bluedroid_get_status();
int esp_bt_controller_init(esp_bt_controller_config_t *); int esp_bt_controller_enable(esp_bt_mode_t);
int esp_bluedroid_init(); int esp_bluedroid_enable();
int esp_ble_gattc_register_callback(esp_gattc_cb_t); int esp_ble_gattc_app_register(uint16_t);
int esp_ble_gattc_app_unregister(esp_gatt_if_t);
int esp_bluedroid_disable(); int esp_bluedroid_deinit();
int esp_bt_controller_disable(); int esp_bt_controller_deinit();
}
using TickType_t=uint32_t;
using TaskHandle_t=void *;
using TaskFunction_t=void (*)(void *);
using portMUX_TYPE=std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(m) (m)->lock()
#define portEXIT_CRITICAL(m) (m)->unlock()
#define pdMS_TO_TICKS(ms) (ms)
constexpr int pdPASS=1;
TickType_t xTaskGetTickCount();
int xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t, void *, int, TaskHandle_t *, int);
void vTaskDelay(TickType_t); void vTaskDelete(TaskHandle_t);
'''
HARNESS = r'''
#include "sdk.h"
#include "tesla_ble_bench_smoke.h"
#include <cassert>
#include <string>
#include <vector>
#include <cstring>
static std::vector<std::string> calls;
static TaskFunction_t worker;
static void *context;
static esp_gattc_cb_t callback;
static uint32_t ticks;
static int fail_at=-1, invocation=0, controller_status=0, host_status=0;
static uint16_t app_id;
static const char *mode="success";
static int call(const char *name) {
 calls.emplace_back(name);
 if ((!std::strcmp(mode,"blocked") && calls.size()==1) ||
     (!std::strcmp(mode,"blocked_unregister_timeout") && calls.size()==6) ||
     (!std::strcmp(mode,"blocked_cleanup") && calls.size()==8)) {
  ticks+=6000; tesla_ble_bench_smoke.supervise();
  auto s=tesla_ble_bench_smoke.snapshot();
  assert(std::string(s.status)=="restart_required" && s.worker_active && s.failure_code==ESP_ERR_TIMEOUT);
  assert(!tesla_ble_bench_smoke.request());
 }
 if (!std::strcmp(mode,"slow_final_return") && calls.size()==11) ticks+=6000;
 return invocation++==fail_at ? -77 : 0;
}
extern "C" {
int esp_bt_controller_get_status() { return controller_status; }
int esp_bluedroid_get_status() { return host_status; }
int esp_bt_controller_init(esp_bt_controller_config_t *cfg) { assert(cfg->marker==12345); return call("controller.init"); }
int esp_bt_controller_enable(esp_bt_mode_t mode) { assert(mode==ESP_BT_MODE_BLE); return call("controller.enable"); }
int esp_bluedroid_init() { return call("host.init"); }
int esp_bluedroid_enable() { return call("host.enable"); }
int esp_ble_gattc_register_callback(esp_gattc_cb_t cb) { callback=cb; return call("gatt.callback_register"); }
int esp_ble_gattc_app_register(uint16_t id) {
 app_id=id; int rc=call("gatt.app_register");
 if (!rc && !std::strcmp(mode,"ignored_events")) {
  esp_ble_gattc_cb_param_t wrong{{ESP_GATT_OK, uint16_t(id+1)}};
  callback(ESP_GATTC_REG_EVT,7,&wrong); callback(ESP_GATTC_REG_EVT,7,nullptr);
 }
 if (!rc && std::strcmp(mode,"register_timeout") && std::strcmp(mode,"ignored_events")) {
  esp_ble_gattc_cb_param_t p{{std::strcmp(mode,"register_event_fail") ? ESP_GATT_OK : 133, id}};
  callback(ESP_GATTC_REG_EVT, 7, &p);
 }
 return rc;
}
int esp_ble_gattc_app_unregister(esp_gatt_if_t iface) {
 assert(iface==7); int rc=call("gatt.app_unregister");
 if (!rc && std::strcmp(mode,"unregister_timeout") && std::strcmp(mode,"blocked_unregister_timeout")) callback(ESP_GATTC_UNREG_EVT, iface, nullptr);
 return rc;
}
int esp_bluedroid_disable() { return call("host.disable"); }
int esp_bluedroid_deinit() { return call("host.deinit"); }
int esp_bt_controller_disable() { return call("controller.disable"); }
int esp_bt_controller_deinit() { return call("controller.deinit"); }
}
TickType_t xTaskGetTickCount() { return ticks; }
int xTaskCreatePinnedToCore(TaskFunction_t fn, const char *, uint32_t stack, void *arg, int priority, TaskHandle_t *handle, int core) {
 assert(stack==4096 && priority==1 && core==0); worker=fn; context=arg; *handle=reinterpret_cast<void *>(1);
 return std::strcmp(mode,"task_fail") ? pdPASS : 0;
}
void vTaskDelay(TickType_t delay) { ticks+=delay; }
void vTaskDelete(TaskHandle_t) {}
void tesla_ble_iram_snapshot(const char *stage, const char *phase, int rc, bool valid) {
 assert(std::string(stage)=="gatt.app_register" && std::string(phase)=="event" && valid);
 assert(rc==ESP_GATT_OK || rc==133);
}
int main(int argc, char **argv) {
 if (argc>1) mode=argv[1];
 if (argc>2) fail_at=std::stoi(argv[2]);
 if (!std::strcmp(mode,"controller_owned")) controller_status=1;
 if (!std::strcmp(mode,"host_owned")) host_status=1;
 assert(std::string(tesla_ble_bench_smoke.snapshot().status)=="idle" && calls.empty() && worker==nullptr);
 const bool accepted=tesla_ble_bench_smoke.request();
 if (!std::strcmp(mode,"task_fail")) {
  auto s=tesla_ble_bench_smoke.snapshot();
  assert(!accepted && std::string(s.status)=="failed" && s.failure_code==ESP_ERR_NO_MEM);
  assert(s.task_create_code==0);
  assert(std::string(s.failure_stage)=="task.create" && !s.worker_active && calls.empty());
  assert(!tesla_ble_bench_smoke.request()); return 0;
 }
 assert(accepted);
 assert(tesla_ble_bench_smoke.snapshot().task_create_code==pdPASS);
 assert(std::string(tesla_ble_bench_smoke.snapshot().status)=="running");
 assert(!tesla_ble_bench_smoke.request() && calls.empty());
 assert(worker); worker(context);
 auto s=tesla_ble_bench_smoke.snapshot();
 assert(!s.worker_active && s.restart_required && !tesla_ble_bench_smoke.request());
 if (fail_at>=0) {
  const std::vector<std::string> ordered={"controller.init","controller.enable","host.init","host.enable",
    "gatt.callback_register","gatt.app_register","gatt.app_unregister","host.disable","host.deinit","controller.disable","controller.deinit"};
  assert(s.failure_code==-77 && std::string(s.failure_stage)==ordered.at(fail_at));
  assert(s.initialization_ok==(fail_at>=6));
  if (fail_at<6) {
   std::vector<std::string> expected(ordered.begin(),ordered.begin()+fail_at+1);
   if (fail_at>3) expected.push_back("host.disable");
   if (fail_at>2) expected.push_back("host.deinit");
   if (fail_at>1) expected.push_back("controller.disable");
   if (fail_at>0) expected.push_back("controller.deinit");
   assert(calls==expected && std::string(s.status)=="failed" && s.teardown_complete);
  } else {
   auto expected=ordered;
   if (fail_at>=7) expected.resize(fail_at+1);
   assert(calls==expected && std::string(s.status)=="restart_required" && !s.teardown_complete);
   assert(s.teardown_code==-77 && std::string(s.teardown_stage)==ordered.at(fail_at));
  }
  return 0;
 }
 if (!std::strcmp(mode,"controller_owned") || !std::strcmp(mode,"host_owned")) {
  assert(calls.empty() && s.failure_code==ESP_ERR_INVALID_STATE && std::string(s.failure_stage)=="ownership.check"); return 0;
 }
 if (!std::strcmp(mode,"blocked")) {
  assert((calls==std::vector<std::string>{"controller.init","controller.deinit"}));
  assert(std::string(s.status)=="restart_required" && s.failure_code==ESP_ERR_TIMEOUT);
  assert(std::string(s.failure_stage)=="controller.init" && !s.initialization_ok); return 0;
 }
 if (!std::strcmp(mode,"blocked_unregister_timeout")) {
  assert(std::string(s.status)=="restart_required" && !s.initialization_ok && !s.teardown_complete);
  assert(s.failure_code==ESP_ERR_TIMEOUT && std::string(s.failure_stage)=="gatt.app_register");
  assert(s.teardown_code==ESP_ERR_TIMEOUT && std::string(s.teardown_stage)=="gatt.unregister_event");
  assert(ticks>=8000 && ticks<=8100 && calls.back()=="controller.deinit"); return 0;
 }
 if (!std::strcmp(mode,"blocked_cleanup") || !std::strcmp(mode,"slow_final_return")) {
  const char *stage=!std::strcmp(mode,"blocked_cleanup") ? "host.disable" : "controller.deinit";
  assert(std::string(s.status)=="restart_required" && s.initialization_ok && !s.teardown_complete);
  assert(s.failure_code==ESP_ERR_TIMEOUT && std::string(s.failure_stage)==stage);
  assert(s.teardown_code==ESP_ERR_TIMEOUT && std::string(s.teardown_stage)==stage);
  assert(calls.back()=="controller.deinit"); return 0;
 }
 if (!std::strcmp(mode,"register_event_fail")) {
  assert(std::string(s.status)=="failed" && s.failure_code==133 && !s.initialization_ok);
  assert(std::string(s.failure_stage)=="gatt.register_event"); return 0;
 }
 if (!std::strcmp(mode,"register_timeout") || !std::strcmp(mode,"unregister_timeout") || !std::strcmp(mode,"ignored_events")) {
  assert(std::string(s.status)=="restart_required" && s.failure_code==ESP_ERR_TIMEOUT);
  assert(ticks>=2000 && ticks<=2100);
  assert(s.initialization_ok==!std::strcmp(mode,"unregister_timeout"));
  assert(calls.back()=="controller.deinit"); return 0;
 }
 assert(std::string(s.status)=="succeeded" && s.initialization_ok && s.teardown_complete && !s.worker_active);
 assert(s.restart_required && s.failure_code==0 && s.teardown_code==0);
 assert(!tesla_ble_bench_smoke.request());
 assert((calls==std::vector<std::string>{"controller.init","controller.enable","host.init","host.enable",
   "gatt.callback_register","gatt.app_register","gatt.app_unregister","host.disable","host.deinit","controller.disable","controller.deinit"}));
}
'''


class BenchSmokeTests(unittest.TestCase):
    def compile_run(self, scenario="success", defines=("TESLA_BLE_BENCH_ONLY=1", "TESLA_BLE_IRAM_DIAGNOSTICS=1"), harness=HARNESS):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / "sdk.h").write_text(SDK)
            (temp / "freertos").mkdir()
            for header in ("esp_bt.h", "esp_bt_main.h", "esp_gattc_api.h", "freertos/FreeRTOS.h", "freertos/task.h"):
                (temp / header).write_text('#include "sdk.h"\n')
            (temp / "test.cpp").write_text(harness)
            command = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fno-exceptions", "-fno-rtti", "-pthread",
                       *["-D" + flag for flag in defines], "-I" + str(temp), "-I" + str(MODULE),
                       str(temp / "test.cpp"), str(MODULE / "tesla_ble_bench_smoke.cpp"), "-o", str(temp / "test")]
            if os.environ.get("TESLA_BLE_SMOKE_SANITIZE") == "1":
                command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g", "-no-pie"]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(temp / "test"), *scenario.split()], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            if not all(flag in defines for flag in ("TESLA_BLE_BENCH_ONLY=1", "TESLA_BLE_IRAM_DIAGNOSTICS=1")):
                symbols = subprocess.check_output(["nm", "-C", str(temp / "test")], text=True)
                self.assertNotIn("tesla_ble_bench_smoke", symbols)
                self.assertNotIn("TeslaBleBenchSmoke", symbols)

    def test_explicit_one_shot_order_and_confirmed_teardown(self):
        self.compile_run()

    def test_production_and_single_flag_builds_have_no_smoke_symbols(self):
        source = '#include "tesla_ble_bench_smoke.h"\nint main() {}\n'
        for defines in ((), ("TESLA_BLE_BENCH_ONLY=1",), ("TESLA_BLE_IRAM_DIAGNOSTICS=1",),
                        ("TESLA_BLE_BENCH_ONLY=0", "TESLA_BLE_IRAM_DIAGNOSTICS=1")):
            with self.subTest(defines=defines):
                self.compile_run(defines=defines, harness=source)

    def test_failures_at_every_submission_and_teardown_stage(self):
        for stage in range(11):
            with self.subTest(stage=stage):
                self.compile_run("failure " + str(stage))

    def test_event_failure_and_timeouts_are_not_success(self):
        for scenario in ("register_event_fail", "register_timeout", "unregister_timeout", "ignored_events", "blocked",
                         "blocked_unregister_timeout", "blocked_cleanup", "slow_final_return"):
            with self.subTest(scenario=scenario):
                self.compile_run(scenario)

    def test_task_failure_and_foreign_stack_ownership(self):
        for scenario in ("task_fail", "controller_owned", "host_owned"):
            with self.subTest(scenario=scenario):
                self.compile_run(scenario)

    def test_watchdog_time_and_stage_are_sampled_under_same_lock(self):
        # A pre-lock 'now' can precede a concurrent new stage timestamp and
        # unsigned subtraction then falsely reports a multi-week timeout.
        source = (MODULE / "tesla_ble_bench_smoke.cpp").read_text()
        for signature in ("void TeslaBleBenchSmoke::supervise()", "bool TeslaBleBenchSmoke::stage(",
                          "bool TeslaBleBenchSmoke::request()"):
            body = source.split(signature, 1)[1]
            self.assertLess(body.index("portENTER_CRITICAL(&lock)"), body.index("xTaskGetTickCount()"))


if __name__ == "__main__":
    unittest.main()
