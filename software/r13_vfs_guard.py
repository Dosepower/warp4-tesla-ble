# Read-only PlatformIO binding guard; no outputs, package edits, or cleanup.
from pathlib import Path
import sys
Import('env')
sys.path.insert(0, str(Path(env.subst('$PROJECT_DIR')) / 'tools'))
from r13_vfs_binding import BINDING, BENCH_BINDING, verified, verified_bench

# This hook is the only issuer. A manually supplied/stale token must not
# survive a failed artifact check (including through project build_flags).
tokens = ('WARP_BLUETOOTH_LITTLEFS_BINDING', 'WARP_BLUETOOTH_BENCH_VFS_BINDING')
definitions = env.get('CPPDEFINES', []) if hasattr(env, 'get') else []
flags = env.GetProjectOption('build_flags', []) if hasattr(env, 'GetProjectOption') else []
if any(token in str(value) for value in list(definitions) + list(flags) for token in tokens):
    raise ValueError('R13 binding tokens may only be issued by the artifact guard')

platform = env.PioPlatform()
core = platform.get_package_dir('framework-arduinoespressif32')
sdk = platform.get_package_dir('framework-arduinoespressif32-libs')
if verified(core, sdk):
    env.Append(CPPDEFINES=[('WARP_BLUETOOTH_LITTLEFS_BINDING', hex(BINDING))])
elif (hasattr(env, 'GetProjectOption') and env['PIOENV'] == 'warp4_tesla_ble_iram_bench' and
      all(env.GetProjectOption(k, '') == v for k, v in {
          'custom_bench_only': 'true', 'custom_provisioning': 'false',
          'custom_secure_boot': '', 'custom_signature_preset': ''}.items()) and
      all(flag in env.GetProjectOption('build_flags', []) for flag in (
          '-DTESLA_BLE_R13_MASTER_BENCH=1', '-DTESLA_BLE_BENCH_ONLY=1',
          '-DTESLA_BLE_IRAM_DIAGNOSTICS=1')) and verified_bench(core, sdk)):
    env.Append(CPPDEFINES=[('WARP_BLUETOOTH_BENCH_VFS_BINDING', hex(BENCH_BINDING))])
else:
    print('R13: unverified LittleFS/VFS artifacts; Bluetooth master persistence unavailable')
