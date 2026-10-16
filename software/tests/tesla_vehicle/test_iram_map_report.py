#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[3]
MODULE_PATH = REPO / "software" / "tools" / "iram_map_report.py"


def load_module():
    spec = importlib.util.spec_from_file_location("iram_map_report", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


MAP_TEXT = """
Memory Configuration

Name             Origin             Length             Attributes
iram0_0_seg      0x40080000         0x00020000         xr

.iram0.vectors  0x40080000      0x404
                0x40080000                        _iram_start = ABSOLUTE (.)
.iram0.text     0x40080404    0x1889f
                0x40080404                        _iram_text_start = ABSOLUTE (.)
.iram0.data     0x40098ca4      0x1e0
                0x40098e84                        _iram_end = ABSOLUTE (.)
"""


class IramMapReportTests(unittest.TestCase):
    def test_reports_static_iram_layout_and_tail(self):
        module = load_module()
        report = module.parse_map(MAP_TEXT)

        self.assertEqual(report["segment_origin"], 0x40080000)
        self.assertEqual(report["segment_size"], 0x20000)
        self.assertEqual(report["vectors_size"], 0x404)
        self.assertEqual(report["text_size"], 0x1889F)
        self.assertEqual(report["data_size"], 0x1E0)
        self.assertEqual(report["static_end"], 0x40098E84)
        self.assertEqual(report["static_used"], 102020)
        self.assertEqual(report["static_tail"], 29052)

    def test_rejects_incomplete_map_instead_of_guessing(self):
        module = load_module()
        with self.assertRaisesRegex(ValueError, "iram0.data"):
            module.parse_map(MAP_TEXT.replace(".iram0.data", ".missing.data"))

    def test_comparison_reports_signed_delta(self):
        module = load_module()
        smaller = MAP_TEXT.replace("0x1889f", "0x18000").replace("0x40098ca4", "0x40098404").replace("0x40098e84", "0x400985e4")
        comparison = module.compare_reports(module.parse_map(MAP_TEXT), module.parse_map(smaller))

        self.assertEqual(comparison["static_used_delta"], -2208)
        self.assertEqual(comparison["static_tail_delta"], 2208)

    def test_rejects_incompatible_memory_layouts(self):
        module = load_module()
        baseline = module.parse_map(MAP_TEXT)
        for key in ("segment_origin", "segment_size"):
            candidate = dict(baseline)
            candidate[key] += 4
            with self.assertRaisesRegex(ValueError, "different IRAM memory layouts"):
                module.compare_reports(baseline, candidate)

    def test_rejects_duplicate_required_records(self):
        module = load_module()
        required_lines = (
            "iram0_0_seg      0x40080000         0x00020000         xr",
            ".iram0.vectors  0x40080000      0x404",
            ".iram0.text     0x40080404    0x1889f",
            ".iram0.data     0x40098ca4      0x1e0",
            "                0x40098e84                        _iram_end = ABSOLUTE (.)",
        )
        for line in required_lines:
            with self.subTest(line=line.strip()):
                with self.assertRaisesRegex(ValueError, "ambiguous"):
                    module.parse_map(MAP_TEXT + "\n" + line + "\n")

    def test_cli_rejects_duplicate_required_records(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "duplicate.map"
            path.write_text(MAP_TEXT + "\n.iram0.text     0x40080404    0x1889f\n")
            result = subprocess.run([sys.executable, str(MODULE_PATH), str(path)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")
            self.assertIn("ambiguous iram0.text", result.stderr)
            self.assertNotIn("Traceback", result.stderr)

    def test_checks_end_boundaries(self):
        module = load_module()
        self.assertEqual(module.parse_map(MAP_TEXT.replace("0x40098e84", "0x400a0000"))["static_tail"], 0)
        for address in ("0x4007ffff", "0x400a0001"):
            with self.assertRaisesRegex(ValueError, "outside iram0_0_seg"):
                module.parse_map(MAP_TEXT.replace("0x40098e84", address))

    def test_rejects_sections_outside_static_span_and_overlaps(self):
        module = load_module()
        for malformed in (
            MAP_TEXT.replace("0x1889f", "0x30000"),
            MAP_TEXT.replace("0x40098ca4", "0x40098e80"),
            MAP_TEXT.replace("0x40080404    0x1889f", "0x40080000    0x1889f"),
        ):
            with self.assertRaisesRegex(ValueError, "section"):
                module.parse_map(malformed)

    def test_cli_labels_static_only_and_compares(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.map"
            path.write_text(MAP_TEXT)
            result = subprocess.run([sys.executable, str(MODULE_PATH), str(path), "--baseline", str(path)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report["measurement"], "static_linker_layout_not_runtime_heap")
            self.assertEqual(report["delta"]["vectors_size_delta"], 0)
            self.assertEqual(report["report"]["static_tail"], 29052)

    def test_cli_reports_invalid_input_without_traceback_or_partial_json(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "missing.map"
            for content in (None, "not a map"):
                if content is not None:
                    path.write_text(content)
                result = subprocess.run([sys.executable, str(MODULE_PATH), str(path)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, "")
                self.assertIn("error:", result.stderr)
                self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
