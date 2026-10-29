import importlib.util
import json
from pathlib import Path
import unittest
from platformio.project.config import ProjectConfig
S = Path(__file__).resolve().parents[2]
A, B = "warp4_ble_installer", "warp4_multi_tesla_installer"
class TargetTests(unittest.TestCase):
    def setUp(self): self.c = ProjectConfig(str(S / "platformio.ini"))
    def test_environments_and_shared_sdk(self):
        for n in (A, B): self.assertTrue(self.c.has_section("env:" + n), "offline installer target missing: " + n)
        a, b, stock = [dict(self.c.items("env:" + n)) for n in (A, B, "warp4")]
        for k in ("platform", "board", "board_build.partitions", "platform_packages", "custom_product_id", "custom_options"):
            self.assertEqual(a[k], b[k], k)
        self.assertEqual(a["custom_product_id"], "warp4")
        self.assertEqual(a["board"], "esp32_ethernet_brick_secure")
        self.assertEqual(a["board_build.partitions"], "default_16MB_secure.csv")
        self.assertIn(".pio/installer-sdk/sha256/", str(a["platform_packages"]))
        for e in (a, b):
            self.assertEqual(e["extra_scripts"], ["pre:warp4_installer_guard.py", "pre:pio_hooks.py", "post:warp4_ota_only_hook.py"])
            self.assertEqual(e["custom_signature_preset"], "")
            self.assertEqual(e["custom_signature_verify_preset"], "warp:v1")
            self.assertEqual(e["custom_provisioning"], "false")
        for k in ("custom_backend_modules", "custom_frontend_modules"):
            self.assertEqual(a[k], stock[k])
            self.assertEqual(b[k].splitlines(), stock[k].splitlines() + ["Tesla Vehicle"])
        self.assertNotIn("Tesla", str(a["lib_deps"]))
        self.assertIn("TeslaBLEClean", b["lib_deps"])
        self.assertIn("-DTESLA_BLE_MANUAL_VEHICLE_UI=1", b["build_flags"])
        self.assertNotIn("Tesla Vehicle | Tesla", b["custom_frontend_components"])
        self.assertIn("Tesla Vehicle | Bluetooth Page", b["custom_frontend_components"])
    def test_existing_defaults_snapshot(self):
        baseline = json.loads((S / "tests/installer/existing_defaults.json").read_text())
        for n, expected in baseline.items(): self.assertEqual(dict(self.c.items(n)), expected, n)
    def test_only_offline_default_and_exact_config(self):
        path = S / "installer_policy.py"
        self.assertTrue(path.exists(), "fail-closed offline installer policy missing")
        spec = importlib.util.spec_from_file_location("installer_policy", path)
        m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
        lock = json.loads((S / "installer_env.lock.json").read_text())
        for name in (A, B):
            opts = dict(self.c.items("env:" + name)); m.validate(name, [], opts, lock)
            for t in ("upload", "uploadfs", "erase", "erase_upload", "monitor", "serial", "buildfs", "sign", "provision", "firmware.bin", "tesla-review", "size", "compiledb", "clean"):
                for ts in ([t], ["build", t]):
                    with self.subTest(ts=ts), self.assertRaisesRegex(ValueError, "OFFLINE_DEFAULT_ONLY"): m.validate(name, ts, opts, lock)
            for k, v in (("extra_scripts", ["post:merge_firmware_hook.py"]), ("custom_secure_boot", "forbidden"), ("custom_provisioning", "true"), ("custom_options", opts["custom_options"] + "\nfactory_data_enable_write_api = 1"), ("build_flags", opts["build_flags"] + ["-DDEBUG_FS_ENABLE"]), ("custom_frontend_modules_injections", "Tesla Vehicle")):
                with self.subTest(k=k), self.assertRaises(ValueError): m.validate(name, [], opts | {k:v}, lock)
if __name__ == "__main__": unittest.main()
