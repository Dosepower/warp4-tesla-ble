"""Source-only build gate. Does not replace upstream layout/security settings."""
import importlib.util
import os
import subprocess
import sys
from pathlib import Path
from SCons.Script import COMMAND_LINE_TARGETS

Import('env')
S = Path(env.subst('$PROJECT_DIR'))
spec = importlib.util.spec_from_file_location('tesla_sdk', S / 'lib-builder/tesla_sdk.py')
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)
sdk.validate_targets(COMMAND_LINE_TARGETS)

package = Path(os.environ.get('TESLA_BLE_FRAMEWORK_LIBS', ''))
if not package.is_absolute():
    raise ValueError('TESLA_BLE_FRAMEWORK_LIBS must identify the rebuilt, manifest-bound secure SDK')
verified = sdk.verify_package(package)
if verified['disposition'] != 'noninstallable-fallback-review':
    raise ValueError('Unsupported SDK disposition; source-review fallback only')

# A verified SDK alone does not prove LittleFS close/error semantics. Rerun
# native acceptance against this build's sources; never reuse old attestations.
subprocess.run([sys.executable, str(S/'tools/tesla_vfs_acceptance.py')], check=True)
env.Append(CPPDEFINES=[('WARP_BLUETOOTH_LITTLEFS_BINDING', '0x57415234')])
flags = ' '.join(env.GetProjectOption('build_flags', []))
for token in ('BENCH', 'CANONICAL', 'IRAM_DIAGNOSTICS', '--wrap=', 'RADIO_SPIKE'):
    if token in flags:
        raise ValueError('Nonproduction feature in WARP4 target: ' + token)

def source_review(env, node):
    name = Path(str(node)).name
    if name.startswith('tesla_ble_bench_') or name in (
            'tesla_ble_radio_spike.cpp', 'tesla_ble_iram_diagnostics.cpp',
            'bluetooth_master_bench.cpp', 'r13_psram_policy.cpp'):
        return None
    return node

env.AddBuildMiddleware(source_review)
# PlatformIO resolves custom dependencies immediately, before its default
# program -> firmware rename. Pin a distinct, explicitly non-installable ELF.
env.Replace(PROGNAME='tesla-source-review-NONINSTALLABLE')
env.AddCustomTarget('tesla-review', '$BUILD_DIR/${PROGNAME}.elf', [],
                    title='Non-installable WARP4 Tesla ELF',
                    description='Compile/link only; bootloader/partition prerequisites, no app image, merge, upload, signing or provisioning.')
