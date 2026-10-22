#!/usr/bin/env python3
"""Real Xtensa/Arduino/IDF SDK-OFF compilation, no MODULE_HOST_TEST or BLE includes.

Only generated module declarations are supplied in scratch; Config, FreeRTOS,
Arduino, IDF, Tesla headers and translation units are the production files.
This compile-only probe is NOT a linked WARP4 firmware or packaging result.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'software/tools'))
from bench_confinement import output_plan
SDK = ROOT/'software/packages/esp32-arduino-libs#tf-esp-secure-20260922_4f0ab20592113e12c1792ba670c48aad04ceb288/esp32'
PACKAGES = Path(os.environ.get('PLATFORMIO_CORE_DIR', Path.home()/'.platformio'))/'packages'
CORE = PACKAGES/'framework-arduinoespressif32'
DEPS = ROOT/'software/.pio/libdeps/warp4'
CC = PACKAGES/'toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-g++'
MOD = ROOT/'software/src/modules/tesla_vehicle'

def main():
    with tempfile.TemporaryDirectory(prefix='r13-sdk-off-', dir=os.environ['TMPDIR']) as tmp:
        out = Path(tmp)
        output_plan(roots=(out,)).revalidate()
        (out/'generated').mkdir()
        (out/'generated/module_dependencies.h').write_text('#pragma once\n#include "modules/api/api.h"\n#include "modules/ev/ev.h"\nextern API api;\nextern Ev ev;\n')
        (out/'generated/module_available.h').write_text('#pragma once\n#define MODULE_METERS_ISO15118_AVAILABLE() 1\n')
        (out/'generated/module_available_end.h').write_text('')
        enums = out/'modules/meters_iso15118/generated'
        enums.mkdir(parents=True)
        values = (ROOT/'software/src/modules/meters_iso15118/EV Data Source.uint8.enum').read_text().splitlines()
        # A prior frontend generation may have populated real generated files.
        # Never shadow that same enum with a second scratch declaration.
        if not (ROOT/'software/src/modules/meters_iso15118/generated/ev_data_source.enum.h').exists():
            (enums/'ev_data_source.enum.h').write_text('#pragma once\nenum class EVDataSource : uint8_t {'+','.join(values)+'};\n')
        includes = [out, ROOT/'software/src', ROOT/'software/lib/TeslaBLEClean/include',
                    CORE/'cores/esp32', CORE/'variants/esp32', CORE/'libraries/FS/src',
                    CORE/'libraries/LittleFS/src', SDK/'qio_qspi/include']
        for dep in DEPS.iterdir():
            if dep.is_dir(): includes += [dep, dep/'src', dep/'include']
        # No bt/bluedroid/NimBLE/Arduino BLE header directory enters this probe.
        sdk_paths = (SDK/'flags/includes').read_text().split('-iwithprefixbefore ')
        includes += [SDK/'include'/p.strip() for p in sdk_paths if p.strip() and not p.strip().startswith('bt/')]
        common = [str(CC), '-std=gnu++2b', '-fno-exceptions', '-fno-rtti', '-DARDUINO=10819', '-DESP_PLATFORM', '-DARDUINO_ARCH_ESP32',
                  *('-I'+str(p) for p in includes)]
        # Actual stock sdkconfig has BT OFF; verify it instead of defining a test override.
        macros = subprocess.check_output([*common, '-dM', '-E', '-x', 'c++', '-include', 'sdkconfig.h', '/dev/null'], text=True)
        assert not re.search(r'^#define CONFIG_BT_ENABLED 1$', macros, re.M)
        failures = 0
        for source in ('tesla_vehicle.cpp','tesla_ble_gatt.cpp','tesla_ble_transport.cpp','tesla_ble_storage.cpp'):
            obj = out/(source+'.o'); dep = out/(source+'.d')
            result = subprocess.run([*common, '-MMD', '-MF', str(dep), '-c', str(MOD/source), '-o', str(obj)], text=True, capture_output=True)
            print(source, 'exit', result.returncode, result.stderr, flush=True)
            if result.returncode: failures += 1; continue
            dependencies = dep.read_text()
            assert 'module_platform.h' not in dependencies
            assert not re.search(r'esp_gattc_api.h|tesla_ble_uuid.h|/BLE(?:Device|Address|UUID).h|/include/bt/', dependencies)
            symbols = subprocess.check_output([str(CC.with_name('xtensa-esp32-elf-nm')), '-C', str(obj)], text=True)
            assert not re.search(r'BLEDevice::|esp_ble_|esp_bt_|esp_gatt|gatt_callback', symbols), symbols
            preprocessed = subprocess.check_output([*common, '-E', str(MOD/source)], text=True)
            assert not re.search(r'esp_gattc_cb_event_t|esp_gatt_if_t|esp_ble_gattc_cb_param_t|esp_bt_uuid_t', preprocessed)
        # Actual hardware-OFF macro branch in addition to stock SDK-OFF.
        (out/'soc').mkdir()
        (out/'soc/soc_caps.h').write_text('#pragma once\n#define SOC_BT_SUPPORTED 0\n')
        probe = out/'capability.cpp'
        probe.write_text('#include "bluetooth_support.h"\nstatic_assert(WARP_BLUETOOTH_COMPILED == 0);\nint main() { return bluetooth_supported(); }\n')
        subprocess.run([*common, '-I'+str(MOD), '-c', str(probe), '-o', str(out/'hardware-off.o')], check=True)
        print('production SDK-OFF objects failures:', failures)
        return int(bool(failures))
if __name__ == '__main__': raise SystemExit(main())
