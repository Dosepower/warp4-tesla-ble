#!/usr/bin/env python3
"""Pinned-source initialization boundaries, NOT a target success test."""
import importlib.util
import os
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "software/tools/bench_bluedroid_memory_check.py"


class BluedroidMemoryTests(unittest.TestCase):
    def load_tool(self):
        self.assertTrue(TOOL.is_file(), "missing pinned-source memory checker")
        spec = importlib.util.spec_from_file_location("bluedroid_memory", TOOL)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_source_boundary_and_observed_memory_barrier(self):
        tool = self.load_tool()
        builder = Path(os.environ.get("TESLA_BLE_MEMORY_TEST_BUILDER", ROOT / "software/.pio/warp4-bench-heap-sdk"))
        self.assertTrue((builder / "build/compile_commands.json").is_file(), "real pinned target build required")
        result = tool.check(builder, sanitize=os.environ.get("TESLA_BLE_SMOKE_SANITIZE") == "1")
        self.assertEqual(result["abi"]["btc_stack"], 8704)
        self.assertEqual(result["abi"]["btu_stack"], 8704)
        self.assertEqual(result["abi"]["hci_stack"], 2560)
        self.assertEqual(result["abi"]["tcb"], 352)
        self.assertEqual(result["abi"]["queue"], 92)
        self.assertEqual(result["abi"]["stack_element"], 1)
        self.assertEqual(result["return_sites"]["esp_fail"], [165, 175, 189, 218, 229])
        self.assertEqual(result["return_sites"]["esp_err_no_mem"], [202])
        self.assertIn("observed_stack_barrier", result["native_scenarios"])
        self.assertIn("idf_default_btc_still_too_large", result["native_scenarios"])
        self.assertEqual(result["strict_internal_lower_bound"], 23692)
        self.assertFalse(result["hardware_success_claimed"])

    def test_extraction_fails_closed_for_missing_or_duplicate_definition(self):
        tool = self.load_tool()
        for source in ("", "int f(void) { return 1; }\nint f(void) { return 2; }"):
            with self.subTest(source=source), self.assertRaises(ValueError):
                tool.function(source, "f")
        self.assertEqual(tool.function("int f(void) { return 1; }", "f"), "int f(void) { return 1; }")

    def test_bench_only_default_malloc_policy_preserves_internal_capability(self):
        tool = self.load_tool()
        result = tool.check_policy(ROOT / "software/.pio/bench-idf",
                                   sanitize=os.environ.get("TESLA_BLE_SMOKE_SANITIZE") == "1")
        self.assertTrue(result["external_preferred_internal_unchanged"])
        bench = (ROOT / "software/lib-builder/defconfig.warp4-ble-bench").read_text()
        self.assertIn("CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0", bench)
        production = (ROOT / "software/lib-builder/defconfig.esp32ethernetbrick").read_text()
        self.assertIn("CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=2048", production)


if __name__ == "__main__":
    unittest.main()
