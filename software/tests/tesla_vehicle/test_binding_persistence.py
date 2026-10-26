#!/usr/bin/env python3
"""Pairing MAC-binding persistence regression on the real target storage path.

Builds the production Tesla module (scratch copy) with its POSIX target storage
branch routed through the untouched esp_littlefs 1.22.3 VFS hooks over full
lfs.c (host/build_littlefs_vfs_harness.py) and drives the complete guided
API -> main -> worker -> mutation guard -> storage transaction pairing path.

Only the scratch copy of tesla_ble_result.h receives a publication hook that
pauses a writer exactly after its odd coherence-version store. Production and
test sources in the repository are never modified by this runner.

Usage: test_binding_persistence.py [sanitize] [--production-root DIR --expect-red]
  --production-root  take src/modules/tesla_vehicle from DIR (e.g. the original
                     snapshot) instead of this checkout; tests always from here.
  --expect-red       require the mechanism-specific RED marker for the
                     republication modes and PASS for the fail-closed controls.
"""
from pathlib import Path
import argparse, os, shutil, subprocess, sys, tempfile

S = Path(__file__).resolve().parents[2]
T = S / 'tests/tesla_vehicle'
sys.path.insert(0, str(T))
from host_dependencies import cache, mbed  # noqa: E402

HOOK_POINT = 'generation_low.store(static_cast<uint32_t>(identity.generation));'
CLASS_POINT = 'class TeslaBleIdentitySnapshot\n'
HOOK_STATE = '''#include <condition_variable>
#include <mutex>
// TEST-ONLY scratch injection (test_binding_persistence.py). Never in firmware.
inline std::mutex tesla_binding_schedule;
inline std::condition_variable tesla_binding_cv;
inline bool tesla_binding_paused = false, tesla_binding_release = false, tesla_binding_done = false;
inline thread_local bool tesla_binding_publisher = false;
inline void tesla_identity_publication_hook()
{
    if (!tesla_binding_publisher) { return; }
    std::unique_lock<std::mutex> lock(tesla_binding_schedule);
    tesla_binding_paused = true; tesla_binding_cv.notify_all();
    tesla_binding_cv.wait(lock, [] { return tesla_binding_release; });
}
'''
FLOW_MODES = ('binding-republish', 'binding-first', 'binding-change', 'binding-faults')
RED_MODES = ('binding-republish', 'binding-first')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mode', nargs='?', default='normal', choices=('normal', 'sanitize'))
    ap.add_argument('--production-root', type=Path)
    ap.add_argument('--expect-red', action='store_true')
    a = ap.parse_args(sys.argv[1:])
    production = (a.production_root or S) / 'src/modules/tesla_vehicle'
    assert (production / 'tesla_vehicle.cpp').is_file(), production
    sanitize = a.mode == 'sanitize'
    with tempfile.TemporaryDirectory(prefix='binding-persistence-', dir=os.environ['TMPDIR']) as td:
        d = Path(td); root = d / 'software'
        env = dict(os.environ, TESLA_CLEAN_TEST_OUT=str(d / 'out'), TESLA_CLEAN_HARNESS_SANITIZE='1' if sanitize else '0')
        subprocess.run([sys.executable, str(T / 'host/build_littlefs_vfs_harness.py')], check=True, env=env)
        vfs = d / 'out/littlefs-vfs'
        shutil.copytree(production, root / 'src/modules/tesla_vehicle')
        shutil.copytree(T, root / 'tests/tesla_vehicle', ignore=shutil.ignore_patterns('__pycache__', 'node_modules'))
        header = root / 'src/modules/tesla_vehicle/tesla_ble_result.h'
        text = header.read_text()
        assert text.count(HOOK_POINT) == 1 and text.count(CLASS_POINT) == 1, 'publication hook point changed; review harness'
        text = text.replace(CLASS_POINT, HOOK_STATE + CLASS_POINT)
        text = text.replace(HOOK_POINT, HOOK_POINT + '\n        tesla_identity_publication_hook(); // TEST-ONLY pause after odd version store')
        header.write_text(text)
        (d / 'init.cpp').write_text('extern "C" void vfs_backend_setup();\nstatic const int vfs_backend_ready = (vfs_backend_setup(), 0);\n')
        tt = root / 'tests/tesla_vehicle'; mod = root / 'src/modules/tesla_vehicle'
        flags = ['-DTESLA_NIMBLE_HOST_TEST=1', '-DTESLA_BLE_USE_NIMBLE=1', '-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192',
                 '-DCONFIG_BT_NIMBLE_PINNED_TO_CORE=0', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
                 '-fno-exceptions', '-fno-rtti', '-pthread', '-DTESLA_BLE_MODULE_HOST_TEST', '-DTESLA_BLE_MODULE_POSIX_TEST',
                 '-DTESLA_BLE_CLOSE_QUARANTINE_TEST', '-DTESLA_BLE_BINDING_VFS_TEST', '-DTESLA_BLE_MANUAL_VEHICLE_UI=1',
                 '-DBLUETOOTH_CONFIG_PATH="' + str(d / 'config') + '"', '-I' + str(tt / 'host'),
                 '-I' + str(S / 'lib/TeslaBLEClean/include'), '-I' + str(mbed / 'include')]
        if sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-no-pie']
        wrap = '-Wl,' + ','.join('--wrap=' + n for n in ('open', 'close', 'read', 'write', 'fsync', 'fstat', 'stat', 'rename', 'unlink'))
        mbed_build = Path(os.environ['TESLA_CLEAN_MBEDTLS_BUILD']) if os.environ.get('TESLA_CLEAN_MBEDTLS_BUILD') else cache / 'mbedtls'
        libs = [mbed_build / n for n in ('library/libmbedcrypto.a', '3rdparty/everest/libeverest.a', '3rdparty/p256-m/libp256m.a')]
        common = [*(mod / n for n in ('tesla_ble_storage.cpp', 'tesla_ble_transport.cpp', 'tesla_ble_gatt.cpp', 'bluetooth_lifecycle.cpp',
                                     'tesla_nimble_central.cpp')), *(S / 'lib/TeslaBLEClean/src').glob('*.cpp'),
                  tt / 'host/littlefs_vfs_bridge.cpp', d / 'init.cpp', *(vfs / n for n in ('backend.o', 'lfs.o', 'lfs_util.o'))]
        subprocess.run(['g++', *flags, str(tt / 'test_nimble_module.cpp'), *map(str, common), *map(str, libs), wrap, '-o', str(d / 'flow')], check=True)
        storage_flags = [f for f in flags if not f.startswith('-DTESLA_NIMBLE') and 'NIMBLE' not in f]
        subprocess.run(['g++', *storage_flags, '-I' + str(mod), str(tt / 'test_binding_storage_vfs.cpp'), str(mod / 'tesla_ble_storage.cpp'),
                        *map(str, (S / 'lib/TeslaBLEClean/src').glob('*.cpp')), str(tt / 'host/littlefs_vfs_bridge.cpp'), str(d / 'init.cpp'),
                        *(str(vfs / n) for n in ('backend.o', 'lfs.o', 'lfs_util.o')), *map(str, libs), wrap, '-o', str(d / 'storage')], check=True)
        print('BINDING_HARNESS production=%s mode=%s' % ('override' if a.production_root else 'checkout', a.mode), flush=True)
        r = subprocess.run([str(d / 'storage')], timeout=120)
        assert r.returncode == 0, 'storage contract failed'
        failures = []
        for mode in FLOW_MODES:
            p = subprocess.run([str(d / 'flow'), mode], timeout=120, capture_output=True, text=True)
            sys.stdout.write(p.stdout); sys.stderr.write(p.stderr); sys.stdout.flush()
            red = p.returncode == 3 and 'RED_MECHANISM' in p.stderr
            if a.expect_red and mode == 'binding-change':
                # Control half must already fail closed on the original; its later
                # unchanged-republication attempt is the same mechanism RED.
                if not (red and 'BINDING_FAIL_CLOSED attempt=1 cause=changed-identity' in p.stdout and 'attempt=2' in p.stderr):
                    failures.append(f'{mode}: expected fail-closed control then mechanism RED, got rc={p.returncode}')
                else:
                    print(f'EXPECTED_RED {mode} (control attempt 1 failed closed)', flush=True)
            elif a.expect_red and mode in RED_MODES:
                if not red:
                    failures.append(f'{mode}: expected mechanism RED, got rc={p.returncode}')
                else:
                    print(f'EXPECTED_RED {mode}', flush=True)
            elif p.returncode != 0:
                failures.append(f'{mode}: rc={p.returncode}' + (' (RED_MECHANISM)' if red else ''))
        if failures:
            print('BINDING_FAILURES ' + '; '.join(failures), flush=True)
            return 1
        print('BINDING_PERSISTENCE_%s %s' % ('RED_CONFIRMED' if a.expect_red else 'PASS', a.mode), flush=True)
        return 0


if __name__ == '__main__':
    sys.exit(main())
