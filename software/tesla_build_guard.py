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
# Candidate selection is explicit; the package verifier rejects unknown policies
# and binds the exact policy, effective config/header and all archive hashes.
policy = os.environ.get('TESLA_BLE_SDK_POLICY', 'tesla-psram-first-v1')
verified = sdk.verify_package(package, policy=policy)
if verified['disposition'] != 'noninstallable-fallback-review':
    raise ValueError('Unsupported SDK disposition; source-review fallback only')
if policy == sdk.NIMBLE_POLICY:
    sdk.source_policy_inventory(Path(os.environ['TESLA_BLE_SDK_SOURCE_ROOT']) / 'esp-idf-secure', policy)

if policy in (sdk.CLASSIC_RELEASE_POLICY, sdk.NIMBLE_POLICY):
    framework = Path(env.PioPlatform().get_package_dir('framework-arduinoespressif32'))
    selected = Path(os.environ.get('TESLA_BLE_FRAMEWORK_CORE', ''))
    if not selected.is_absolute() or framework.resolve() != selected.resolve():
        raise ValueError('Classic candidate requires the exact separately selected Arduino core')
    sdk.verify_classic_core(framework / 'cores/esp32/esp32-hal-bt.c')
    source = Path(os.environ['TESLA_BLE_SDK_SOURCE_ROOT']) / 'builder/components/arduino/cores/esp32'
    if (framework / 'cores/esp32/esp32-hal-misc.c').read_bytes() != (source / 'esp32-hal-misc.c').read_bytes():
        raise ValueError('Classic candidate boot policy differs from tested SDK sources')
    # PlatformIO does not consume Arduino platform.txt's release interception.
    # Both entrypoints MUST share the core tracking, not a direct SDK bypass.
    env.Append(LINKFLAGS=['-Wl,--wrap=esp_bt_mem_release',
                          '-Wl,--wrap=esp_bt_controller_mem_release'])

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
