#!/usr/bin/env python3
"""Bench-only byte-heap and RTOS failure instrumentation, real wrapper source."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_iram_diagnostics import SDK, MODULE

HARNESS = r'''
#include "sdk.h"
#include "tesla_ble_iram_diagnostics.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
static std::vector<uint32_t> caps;
static std::vector<std::string> lines;
static bool fail=false;
static int allocation_calls;
static char allocation;
void heap_caps_get_info(multi_heap_info_t *info, uint32_t cap) {
 caps.push_back(cap); *info={100,80,60};
 if(cap==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)) *info={20,16,12};
}
int esp_rom_printf(const char *format, ...) {
 char buffer[512]; va_list args; va_start(args,format); int n=vsnprintf(buffer,sizeof(buffer),format,args); va_end(args);
 assert(n>0 && size_t(n)<sizeof(buffer)); lines.emplace_back(buffer); return n;
}
extern "C" {
void *__real_pvPortMalloc(size_t size) { assert(size==8704); ++allocation_calls; return fail ? nullptr : &allocation; }
void *__wrap_pvPortMalloc(size_t);
int __real_esp_bt_controller_init(esp_bt_controller_config_t *) { return 0; }
int __real_esp_bt_controller_enable(esp_bt_mode_t) { return 0; }
int __real_esp_bluedroid_init() { return -1; }
int __real_esp_bluedroid_enable() { return 0; }
int __real_esp_ble_gattc_register_callback(esp_gattc_cb_t) { return 0; }
int __real_esp_ble_gattc_app_register(uint16_t) { return 0; }
int __real_esp_ble_gattc_open(esp_gatt_if_t,uint8_t *,esp_ble_addr_type_t,bool) { return 0; }
}
int main() {
 tesla_ble_iram_snapshot("host.init","before");
 assert((caps==std::vector<uint32_t>{MALLOC_CAP_EXEC,MALLOC_CAP_INTERNAL,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT}));
 assert(lines.size()==1 && lines[0].find("internal8_free=20 internal8_largest=16 internal8_min=12")!=std::string::npos);
 lines.clear(); assert(__wrap_pvPortMalloc(8704)==&allocation && allocation_calls==1 && lines.empty());
 fail=true; assert(__wrap_pvPortMalloc(8704)==nullptr && allocation_calls==2);
 assert(lines.size()==1 && lines[0].find("TESLA_BENCH_ALLOC function=pvPortMalloc bytes=8704 caps=0x00000006")!=std::string::npos);
}
'''


class BenchMemoryTests(unittest.TestCase):
    def test_byte_capability_view_and_nonallocating_rtos_failure_wrapper(self):
        with tempfile.TemporaryDirectory(prefix="warp-bench-memory-") as temporary:
            root = Path(temporary)
            (root / "sdk.h").write_text(SDK + '\nconstexpr uint32_t MALLOC_CAP_8BIT=4;\n')
            for name in ("esp_bt.h", "esp_bt_main.h", "esp_gattc_api.h", "esp_heap_caps.h", "esp_rom_sys.h"):
                (root / name).write_text('#include "sdk.h"\n')
            (root / "test.cpp").write_text(HARNESS)
            command = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fno-exceptions", "-fno-rtti",
                       "-DTESLA_BLE_BENCH_ONLY=1", "-DTESLA_BLE_IRAM_DIAGNOSTICS=1",
                       "-I"+str(root), "-I"+str(MODULE), str(root / "test.cpp"),
                       str(MODULE / "tesla_ble_iram_diagnostics.cpp"), "-o", str(root / "test")]
            if os.environ.get("TESLA_BLE_SMOKE_SANITIZE") == "1":
                command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g", "-no-pie"]
            compile_result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run_result = subprocess.run([str(root / "test")], capture_output=True, text=True)
            self.assertEqual(run_result.returncode, 0, run_result.stderr)

    def test_rtos_wrapper_is_absent_from_c1_diagnostic_inputs(self):
        from test_iram_diagnostics import IramDiagnosticsTests
        IramDiagnosticsTests().compile_run(True)  # Existing heap/view behavior remains unchanged.
        ini = (Path(__file__).resolve().parents[3] / "software/warp4_tesla_ble.ini").read_text()
        production, bench = ini.split("[env:warp4_tesla_ble_iram_bench]", 1)
        self.assertNotIn("--wrap=pvPortMalloc", production)
        self.assertNotIn("--wrap=pvPortMalloc", bench)

    def test_disabled_and_single_flag_objects_exclude_new_instrumentation(self):
        with tempfile.TemporaryDirectory(prefix="warp-bench-memory-gates-") as temporary:
            root = Path(temporary)
            (root / "sdk.h").write_text(SDK + '\nconstexpr uint32_t MALLOC_CAP_8BIT=4;\n')
            for name in ("esp_bt.h", "esp_bt_main.h", "esp_gattc_api.h", "esp_heap_caps.h", "esp_rom_sys.h"):
                (root / name).write_text('#include "sdk.h"\n')
            for flags in ((), ("TESLA_BLE_BENCH_ONLY=1",), ("TESLA_BLE_IRAM_DIAGNOSTICS=1",),
                          ("TESLA_BLE_BENCH_ONLY=0", "TESLA_BLE_IRAM_DIAGNOSTICS=1"),
                          ("TESLA_BLE_BENCH_ONLY=1", "TESLA_BLE_IRAM_DIAGNOSTICS=0")):
                with self.subTest(flags=flags):
                    subprocess.run(["g++", "-std=c++17", "-I"+str(root), "-I"+str(MODULE),
                                    *["-D"+flag for flag in flags], "-c",
                                    str(MODULE / "tesla_ble_iram_diagnostics.cpp"), "-o", str(root / "gate.o")], check=True)
                    symbols = subprocess.check_output(["nm", "--defined-only", str(root / "gate.o")], text=True)
                    self.assertNotIn("__wrap_pvPortMalloc", symbols)
                    self.assertNotIn(b"internal8_free=", (root / "gate.o").read_bytes())


if __name__ == "__main__":
    unittest.main()
