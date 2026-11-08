"""Remote-only production owner: no Tesla implementation/library linked."""
from pathlib import Path
import os
import subprocess

S = Path(__file__).resolve().parents[2]


def test_remote_only_boot_save_preserves_excluded_data_and_journal(tmp_path):
    mbed = Path(os.environ['REMOTE_TEST_MBEDTLS_SOURCE'])
    build = Path(os.environ['REMOTE_TEST_MBEDTLS_BUILD'])
    source = tmp_path / 'owner.cpp'
    source.write_text(r'''
#include "module_platform.h"
#include "modules/warp_remote/warp_remote.cpp"
#include "nimble_fixture.h"
struct TeslaBleModuleTest {
 static void run() {
  uint8_t peer[32]={9}, key[32]={7};
  RemoteBindingStore seed{{nullptr,RemoteBindingBackend::read,RemoteBindingBackend::write}};
  assert(seed.load() && seed.prepare(peer,key,"fixture") && seed.commit_prepared(peer,key) && seed.activate(peer));
  RemoteOperationJournal journal{{nullptr,RemoteOperationBackend::read,RemoteOperationBackend::write}};
  assert(journal.load());assert(journal.begin(peer,1,3)==RemoteOperationJournal::Fresh);
  assert(journal.issue(peer,1)); // surviving UNKNOWN must never be reissued
  const auto bindings=RemoteBindingBackend::disk, operations=RemoteOperationBackend::disk;
  const auto binding_writes=RemoteBindingBackend::writes, operation_writes=RemoteOperationBackend::writes;
  LittleFS.files["/config/tesla_retained_test"]={1,2,3,4};
  const auto foreign=LittleFS.files;
  WarpRemote m;m.pre_setup();m.setup();m.register_urls();m.loop();
  assert(m.initialized && tasks.empty() && host_restore_calls==0);
  assert(!api.states.count("tesla_vehicle/state") && !api.commands.count("tesla_vehicle/pair"));
  assert(api.states.count("bluetooth/config") && api.states.count("bluetooth/remote_config"));
  assert(test_scans==0 && test_connects==0 && reboot_calls==0 && host_charge_calls==0);
  assert(RemoteBindingBackend::disk==bindings && RemoteOperationBackend::disk==operations);
  assert(RemoteBindingBackend::writes==binding_writes && RemoteOperationBackend::writes==operation_writes);
  String error;m.bluetooth_update.get("enable")->updateBool(true);
  api.commands.at("bluetooth/config_update")(0,error);assert(error.empty());
  assert(tasks.empty() && test_scans==0 && test_connects==0 && reboot_calls==0);
  {
   HostNimbleDriver driver(m.bluetooth,nullptr);
   m.remote_update.get("enable")->updateBool(true);
   api.commands.at("bluetooth/remote_config_update")(0,error);assert(error.empty());
   assert(test_scans==0 && test_connects==0);
   m.remote_update.get("enable")->updateBool(false);
   api.commands.at("bluetooth/remote_config_update")(0,error);assert(error.empty());
   m.loop();assert(test_scans==0 && test_connects==0 && host_charge_calls==0);
  }
  assert(RemoteBindingBackend::disk==bindings && RemoteOperationBackend::disk==operations);
  assert(RemoteBindingBackend::writes==binding_writes && RemoteOperationBackend::writes==operation_writes);
  assert(LittleFS.files==foreign && reboot_calls==0);
 }
};
int main(){TeslaBleModuleTest::run();}
''')
    exe = tmp_path / 'owner'
    flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
             '-Wno-unused-variable', '-pthread', '-fsanitize=address,undefined', '-no-pie',
             '-DTESLA_BLE_MODULE_HOST_TEST', '-DTESLA_NIMBLE_HOST_TEST=1', '-DTESLA_BLE_USE_NIMBLE=1',
             '-DWARP_FEATURE_TESLA=0', '-DWARP_FEATURE_REMOTE=1', '-DTESLA_BLE_MANUAL_VEHICLE_UI=1',
             '-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192', '-DCONFIG_BT_NIMBLE_PINNED_TO_CORE=0',
             '-DBLUETOOTH_CONFIG_PATH="' + str(tmp_path / 'bluetooth') + '"',
             '-DWARP_REMOTE_CONFIG_PATH="' + str(tmp_path / 'remote') + '"',
             '-I' + str(S / 'tests/tesla_vehicle/host'), '-I' + str(S / 'src'),
             '-I' + str(S / 'lib/TeslaBLEClean/include'),  # declarations required by legacy FS shim only
             '-I' + str(mbed / 'include')]
    libs = [build / name for name in ('library/libmbedcrypto.a', '3rdparty/everest/libeverest.a', '3rdparty/p256-m/libp256m.a')]
    subprocess.run(['g++', *flags, str(source), *map(str, libs), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
    symbols = subprocess.check_output(['nm', '-C', str(exe)], text=True)
    assert 'tesla_clean::Vehicle::' not in symbols
    assert 'TeslaVehicle::' not in symbols
    assert 'TeslaBleStorageAdapter::' not in symbols
    assert 'BluetoothCore::remote_load()' in symbols
