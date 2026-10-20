"""Dedicated candidate guard: exact archives + real compiled SDK config."""
from pathlib import Path
import hashlib, json, os, sys
Import('env')
project=Path(env.subst('$PROJECT_DIR'))
sys.path.insert(0,str(project/'tools'))
from r13_psram_sdk import verify_header
from r13_vfs_binding import CORE_HASHES, BINDING, BENCH_BINDING
allowed={'warp4_tesla_ble_psram':False,'warp4_tesla_ble_psram_diag':True,'warp4_tesla_ble_psram_manual_bench':True,'warp4_tesla_ble_psram_manual_canonical':True,'warp4_tesla_ble_vehicle_canonical':True}
if env['PIOENV'] not in allowed:raise ValueError('unexpected PSRAM candidate profile')
bench=allowed[env['PIOENV']]
vehicle=env['PIOENV']=='warp4_tesla_ble_vehicle_canonical'
canonical=vehicle or env['PIOENV']=='warp4_tesla_ble_psram_manual_canonical'
manual=env['PIOENV'] in ('warp4_tesla_ble_psram_manual_canonical','warp4_tesla_ble_psram_manual_bench')
flags=list(env.GetProjectOption('build_flags',[]))
if any('CONFIG_SPIRAM_' in str(f) or 'WARP_BLUETOOTH_' in str(f) for f in flags+list(env.get('CPPDEFINES',[]))):raise ValueError('application cannot override SDK/config binding')
required={'-DTESLA_BLE_BENCH_ONLY=1','-DTESLA_BLE_IRAM_DIAGNOSTICS=1'}
required.update({'-DTESLA_BLE_MANUAL_WEB_BENCH=1','-DTESLA_BLE_R13_MASTER_BENCH=1'} if manual else {'-DTESLA_BLE_PSRAM_POLICY_DIAGNOSTIC=1'})
if bench and not vehicle and not required.issubset(set(flags)):raise ValueError('generic diagnostic macros missing')
if vehicle:
 if '-DTESLA_BLE_MANUAL_VEHICLE_UI=1' not in flags:raise ValueError('manual vehicle policy missing')
 if any(any(token in str(f) for token in ('TESLA_BLE_MANUAL_WEB_BENCH','TESLA_BLE_R13_MASTER_BENCH','TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC','TESLA_BLE_IRAM_DIAGNOSTICS','TESLA_BLE_BENCH_ONLY')) for f in flags):raise ValueError('vehicle backend cannot use bench or diagnostic suppression')
if manual and any('TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC=' in str(f) for f in flags):raise ValueError('manual browser bench must not include UART policy')
if canonical and '-DTESLA_BLE_CANONICAL_LAYOUT_BENCH=1' not in flags:raise ValueError('canonical storage guard missing')
if canonical and '-Wl,--wrap=esp_vfs_littlefs_register' not in flags:raise ValueError('canonical no-format/no-growth VFS guard missing')
if not canonical and any('TESLA_BLE_CANONICAL_LAYOUT_BENCH' in str(f) for f in flags):raise ValueError('canonical storage forbidden in other profiles')
if canonical and (env.GetProjectOption('board_build.partitions','')!='default_16MB_coredump.csv' or env.GetProjectOption('board_upload.arduino.partitions_bin','')!='0x8000' or env.GetProjectOption('board_upload.arduino.boot_app0','')!='0xe000'):raise ValueError('canonical table/OTA layout mismatch')
if not bench and any('TESLA_BLE_BENCH' in str(f) or '--wrap=' in str(f) or 'DIAGNOSTIC' in str(f) for f in flags):raise ValueError('production candidate must remain ordinary path')
if bench and ((not manual and any('TESLA_BLE_R13_MASTER_BENCH=1' in str(f) for f in flags)) or env.GetProjectOption('custom_provisioning','')!='false' or env.GetProjectOption('custom_secure_boot','')!='' or env.GetProjectOption('custom_signature_preset','')!=''):raise ValueError('unsafe generic diagnostic profile')
platform=env.PioPlatform();core=Path(platform.get_package_dir('framework-arduinoespressif32'));sdk=Path(platform.get_package_dir('framework-arduinoespressif32-libs'))
manifest=Path(os.environ['R13_PSRAM_SDK_MANIFEST'])
expected=json.loads(manifest.read_text())
for rel,h in expected.items():
 p=sdk/rel
 if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=h:raise ValueError('SDK archive/header drift: '+rel)
for rel,h in CORE_HASHES.items():
 if hashlib.sha256((core/rel).read_bytes()).hexdigest()!=h:raise ValueError('unreviewed FS core '+rel)
header=(sdk/'esp32/qio_qspi/include/sdkconfig.h').read_text();verify_header(header)
if ('#define CONFIG_SECURE_BOOT 1' in header)==bench:raise ValueError('production/generic security separation failed')
for line in ('#define CONFIG_BT_BTC_TASK_STACK_SIZE 8192','#define CONFIG_BT_BTU_TASK_STACK_SIZE 8192','#define CONFIG_PARTITION_TABLE_OFFSET '+('0x8000' if canonical else '0xe000')):
 if line not in header:raise ValueError('compiled stack/layout contract mismatch')
# These tokens attest only the reviewed FS adapter, not hardware acceptance.
env.Append(CPPDEFINES=[('WARP_BLUETOOTH_BENCH_VFS_BINDING' if bench and not vehicle else 'WARP_BLUETOOTH_LITTLEFS_BINDING',hex(BENCH_BINDING if bench and not vehicle else BINDING))])
if bench:
    # Legacy bench translation units have global constructors/storage adapters.
    # The dedicated diagnostic must not link them merely because the module
    # directory contains them; ordinary Tesla/API registration remains linked.
    def no_legacy_bench(env, node):
        name=Path(str(node)).name
        if name.startswith('tesla_ble_bench_') or name in ('bluetooth_master_bench.cpp','tesla_ble_radio_spike.cpp'):
            return None
        return node
    env.AddBuildMiddleware(no_legacy_bench)
print('PSRAM candidate: exact source-bound SDK config/archive/FS guard passed; hardware acceptance remains separate')
