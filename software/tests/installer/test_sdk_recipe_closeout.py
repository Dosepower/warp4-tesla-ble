"""SDK 2.13.6 recipe closeout: pins, gaps, CA, recipe-input closure, old-SDK rejection.

Static recipe checks only; NOT an SDK build or qualification.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

S = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(S))
import installer_sdk_verify as isv  # noqa: E402

spec = importlib.util.spec_from_file_location('tesla_sdk', S / 'lib-builder/tesla_sdk.py')
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)

OLD_NIMBLE_SDK = Path('/home/hermes/warp4-build-support/nimble-v2/builder/out/tools/esp32-arduino-libs')
OLD_STOCK_SDK = Path('/home/hermes/warp4-tesla-installer/software/.pio/tesla-sdk/builder/out/tools/esp32-arduino-libs')


def copy_recipe(root):
    for rel in ('lib-builder', 'patches/lib-builder', 'patches/lib-builder-pre'):
        shutil.copytree(S / rel, root / rel, ignore=shutil.ignore_patterns('__pycache__'))
    return root


class CloseoutTests(unittest.TestCase):
    def test_pins_identical_in_both_recipes(self):
        self.assertEqual(sdk.PINS_as_json(), isv.PINS)
        self.assertEqual(sdk.unresolved_revisions(), isv.GAPS)
        self.assertEqual(sdk.previous_unresolved_revisions(), isv.PREVIOUS_GAPS)
        self.assertEqual(sdk.PINS['esp-idf-secure'][1], '3ea97e7591b6a83f5d6f8e0cc7f1b16f27b2b855')
        self.assertEqual(sdk.IDF_RELEASE_TAG[2], '880a203ae603d08a94d06aa01e0d6761958e5e9e')
        self.assertEqual(sdk.IDF_VERSION, (5, 5, 6))
        # New and old gaps are disjoint and both kept: neither replaced nor erased.
        self.assertFalse({g['revision'] for g in isv.GAPS} & {g['revision'] for g in isv.PREVIOUS_GAPS})

    def test_recipe_inputs_equal_build_inputs_closure(self):
        a, b = isv.recipe_inputs(S), sdk.inputs()
        self.assertEqual(a, b)
        for rel in ('lib-builder/defconfig.warp4-tesla-single-link', 'lib-builder/defconfig.warp4-tesla-nimble',
                    'lib-builder/nimble-init-v2/sdk.patch', 'lib-builder/nimble-init-v2/source-policy.json',
                    'lib-builder/cacert-2026-10-20.pem',
                    'patches/lib-builder/managed_components/espressif__mdns/0004-mdns-Place-static-buffer-to-assemble-packets-on-stac.rawpatch'):
            self.assertIn(rel, a)

    def test_singleton_nimble_policy_and_patch_binding(self):
        for mutation in ('extra', 'missing', 'patch'):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as td:
                root = copy_recipe(Path(td))
                p = root / 'lib-builder/nimble-init-v2'
                if mutation == 'extra':
                    (p / 'extra.patch').write_text('x')
                elif mutation == 'missing':
                    (p / 'sdk.patch').unlink()
                else:
                    with open(p / 'sdk.patch', 'a') as f:
                        f.write('\n')
                with self.assertRaises(ValueError):
                    isv.recipe_inputs(root)
                with self.assertRaises(ValueError):
                    sdk.nimble_policy_paths(root)

    def test_ca_bundle_pin(self):
        data = (S / sdk.CA_BUNDLE).read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), sdk.CA_BUNDLE_SHA256)
        self.assertTrue(data.lstrip().startswith(b'##') or b'-----BEGIN CERTIFICATE-----' in data)
        self.assertNotIn(b'PRIVATE KEY', data)
        with tempfile.TemporaryDirectory() as td:
            idf = Path(td)
            target = idf / sdk.IDF_CA_BUNDLE
            target.parent.mkdir(parents=True)
            target.write_bytes(b'unpinned bundle')
            with self.assertRaises(ValueError):
                sdk.prepare_ca_bundle(idf)
            target.write_bytes(data)
            sdk.prepare_ca_bundle(idf)

    def test_managed_component_pins(self):
        self.assertEqual([v for _, v in sdk.MANAGED_VERSIONS], ['1.14.0', '1.0.22~1', '1.23.1'])
        self.assertEqual(sorted(sdk.MANAGED_COMPONENT_HASHES),
                         ['espressif__libsodium', 'espressif__mdns', 'joltwallet__littlefs'])
        self.assertTrue(all(isv.hexpin(v) for v in sdk.MANAGED_COMPONENT_HASHES.values()))
        manifest = 'dependencies:\n  espressif/mdns:\n    version: "^1.11.2"\n  espressif/libsodium:\n    version: "^1.0.22"\n  joltwallet/littlefs:\n    version: "^1.22.1"\n'
        frozen = sdk.freeze_managed_versions(manifest)
        self.assertIn('version: "1.14.0"', frozen)
        with self.assertRaises(ValueError):
            sdk.freeze_managed_versions(manifest + '  x:\n    version: "^1.11.2"\n')
        with tempfile.TemporaryDirectory() as td:
            with self.assertRaisesRegex(ValueError, 'unavailable'):
                sdk.verify_managed_components(Path(td), dict(os.environ))

    def test_source_policy_secure_boot_encryption_nimble_psram_unchanged(self):
        base = sdk.parse_config((S / 'lib-builder/defconfig.esp32secure').read_text())
        cfg = sdk.profile_config(sdk.NIMBLE_POLICY)
        for key, value in {'CONFIG_SECURE_BOOT': 'y', 'CONFIG_SPIRAM': 'y',
                           'CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL': '0', 'CONFIG_PARTITION_TABLE_OFFSET': '0xe000',
                           'CONFIG_BT_NIMBLE_ENABLED': 'y', 'CONFIG_BT_ENABLED': 'y'}.items():
            self.assertEqual(cfg.get(key, 'n'), value, key)
        self.assertEqual(base['CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL'], '2048')
        for key in [k for k in base if k.startswith(('CONFIG_SECURE_', 'CONFIG_FLASH_ENCRYPTION'))]:
            self.assertEqual(cfg.get(key, 'n'), base[key], key)
        self.assertEqual(sdk.nimble_source_policy()['sdk_patch_sha256'],
                         '631e515751a2dd927cb32e4f1e6a0519ad7dae4b7934b9f1e41cc9f1bb0d6a80')
        # Upstream 195877a3d removed the explicit disables; the IDF default (y) then
        # resolves exactly as in the official tf-esp-secure-20261022 sdkconfig.
        self.assertNotEqual(base.get('CONFIG_MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA'), 'n')
        self.assertNotEqual(base.get('CONFIG_MBEDTLS_SERVER_SSL_SESSION_TICKETS'), 'n')

    def test_old_binary_sdks_are_rejected(self):
        found = 0
        for package, policy in ((OLD_NIMBLE_SDK, sdk.NIMBLE_POLICY), (OLD_STOCK_SDK, sdk.TESLA_POLICY)):
            if not (package / 'tesla-sdk.json').is_file():
                continue
            found += 1
            old = json.loads((package / 'tesla-sdk.json').read_text())
            self.assertNotEqual(old['pins'], sdk.PINS_as_json())
            with self.subTest(package=str(package)), self.assertRaises(ValueError):
                sdk.verify_package(package, policy=policy)
            # Relabelling the old manifest in a scratch copy is still rejected.
            with tempfile.TemporaryDirectory() as td:
                fake = Path(td)
                (fake / 'tesla-sdk.json').write_text(json.dumps(old | {
                    'unresolved_upstream_revisions': sdk.unresolved_revisions(),
                    'previous_unresolved_upstream_revisions': sdk.previous_unresolved_revisions(),
                    'reference_package': sdk.OFFICIAL_PACKAGE, 'sdk': sdk.SDK_LABEL}))
                with self.assertRaisesRegex(ValueError, 'stale'):
                    sdk.verify_package(fake, policy=policy)
        if not found:
            self.skipTest('no retained old SDK package available read-only')


if __name__ == '__main__':
    unittest.main()
