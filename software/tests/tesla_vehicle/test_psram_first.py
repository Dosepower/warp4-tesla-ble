"""Explicit Tesla malloc policy; stock security/layout remains the default."""
import importlib.util
import os
import sys
from pathlib import Path
import unittest
S = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(S))
spec = importlib.util.spec_from_file_location('sdk_psram', S/'lib-builder/tesla_sdk.py')
sdk = importlib.util.module_from_spec(spec); spec.loader.exec_module(sdk)
from installer_sdk_verify import verify_config_maps, verify_config_header, REQUIRED_BLE
POLICY = 'tesla-psram-first-v1'
KEY = 'CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL'
PACKAGE = Path(os.environ.get('TESLA_BLE_FRAMEWORK_LIBS', S/'.pio/tesla-sdk/builder/out/tools/esp32-arduino-libs'))
PACKAGE_POLICY = os.environ.get('TESLA_BLE_SDK_POLICY', POLICY)
class PSRAMFirst(unittest.TestCase):
    def test_threshold_zero_requires_explicit_candidate_policy(self):
        base = sdk.parse_config((S/'lib-builder/defconfig.esp32secure').read_text())
        cfg = base | sdk.parse_config((S/'lib-builder/defconfig.warp4-tesla-ble').read_text()) | {KEY:'0'}
        sdk.validate_config(cfg, base, policy=POLICY)
        with self.assertRaises(ValueError): sdk.validate_config(cfg, base)
        with self.assertRaises(ValueError): sdk.validate_config(cfg | {KEY:'2048'}, base, policy=POLICY)

    def test_installer_one_key_exception_is_explicit_and_complete(self):
        base = REQUIRED_BLE | {'CONFIG_SECURE_BOOT':'y', 'CONFIG_SPIRAM':'y', KEY:'2048',
            'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE':'y', 'CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK':'n',
            'CONFIG_PARTITION_TABLE_OFFSET':'0xe000', 'CONFIG_ESPTOOLPY_FLASHSIZE':'"16MB"'}
        cfg = base | {KEY:'0'}
        delta = [{'key':KEY, 'before':'2048', 'after':'0', 'reason':'Explicit user authorized ordinary malloc PSRAM-first'}]
        verify_config_maps(base, cfg, delta, policy=POLICY)
        with self.assertRaises(ValueError): verify_config_maps(base, cfg, delta)
        with self.assertRaises(ValueError): verify_config_maps(base, base, [], policy=POLICY)
        for key, val in [('CONFIG_SECURE_BOOT','n'), ('CONFIG_SPIRAM_SPEED_80M','y'),
                         ('CONFIG_PARTITION_TABLE_OFFSET','0x8000'), ('CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST','y')]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                verify_config_maps(base, cfg | {key:val}, delta, policy=POLICY)
        verify_config_header({KEY:'0'}, '#define '+KEY+' 0\n')
        with self.assertRaises(ValueError): verify_config_header({KEY:'0'}, '#define '+KEY+' 2048\n')

    def test_recipe_rejects_every_other_protected_change(self):
        base = sdk.parse_config((S/'lib-builder/defconfig.esp32secure').read_text())
        cfg = base | sdk.parse_config((S/'lib-builder/defconfig.warp4-tesla-ble').read_text()) | {KEY:'0'}
        for key in base:
            if key != KEY and key.startswith(('CONFIG_SECURE_', 'CONFIG_SPIRAM', 'CONFIG_BOOTLOADER_APP_', 'CONFIG_PARTITION_TABLE', 'CONFIG_ESPTOOLPY_FLASH', 'CONFIG_ESP32_REV_MIN', 'CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM')):
                val = 'n' if base[key] == 'y' else 'y'
                with self.subTest(key=key), self.assertRaises(ValueError):
                    sdk.validate_config(cfg | {key:val}, base, policy=POLICY)
        with self.assertRaises(ValueError):
            sdk.validate_config(cfg | {'CONFIG_SPIRAM_FOREIGN':'y'}, base, policy=POLICY)

    def test_recipe_profile_selection_is_not_implicit_stock(self):
        self.assertEqual(sdk.profile_config('stock')[KEY], '2048')
        self.assertEqual(sdk.profile_config(POLICY)[KEY], '0')
        with self.assertRaises(ValueError): sdk.profile_config('typo')
        self.assertIn('lib-builder/tesla_sdk.py', sdk.inputs())
        self.assertIn('lib-builder/defconfig.warp4-tesla-psram-first', sdk.inputs())

    def test_candidate_package_rejects_stock_policy_and_tampered_effective_header(self):
        import shutil, tempfile, json
        package = PACKAGE
        sdk.verify_package(package, policy=PACKAGE_POLICY)
        with self.assertRaises(ValueError): sdk.verify_package(package)
        with tempfile.TemporaryDirectory() as td:
            target = Path(td)/'sdk'; shutil.copytree(package, target)
            header = target/'esp32/qio_qspi/include/sdkconfig.h'
            header.write_text(header.read_text().replace('#define '+KEY+' 0', '#define '+KEY+' 2048'))
            mp = target/'tesla-sdk.json'; manifest = json.loads(mp.read_text())
            manifest['files']['esp32/qio_qspi/include/sdkconfig.h'] = sdk.sha(header)
            mp.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, 'sdkconfig/header mismatch'):
                sdk.verify_package(target, policy=PACKAGE_POLICY)


    def test_installer_cannot_approve_external_task_stacks(self):
        base = REQUIRED_BLE | {'CONFIG_SECURE_BOOT':'y', 'CONFIG_SPIRAM':'y', KEY:'2048',
            'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE':'y', 'CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK':'n',
            'CONFIG_PARTITION_TABLE_OFFSET':'0xe000', 'CONFIG_ESPTOOLPY_FLASHSIZE':'"16MB"',
            'CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM':'n'}
        cfg = base | {KEY:'0', 'CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM':'y'}
        delta = [{'key':k, 'before':base.get(k), 'after':cfg.get(k), 'reason':'must not authorize extra changes'}
                 for k in sorted(base.keys() | cfg.keys()) if base.get(k)!=cfg.get(k)]
        with self.assertRaises(ValueError): verify_config_maps(base, cfg, delta, policy=POLICY)


    def test_full_candidate_refuses_all_other_protected_deltas_even_explained(self):
        base = sdk.parse_config((S/'packages/esp32-arduino-libs#tf-esp-secure-20260922_4f0ab20592113e12c1792ba670c48aad04ceb288/esp32/sdkconfig').read_text())
        cfg = sdk.parse_config((PACKAGE/'esp32/sdkconfig').read_text())
        protected = ('CONFIG_SECURE_', 'CONFIG_SPIRAM', 'CONFIG_BOOTLOADER_APP_',
                     'CONFIG_PARTITION_TABLE', 'CONFIG_ESPTOOLPY_FLASH', 'CONFIG_ESP32_REV_MIN',
                     'CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM')
        for key in (base.keys() | cfg.keys() | {'CONFIG_SPIRAM_FOREIGN', 'CONFIG_SECURE_FOREIGN'}):
            if key == KEY or not key.startswith(protected): continue
            bad = cfg | {key:'n' if cfg.get(key) == 'y' else 'y'}
            deltas = [{'key':k, 'before':base.get(k), 'after':bad.get(k), 'reason':'explained but unauthorized'}
                      for k in sorted(base.keys() | bad.keys()) if base.get(k)!=bad.get(k)]
            with self.subTest(key=key), self.assertRaises(ValueError):
                verify_config_maps(base, bad, deltas, policy=POLICY)

if __name__ == '__main__': unittest.main()
