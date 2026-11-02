"""LSP1 focused host tests (unit + probe-ON production module modes, normal and sanitize). Host only."""
from pathlib import Path
import os, subprocess, sys, tempfile
H = Path(__file__).resolve().parent; T = H.parent; R = T.parents[2]
sys.path.insert(0, str(T)); from host_dependencies import cache, mbed
san = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-no-pie']
MODES = os.environ.get('LSP_MODES', 'init-fail host-create-fail stop-fail foreign-stack quiesce-error-delayed pending-connect-stop late-connect-stop connect-repeat multi idle-enabled').split()
with tempfile.TemporaryDirectory(prefix='lsp-', dir=os.environ['TMPDIR']) as td:
    d = Path(td)
    for m in ('normal', 'sanitize'):
        x = san if m == 'sanitize' else []
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread', '-DLSP_UNIT_TEST=1', '-DTESLA_LIFECYCLE_STACK_PROBE=1', *x, str(H / 'test_lsp_unit.cpp'), '-o', str(d / f'unit-{m}')], check=True)
        for mode in ('ok', 'allocfail', 'uninit'): subprocess.run([str(d / f'unit-{m}'), mode], check=True, timeout=30)
        flags = ['-DTESLA_NIMBLE_HOST_TEST=1', '-DTESLA_BLE_USE_NIMBLE=1', '-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192', '-DCONFIG_BT_NIMBLE_PINNED_TO_CORE=0', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-fno-exceptions', '-fno-rtti', '-pthread', '-DTESLA_BLE_MODULE_HOST_TEST', '-DTESLA_BLE_HOST_FILESYSTEM', '-DTESLA_BLE_MANUAL_VEHICLE_UI=1', '-DBLUETOOTH_CONFIG_PATH="' + str(d / ('config-' + m)) + '"', '-I' + str(T / 'host'), '-I' + str(R / 'software/lib/TeslaBLEClean/include'), '-I' + str(mbed / 'include'), '-DTESLA_LIFECYCLE_STACK_PROBE=1', *x]
        src = [H / 'test_lsp_module.cpp'] + [R / 'software/src/modules/tesla_vehicle' / n for n in ('tesla_ble_storage.cpp', 'tesla_ble_transport.cpp', 'tesla_ble_gatt.cpp', 'bluetooth_lifecycle.cpp', 'tesla_nimble_central.cpp')] + list((R / 'software/lib/TeslaBLEClean/src').glob('*.cpp'))
        libs = [cache / 'mbedtls' / n for n in ('library/libmbedcrypto.a', '3rdparty/everest/libeverest.a', '3rdparty/p256-m/libp256m.a')]
        subprocess.run(['g++', *flags, *map(str, src), *map(str, libs), '-o', str(d / f'mod-{m}')], check=True)
        for mode in MODES: subprocess.run([str(d / f'mod-{m}'), mode], check=True, timeout=60)
        print(f'LSP {m} PASS modes={len(MODES)}')
