#!/usr/bin/env python3
"""Native execution of the diagnostic SDK boundary (not a heap simulator)."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
MODULE = ROOT / "software/src/modules/tesla_vehicle"

SDK = r"""
#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
using esp_gatt_if_t = uint8_t;
using esp_bt_mode_t = int;
using esp_ble_addr_type_t = int;
struct esp_bt_controller_config_t { int marker; };
using esp_gattc_cb_t = void (*)(int, int, void *);
constexpr uint32_t MALLOC_CAP_EXEC = 1, MALLOC_CAP_INTERNAL = 2;
struct multi_heap_info_t { size_t total_free_bytes, largest_free_block, minimum_free_bytes; };
extern void heap_caps_get_info(multi_heap_info_t *, uint32_t);
extern int esp_rom_printf(const char *, ...);
"""
HARNESS = r"""
#include "sdk.h"
#include "tesla_ble_iram_diagnostics.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
static std::vector<std::string> trace, lines;
static int result = 0, real_calls = 0;
static const esp_bt_controller_config_t *config_seen;
static esp_gattc_cb_t callback_seen;
static uint8_t *address_seen;
void heap_caps_get_info(multi_heap_info_t *info, uint32_t cap) {
    trace.push_back(cap == MALLOC_CAP_EXEC ? "exec" : "internal");
    *info = cap == MALLOC_CAP_EXEC ? multi_heap_info_t{100, 80, 60} : multi_heap_info_t{200, 180, 160};
}
int esp_rom_printf(const char *format, ...) {
    char buffer[512]; va_list args; va_start(args, format);
    const int count = vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    assert(count > 0 && size_t(count) < sizeof(buffer));
    trace.push_back("serial"); lines.emplace_back(buffer); return count;
}
static int called() { trace.push_back("real"); ++real_calls; return result; }
extern "C" {
int __real_esp_bt_controller_init(esp_bt_controller_config_t *c) { config_seen=c; return called(); }
int __real_esp_bt_controller_enable(esp_bt_mode_t mode) { assert(mode==7); return called(); }
int __real_esp_bluedroid_init() { return called(); }
int __real_esp_bluedroid_enable() { return called(); }
int __real_esp_ble_gattc_register_callback(esp_gattc_cb_t cb) { callback_seen=cb; return called(); }
int __real_esp_ble_gattc_app_register(uint16_t id) { assert(id==0x5742); return called(); }
int __real_esp_ble_gattc_open(esp_gatt_if_t iface, uint8_t *address, esp_ble_addr_type_t type, bool direct) {
    assert(iface==3 && type==1 && direct); address_seen=address; return called();
}
int __wrap_esp_bt_controller_init(esp_bt_controller_config_t *);
int __wrap_esp_bt_controller_enable(esp_bt_mode_t);
int __wrap_esp_bluedroid_init(); int __wrap_esp_bluedroid_enable();
int __wrap_esp_ble_gattc_register_callback(esp_gattc_cb_t);
int __wrap_esp_ble_gattc_app_register(uint16_t);
int __wrap_esp_ble_gattc_open(esp_gatt_if_t, uint8_t *, esp_ble_addr_type_t, bool);
}
static void callback(int, int, void *) {}
template<class F> void check(const char *stage, F submit) {
    for (int rc : {0, -17, 0x101}) {
        result=rc; real_calls=0; trace.clear(); lines.clear();
        assert(submit()==rc && real_calls==1);
        assert((trace==std::vector<std::string>{"exec","internal","serial","real","exec","internal","serial"}));
        assert(lines.size()==2);
        const std::string prefix=std::string("TESLA_IRAM stage=")+stage;
        assert(lines[0].find(prefix+" phase=before rc=na ")==0);
        assert(lines[1].find(prefix+" phase=after rc="+std::to_string(rc)+" ")==0);
        for (const auto &line:lines) {
            assert(line.find("exec_free=100 exec_largest=80 exec_min=60 internal_free=200 internal_largest=180 internal_min=160")!=std::string::npos);
        }
    }
}
int main() {
    esp_bt_controller_config_t cfg{42}; uint8_t address[6]{};
    check("controller.init", [&] { return __wrap_esp_bt_controller_init(&cfg); }); assert(config_seen==&cfg);
    check("controller.enable", [] { return __wrap_esp_bt_controller_enable(7); });
    check("host.init", [] { return __wrap_esp_bluedroid_init(); });
    check("host.enable", [] { return __wrap_esp_bluedroid_enable(); });
    check("gatt.callback_register", [] { return __wrap_esp_ble_gattc_register_callback(callback); }); assert(callback_seen==callback);
    check("gatt.app_register", [] { return __wrap_esp_ble_gattc_app_register(0x5742); });
    check("gatt.open", [&] { return __wrap_esp_ble_gattc_open(3,address,1,true); }); assert(address_seen==address);
    lines.clear(); tesla_ble_iram_snapshot("gatt.open", "event", 133, true);
    assert(lines.size()==1 && lines[0].find("phase=event rc=133 ")!=std::string::npos);
}
"""


class IramDiagnosticsTests(unittest.TestCase):
    def compile_run(self, enabled):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / "sdk.h").write_text(SDK)
            for header in ("esp_bt.h", "esp_bt_main.h", "esp_gattc_api.h", "esp_heap_caps.h", "esp_rom_sys.h"):
                (temp / header).write_text('#include "sdk.h"\n')
            source = HARNESS if enabled else '#include "tesla_ble_iram_diagnostics.h"\nint main() { tesla_ble_iram_snapshot("disabled", "boot"); }\n'
            (temp / "test.cpp").write_text(source)
            command = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fno-exceptions", "-fno-rtti",
                       "-I" + str(temp), "-I" + str(MODULE), str(temp / "test.cpp"),
                       str(MODULE / "tesla_ble_iram_diagnostics.cpp"), "-o", str(temp / "test")]
            if enabled:
                command.insert(1, "-DTESLA_BLE_IRAM_DIAGNOSTICS=1")
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(temp / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_wrappers_measure_before_after_and_preserve_arguments_and_all_return_codes(self):
        self.compile_run(True)

    def test_disabled_build_needs_no_sdk_heap_serial_or_bluetooth_symbols(self):
        self.compile_run(False)

    def test_async_status_and_disabled_boot_checkpoints_are_bound(self):
        main = (MODULE / "tesla_vehicle.cpp").read_text()
        setup = main.split("void TeslaVehicle::setup()", 1)[1].split("void TeslaVehicle::register_urls()", 1)[0]
        self.assertIn('tesla_ble_iram_snapshot("module.setup", "boot")', setup)
        self.assertLess(setup.index('tesla_ble_iram_snapshot'), setup.index('if (config.get("enable")->asBool())'))
        gatt = (MODULE / "tesla_ble_gatt.cpp").read_text()
        self.assertIn('tesla_ble_iram_snapshot("gatt.app_register", "event", param->reg.status, true)', gatt)
        self.assertIn('tesla_ble_iram_snapshot("gatt.open", "event", param->open.status, true)', gatt)

    def test_minimal_variant_is_separate_and_retains_client_and_task_stack_sizes(self):
        ini = (ROOT / "software/warp4_tesla_ble.ini").read_text()
        self.assertIn("[env:warp4_tesla_ble_iram_diag]", ini)
        self.assertIn("[env:warp4_tesla_ble_iram_minimal]", ini)
        self.assertIn("${sysenv.TESLA_BLE_MINIMAL_FRAMEWORK_LIBS}", ini)
        config = (ROOT / "software/lib-builder/defconfig.esp32ethernetbrick.ble-minimal").read_text()
        for setting in ("CONFIG_BT_ACL_CONNECTIONS=1", "CONFIG_BTDM_CTRL_BLE_MAX_CONN=1", "CONFIG_BT_GATTC_ENABLE=y",
                        "# CONFIG_BT_GATTS_ENABLE is not set", "# CONFIG_BT_BLE_SMP_ENABLE is not set"):
            self.assertIn(setting, config)
        self.assertNotIn("TASK_STACK_SIZE", config)


if __name__ == "__main__":
    unittest.main()
