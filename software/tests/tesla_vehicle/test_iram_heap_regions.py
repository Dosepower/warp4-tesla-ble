#!/usr/bin/env python3
"""Heap startup regressions using ELF reservations and the real SDK patch.

Set TESLA_BLE_HEAP_TEST_ELF to test an actual linked application as well.
No hardware access or inferred runtime success.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import unittest

from test_iram_bench_image import elf_fixture, report, ROOT

PATCH = ROOT / "software/patches/lib-builder/esp-idf/0009-heap-Add-MALLOC_CAP_IRAM-reserve-regions-for-manual-.patch"
LOW_DRAM = "MANUALLY_RESERVED_6432B_AT_0x3FFAE6E0_DRAM"
OTHER_DRAM = "MANUALLY_RESERVED_15072B_AT_0x3FFE0440_DRAM"
IRAM = "MANUALLY_RESERVED_8192B_AT_0x4009E000_IRAM"


class HeapRegionTests(unittest.TestCase):
    def make_elf(self, regions):
        import tempfile
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        path = Path(temp.name) / "heap.elf"
        address = 0x3f400000
        symbols = [("soc_reserved_memory_region_start", address, 0),
                   ("soc_reserved_memory_region_end", address + 8 * len(regions), 0)]
        symbols += [("reserved_region_" + name, address + 8 * i, 8)
                    for i, (name, _, _) in enumerate(regions)]
        path.write_bytes(elf_fixture(b"".join(struct.pack("<II", start, end)
                         for _, start, end in regions), address, symbols, ".rodata"))
        return report.Elf(path)

    def test_real_linked_heap_table(self):
        path = os.environ.get("TESLA_BLE_HEAP_TEST_ELF")
        if not path:
            self.skipTest("set TESLA_BLE_HEAP_TEST_ELF for generated-ELF integration")
        result = report.heap_reservations(report.Elf(path))
        self.assertGreater(result["count"], 0)

    def test_bt_enabled_patch_does_not_claim_controller_data(self):
        self.assertNotIn(LOW_DRAM, self.patch_macros(bluetooth=True))

    def test_production_bt_disabled_manual_arenas_unchanged(self):
        macros = self.patch_macros(bluetooth=False)
        for name in (LOW_DRAM, OTHER_DRAM, IRAM):
            self.assertIn(name, macros)

    def test_bt_enabled_other_manual_arenas_remain(self):
        macros = self.patch_macros(bluetooth=True)
        for name in (OTHER_DRAM, IRAM):
            self.assertIn(name, macros)

    @staticmethod
    def patch_macros(bluetooth):
        # Preprocess the actual added header directives, not parallel test defines.
        header = PATCH.read_text().split("diff --git a/components/heap/port/esp32/memory_layout.c")[0]
        added = "\n".join(line[1:] for line in header.splitlines()
                          if line.startswith("+") and not line.startswith("+++"))
        command = ["gcc", "-E", "-dM", "-x", "c", "-"]
        if bluetooth:
            command.append("-DCONFIG_BT_ENABLED=1")
        result = subprocess.run(command, input=added, text=True, capture_output=True, check=True)
        return "\n".join(line for line in result.stdout.splitlines()
                         if "MANUALLY_RESERVED_" in line or "MALLOC_CAP_IRAM " in line)

    def test_unsorted_nonoverlapping_table_is_valid(self):
        result = report.heap_reservations(self.make_elf([
            ("b", 0x40080000, 0x40081000), ("a", 0x3ffae000, 0x3ffae6e0)]))
        self.assertEqual(result["count"], 2)
        self.assertEqual(result["regions"][0]["name"], "reserved_region_a")

    def test_exact_observed_duplicate_start_is_rejected(self):
        elf = self.make_elf([("manual_heap_6k", 0x3ffae6e0, 0x3ffb0000),
                             ("rom_bt_data", 0x3ffae6e0, 0x3ffaff10)])
        with self.assertRaisesRegex(ValueError, "manual_heap_6k.*0x3ffae6e0.*rom_bt_data.*0x3ffae6e0"):
            report.heap_reservations(elf)

    def test_partial_overlap_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "overlap"):
            report.heap_reservations(self.make_elf([("a", 0x40080000, 0x40081000),
                                                    ("b", 0x40080ffc, 0x40082000)]))

    def test_word_expansion_overlap_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "overlap"):
            report.heap_reservations(self.make_elf([("a", 0x40080000, 0x40080005),
                                                    ("b", 0x40080006, 0x40080010)]))

    def test_empty_adjacent_reservation_is_valid(self):
        report.heap_reservations(self.make_elf([("a", 0x40080000, 0x40080000),
                                               ("b", 0x40080004, 0x40080008)]))

    def test_reversed_reservation_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "reversed"):
            report.heap_reservations(self.make_elf([("a", 0x40080004, 0x40080000)]))


if __name__ == "__main__":
    unittest.main()
