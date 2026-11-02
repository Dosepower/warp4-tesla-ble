"""Production-integration test of the NimBLE dispatcher quarantine consumer (host, test-only).

Compiles the real tesla_vehicle.cpp (main loop + admissions), bluetooth lifecycle, tesla_nimble_central.cpp and the
UNCHANGED production tesla_nimble_dispatch.cpp binding (TESLA_NIMBLE_DISPATCH=1) against host OS/SDK stubs in
host/dispatch_binding (TESLA_NIMBLE_DISPATCH_HOST_BINDING, never set by firmware builds). Runs both modes, then a
negative control: a scratch mirror with the single consume_dispatch_quarantine() call removed must FAIL.
Usage: python3 test_nimble_dispatch_quarantine.py [sanitize]
"""
from pathlib import Path
import os, shutil, subprocess, sys, tempfile
R = Path(__file__).resolve().parents[3]; T = R / 'software/tests/tesla_vehicle'
sys.path.insert(0, str(T)); from host_dependencies import cache, mbed
MODES = ['dispatch-quarantine', 'dispatch-quarantine-runtime']
CALL = '    consume_dispatch_quarantine();\n    flush_error();\n'

def build(root, out):
    t = root / 'software/tests/tesla_vehicle'; m = root / 'software/src/modules/tesla_vehicle'
    flags = ['-DTESLA_NIMBLE_HOST_TEST=1', '-DTESLA_BLE_USE_NIMBLE=1', '-DTESLA_NIMBLE_DISPATCH=1', '-DTESLA_NIMBLE_DISPATCH_HOST_BINDING=1',
             '-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192', '-DCONFIG_BT_NIMBLE_PINNED_TO_CORE=0', '-std=c++17', '-Wall', '-Wextra', '-Werror',
             '-Wno-misleading-indentation', '-fno-exceptions', '-fno-rtti', '-pthread', '-DTESLA_BLE_MODULE_HOST_TEST', '-DTESLA_BLE_HOST_FILESYSTEM',
             '-DTESLA_BLE_MANUAL_VEHICLE_UI=1', '-DBLUETOOTH_CONFIG_PATH="' + str(out.parent / 'config') + '"', '-I' + str(t / 'host/dispatch_binding'),
             '-I' + str(t / 'host'), '-I' + str(root / 'software/lib/TeslaBLEClean/include'), '-I' + str(mbed / 'include')]
    if 'sanitize' in sys.argv: flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-no-pie']
    src = [t / 'test_nimble_module.cpp'] + [m / n for n in ('tesla_ble_storage.cpp', 'tesla_ble_transport.cpp', 'tesla_ble_gatt.cpp',
           'bluetooth_lifecycle.cpp', 'tesla_nimble_central.cpp', 'tesla_nimble_dispatch.cpp')] + sorted((root / 'software/lib/TeslaBLEClean/src').glob('*.cpp'))
    libs = [cache / 'mbedtls' / n for n in ('library/libmbedcrypto.a', '3rdparty/everest/libeverest.a', '3rdparty/p256-m/libp256m.a')]
    subprocess.run(['g++', *flags, *map(str, src), *map(str, libs), '-o', str(out)], check=True)

def run(binary, mode, cwd):
    # Each mode is a fresh boot: idle mode commits bluetooth enable through the real save path into the compiled-in
    # BLUETOOTH_CONFIG_PATH (cwd/config), which must not leak into the next mode's process.
    for suffix in ('', '.bak', '.tmp'):
        f = cwd / ('config' + suffix)
        if f.exists(): print(f'fresh boot: removed {f.name} left by previous mode'); f.unlink()
    return subprocess.run([str(binary), mode], cwd=cwd, capture_output=True, text=True, timeout=60)

with tempfile.TemporaryDirectory(prefix='dispatch-quarantine-', dir=os.environ.get('TMPDIR')) as td:
    d = Path(td); ign = shutil.ignore_patterns('__pycache__', '.pio', 'node_modules', 'web')
    for rel in ('software/src', 'software/lib/TeslaBLEClean', 'software/tests/tesla_vehicle'):
        shutil.copytree(R / rel, d / 'mirror' / rel, ignore=ign)
    (d / 'pos').mkdir(); (d / 'neg').mkdir()
    build(d / 'mirror', d / 'pos/test')
    for mode in MODES:
        p = run(d / 'pos/test', mode, d / 'pos'); sys.stdout.write(p.stdout); sys.stderr.write(p.stderr)
        assert p.returncode == 0 and 'DISPATCH_QUARANTINE PASS' in p.stdout, f'{mode} rc={p.returncode}'
    vehicle = d / 'mirror/software/src/modules/tesla_vehicle/tesla_vehicle.cpp'; text = vehicle.read_text()
    assert text.count(CALL) == 1, 'consumer call site not unique'
    vehicle.write_text(text.replace(CALL, '    flush_error();\n'))
    build(d / 'mirror', d / 'neg/test')
    for mode in MODES:
        p = run(d / 'neg/test', mode, d / 'neg')
        ok = p.returncode != 0 and 'dispatch quarantine must latch runtime restart' in p.stderr
        print(f'NEGATIVE CONTROL {mode}: rc={p.returncode} {"FAILS AS REQUIRED" if ok else "UNEXPECTED"}')
        assert ok, p.stdout + p.stderr
print('DISPATCH QUARANTINE INTEGRATION PASS (2 modes + negative control without consumer fails)')
