"""Synthetic verifier fixtures, not SDK qualification or firmware."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

S = Path(__file__).resolve().parents[2]

class SDKTests(unittest.TestCase):
    def api(self):
        path = S / "installer_sdk_verify.py"
        self.assertTrue(path.exists(), "separate external installer SDK verifier is missing")
        spec = importlib.util.spec_from_file_location("installer_sdk_verify", path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        return mod

    def fixture(self, m, root):
        sdk = root / "sdk"
        sdk.mkdir()
        (sdk / "libbt.a").write_bytes(b"synthetic inventory fixture, NOT an SDK")
        materials = {}
        for name in m.MATERIALS:
            d = root / name
            d.mkdir()
            (d / "fixture").write_text(name)
            materials[name] = d
        manifest = {"schema": "warp4-installer-sdk-v1", "kind": "source-derived-warp4-secure-ble",
                    "exact_upstream_package": False, "baseline_source_gaps": m.GAPS,
                    "previous_baseline_source_gaps": m.PREVIOUS_GAPS,
                    "pins": m.PINS, "recipe_inputs": m.recipe_inputs(S),
                    "materials": {k: m.inventory(v) for k, v in materials.items()},
                    "outputs": m.inventory(sdk), "config_deltas": [],
                    "commands": ["synthetic fixture only"], "secure_boot_sig_block_size": 1216}
        mp = root / "manifest.json"
        mp.write_text(json.dumps(manifest))
        lock = {"schema": "warp4-installer-sdk-lock-v1", "manifest_sha256": m.sha(mp),
                "sdk_inventory_sha256": m.digest(manifest["outputs"]), "pins": m.PINS,
                "recipe_inputs": m.recipe_inputs(S)}
        return sdk, materials, manifest, mp, lock

    def test_external_pin_and_exact_inventory(self):
        m = self.api()
        with tempfile.TemporaryDirectory() as td:
            sdk, materials, manifest, mp, lock = self.fixture(m, Path(td))
            result = m.verify(sdk, mp, lock, materials, S)
            self.assertEqual(result["sdk_inventory_sha256"], lock["sdk_inventory_sha256"])
            self.assertNotIn("installable", result)
            (sdk / "nested").mkdir()
            (sdk / "nested/tesla-sdk.json").write_text("{}")
            with self.assertRaisesRegex(ValueError, "inventory"):
                m.verify(sdk, mp, lock, materials, S)

    def test_missing_anchor_rejects_even_self_claimed_installable(self):
        m = self.api()
        with tempfile.TemporaryDirectory() as td:
            sdk, materials, manifest, mp, lock = self.fixture(m, Path(td))
            lock["manifest_sha256"] = None
            with self.assertRaisesRegex(ValueError, "EXTERNAL_SDK_MANIFEST_PIN_MISSING"):
                m.verify(sdk, mp, lock, materials, S)
            manifest["installable"] = True
            mp.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError): m.verify(sdk, mp, lock, materials, S)

    def test_mutations_and_materials_are_bound(self):
        m = self.api()
        with tempfile.TemporaryDirectory() as td:
            sdk, materials, original, mp, lock = self.fixture(m, Path(td))
            for key, val in [("exact_upstream_package", 0), ("exact_upstream_package", True),
                             ("baseline_source_gaps", []), ("schema", 1),
                             ("pins", {"builder": ["url", "d5a56a1"]}),
                             ("secure_boot_sig_block_size", 4096)]:
                manifest = copy.deepcopy(original); manifest[key] = val
                mp.write_text(json.dumps(manifest))
                changed_lock = lock | {"manifest_sha256": m.sha(mp)}
                with self.subTest(key=key, val=val), self.assertRaises(ValueError):
                    m.verify(sdk, mp, changed_lock, materials, S)
            mp.write_text(json.dumps(original))
            (materials["sources"] / "untracked").write_text("unexpected")
            with self.assertRaisesRegex(ValueError, "materials"):
                m.verify(sdk, mp, lock, materials, S)

    def test_paths_duplicates_and_links_rejected(self):
        m = self.api()
        with self.assertRaisesRegex(ValueError, "duplicate"):
            m.loads("{\"a\":1,\"a\":2}")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "a").write_text("fixture")
            (root / "link").symlink_to(root / "a")
            with self.assertRaisesRegex(ValueError, "link"):
                m.inventory(root)
            (root / "link").unlink()
            os.link(root / "a", root / "hard")
            with self.assertRaisesRegex(ValueError, "link"):
                m.inventory(root)

    def test_inventory_cannot_escape_through_parent_link(self):
        m=self.api()
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); real=root/"real"; real.mkdir(); (real/"nested").mkdir(); (real/"nested/file").write_text("fixture")
            (root/"alias").symlink_to(real,target_is_directory=True)
            with self.assertRaisesRegex(ValueError,"link"):
                m.inventory(root/"alias/nested")
    def test_checked_in_lock_is_honestly_blocked(self):
        m = self.api()
        lock = m.loads((S / "installer_sdk.lock.json").read_text())
        self.assertIsNone(lock["manifest_sha256"])
        with self.assertRaisesRegex(ValueError, "EXTERNAL_SDK_MANIFEST_PIN_MISSING"):
            m.verify(S / ".pio/tesla-sdk/builder/out/tools/esp32-arduino-libs", S / "missing", lock, {}, S)

if __name__ == "__main__": unittest.main()
