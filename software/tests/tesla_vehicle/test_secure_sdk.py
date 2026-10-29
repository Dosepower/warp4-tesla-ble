"""Fail-closed real-WARP4 build preparation, no device or signing operations."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

S = Path(__file__).resolve().parents[2]
PACKAGE = Path(os.environ.get('TESLA_BLE_FRAMEWORK_LIBS', S/'.pio/tesla-sdk/builder/out/tools/esp32-arduino-libs'))
POLICY = os.environ.get('TESLA_BLE_SDK_POLICY', 'tesla-psram-first-v1')
spec = importlib.util.spec_from_file_location('tesla_sdk', S / 'lib-builder/tesla_sdk.py')
sdk = importlib.util.module_from_spec(spec)

class SecureSDK(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec.loader.exec_module(sdk)

    def test_manifest_disposition_cannot_be_elevated(self):
        import copy, json
        from unittest.mock import patch
        package = PACKAGE
        manifest_text = (package/'tesla-sdk.json').read_text()
        real_loads = json.loads
        original = real_loads(manifest_text)
        # Exercise the full verifier without copying the SDK or altering it.
        mutations = []
        for key in ('installable', 'exact_upstream_package'):
            for value in (True, None, 0, 'false'):
                mutations.append((key, value))
            mutations.append((key, 'DELETE'))
        blockers = [{'component': k, 'revision': v} for k,v in sdk.UPSTREAM_PACKAGE_REVISIONS.items()]
        for value in ([], None, {}, blockers[:-1], blockers+blockers[:1],
                      blockers+[{'component':'extra','revision':'unknown'}],
                      [dict(x, revision='wrong') for x in blockers], list(reversed(blockers))):
            mutations.append(('unresolved_upstream_revisions', value))
        mutations.append(('unresolved_upstream_revisions', 'DELETE'))
        # Order is canonical too: no ambiguous blocker declarations.
        for key,value in mutations:
            candidate = copy.deepcopy(original)
            if value == 'DELETE': candidate.pop(key, None)
            else: candidate[key] = value
            with self.subTest(key=key,value=value), patch.object(sdk.json, 'loads', side_effect=lambda text: candidate if text == manifest_text else real_loads(text)):
                with self.assertRaises(ValueError): sdk.verify_package(package, policy=POLICY)
        elevated = copy.deepcopy(original)
        elevated.update(installable=True, exact_upstream_package=True, unresolved_upstream_revisions=[])
        with patch.object(sdk.json, 'loads', side_effect=lambda text: elevated if text == manifest_text else real_loads(text)), self.assertRaises(ValueError):
            sdk.verify_package(package, policy=POLICY)
        spoofed = original | {'disposition': 'installable-exact'}
        with patch.object(sdk.json, 'loads', side_effect=lambda text: spoofed if text == manifest_text else real_loads(text)):
            self.assertEqual(sdk.verify_package(package, policy=POLICY)['disposition'], 'noninstallable-fallback-review')
        verified = sdk.verify_package(package, policy=POLICY)
        self.assertEqual(verified['disposition'], 'noninstallable-fallback-review')
        hook = (S/'tesla_build_guard.py').read_text()
        self.assertIn("verified['disposition']", hook)

    def test_build_guard_consumes_verified_disposition(self):
        import runpy, types, sys
        from unittest.mock import patch, Mock
        script=types.ModuleType('SCons.Script');setattr(script,'COMMAND_LINE_TARGETS',['tesla-review'])
        fake_env=Mock();fake_env.subst.return_value=str(S);fake_env.GetProjectOption.return_value=[]
        fake_sdk=types.SimpleNamespace(CLASSIC_RELEASE_POLICY=sdk.CLASSIC_RELEASE_POLICY,NIMBLE_POLICY=sdk.NIMBLE_POLICY,validate_targets=sdk.validate_targets, verify_package=Mock())
        fake_spec=types.SimpleNamespace(loader=types.SimpleNamespace(exec_module=lambda module: None))
        for disposition in ('noninstallable-fallback-review','installable-exact'):
            fake_sdk.verify_package.return_value={'disposition':disposition}
            with patch.dict(sys.modules,{'SCons':types.ModuleType('SCons'),'SCons.Script':script}), \
                 patch('importlib.util.spec_from_file_location',return_value=fake_spec), \
                 patch('importlib.util.module_from_spec',return_value=fake_sdk), \
                 patch('subprocess.run') as run, \
                 patch.dict('os.environ',{'TESLA_BLE_FRAMEWORK_LIBS':str(S/'.pio/tesla-sdk/builder/out/tools/esp32-arduino-libs'), 'TESLA_BLE_SDK_POLICY':'tesla-psram-first-v1'}):
                def execute():
                    runpy.run_path(str(S/'tesla_build_guard.py'),init_globals={'Import':lambda _:None,'env':fake_env})
                if disposition=='noninstallable-fallback-review':
                    execute();self.assertEqual(run.call_count,1)
                    self.assertIn('tesla_vfs_acceptance.py',str(run.call_args))
                else:
                    with self.assertRaises(ValueError):execute()
                    run.assert_not_called()

    def test_overlay_retains_all_secure_settings(self):
        base = sdk.parse_config((S / 'lib-builder/defconfig.esp32secure').read_text())
        overlay = sdk.parse_config((S / 'lib-builder/defconfig.warp4-tesla-ble').read_text())
        combined = base | overlay
        self.assertEqual(combined['CONFIG_PARTITION_TABLE_OFFSET'], '0xe000')
        for key in base:
            if key.startswith(('CONFIG_SECURE_', 'CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK', 'CONFIG_SPIRAM_')):
                self.assertEqual(combined[key], base[key], key)
        self.assertEqual(combined['CONFIG_BT_ENABLED'], 'y')
        self.assertEqual(combined['CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST'], 'n')

    def test_wrong_layout_or_security_is_rejected(self):
        base = sdk.parse_config((S / 'lib-builder/defconfig.esp32secure').read_text())
        cfg = base | sdk.parse_config((S / 'lib-builder/defconfig.warp4-tesla-ble').read_text())
        sdk.validate_config(cfg, base)
        for key, value in [('CONFIG_PARTITION_TABLE_OFFSET', '0x8000'),
                           ('CONFIG_SECURE_BOOT', 'n'),
                           ('CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL', '0'),
                           ('CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST', 'y'),
                           ('CONFIG_BT_ENABLED', 'n')]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                sdk.validate_config(cfg | {key: value}, base)

    def test_missing_or_tampered_package_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            with self.assertRaises((ValueError, FileNotFoundError)):
                sdk.verify_package(root)
            # A hand-written manifest must not establish SDK provenance.
            (root / 'tesla-sdk.json').write_text('{}')
            with self.assertRaises(ValueError):
                sdk.verify_package(root)

    def test_candidate_sdk_is_explicitly_not_exact_upstream_package(self):
        self.assertEqual(sdk.UPSTREAM_PACKAGE_REVISIONS,
                         {'builder': 'b63070e', 'esp-idf': 'a3afb692749', 'arduino': 'b280564c6'})
        # Superseded tf-esp-secure-20260922 gaps stay recorded, never erased.
        self.assertEqual(sdk.PREVIOUS_UPSTREAM_PACKAGE_REVISIONS,
                         {'builder': 'd5a56a1', 'esp-idf': 'd2a6bf5f2b4', 'arduino': 'fa903c6b9'})
        self.assertEqual(sdk.OFFICIAL_PACKAGE['commit'], 'd80bc8e05ab1012010f5acd87ca974206a64f4ee')
        self.assertIn('source-review candidate', (S/'lib-builder/tesla_sdk.py').read_text())
        ini=(S/'warp4_tesla_ble.ini').read_text()
        self.assertIn('#'+sdk.PINS['builder/components/arduino'][1], ini)

    def test_warp4_storage_acceptance_is_not_old_checkpoint_attestation(self):
        hook = (S/'tesla_build_guard.py').read_text()
        self.assertIn('tesla_vfs_acceptance.py', hook)
        self.assertIn('WARP_BLUETOOTH_LITTLEFS_BINDING', hook)
        self.assertIn('0x57415234', hook)
        binding = (S/'src/modules/tesla_vehicle/bluetooth_littlefs_binding.h').read_text()
        self.assertIn('0x57415234', binding)

    def test_only_exact_platformio_backup_allowed(self):
        expected = {'esp32/pioarduino-build.py': 'synthetic-hash', 'esp32/lib/libbt.a': 'archive'}
        sdk.validate_files(expected, expected | {'esp32/pioarduino-build.py.esp32': 'synthetic-hash'})
        for changed in ({'esp32/pioarduino-build.py.esp32': 'different'},
                        {'esp32/unknown': 'synthetic-hash'},
                        {'esp32/lib/libbt.a': 'changed'}):
            with self.assertRaises(ValueError): sdk.validate_files(expected, expected | changed)

    def test_source_only_hook_rejects_device_and_image_targets(self):
        for target in ('upload', 'uploadfs', 'erase', 'monitor', 'buildfs', 'firmware.bin', 'sign'):
            with self.subTest(target=target), self.assertRaises(ValueError):
                sdk.validate_targets([target])
        sdk.validate_targets(['tesla-review'])
        with self.assertRaises(ValueError):
            sdk.validate_targets([])

if __name__ == '__main__':
    unittest.main()
