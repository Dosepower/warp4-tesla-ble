#!/usr/bin/env python3
"""R8 regressions for immutable Astra R7 findings; software fixtures only."""
import hashlib
from pathlib import Path
import runpy
import struct
import unittest
from unittest.mock import patch
from test_r7_review_packaging import ReviewPackagingTests, report, ROOT
from test_iram_bench_image import elf_fixture, image
import bench_review_export


def repack(header, pieces):
    header = bytearray(header)
    header[1] = len(pieces)
    data, checksum = bytes(header), 0xef
    for address, payload in pieces:
        data += struct.pack('<II', address, len(payload)) + payload
        for value in payload:
            checksum ^= value
    data += b'\0' * ((15 - len(data)) % 16) + bytes([checksum])
    return data + hashlib.sha256(data).digest()


class R8PackagingTests(ReviewPackagingTests):
    def symlink_rejection(self, ancestor, pre_guard=False):
        outside = self.root / 'unrelated'
        outside.mkdir()
        if ancestor == '.pio':
            (self.root / '.pio').symlink_to(outside, target_is_directory=True)
            parent = outside / 'bench-exports'
            parent.mkdir()
        else:
            (self.root / '.pio').mkdir(exist_ok=True)
            if ancestor == 'bench-exports':
                (self.root / '.pio/bench-exports').symlink_to(outside, target_is_directory=True)
                parent = outside
            else:
                parent = self.root / '.pio/bench-exports'
                parent.mkdir()
                (parent / ancestor).symlink_to(outside, target_is_directory=True)
        # Both old success and unchecked targets must be validated BEFORE either
        # is deleted. A bad stage must not cause an earlier success deletion.
        sentinels = []
        for name in ('ordinary', 'ordinary.unchecked'):
            directory = parent / name
            directory.mkdir(exist_ok=True)
            sentinel = directory / 'must-survive.txt'
            sentinel.write_text('unrelated scratch sentinel\n')
            sentinels.append(sentinel)
        with patch('shutil.rmtree', wraps=__import__('shutil').rmtree) as deletion:
            with self.assertRaisesRegex(ValueError, 'symlink|redirect'):
                if pre_guard:
                    outer = self
                    class Env:
                        def subst(self, value):
                            return value.replace('$PROJECT_DIR', str(outer.root)).replace('$BUILD_DIR', str(outer.build))
                        def __getitem__(self, key):
                            return 'warp4_tesla_ble_iram_bench'
                    runpy.run_path(str(ROOT / 'software/bench_guard_hook.py'),
                                   init_globals={'Import': lambda name: None, 'env': Env()})
                else:
                    self.hook()
            deletion.assert_not_called()
        for sentinel in sentinels:
            self.assertEqual(sentinel.read_text(), 'unrelated scratch sentinel\n')

    def test_f1_symlinked_pio_survives_before_any_deletion(self):
        self.symlink_rejection('.pio')

    def test_f1_symlinked_bench_exports_survives_before_any_deletion(self):
        self.symlink_rejection('bench-exports')

    def test_f1_symlinked_stage_does_not_revoke_success_first(self):
        self.symlink_rejection('ordinary.unchecked')

    def test_f1_symlinked_destination_does_not_revoke_stage(self):
        self.symlink_rejection('ordinary')

    def test_f1_pre_guard_symlinked_pio(self):
        self.symlink_rejection('.pio', pre_guard=True)

    def test_f1_pre_guard_symlinked_exports(self):
        self.symlink_rejection('bench-exports', pre_guard=True)

    def test_f1_pre_guard_checks_stage_before_deleting_destination(self):
        self.symlink_rejection('ordinary.unchecked', pre_guard=True)

    def test_f1_pre_guard_checks_destination_before_deleting_stage(self):
        self.symlink_rejection('ordinary', pre_guard=True)

    def test_pre_guard_runs_in_scons_without_file_global(self):
        outer = self
        class Env:
            def subst(self, value):
                return value.replace('$PROJECT_DIR', str(outer.root)).replace('$BUILD_DIR', str(outer.build))
            def __getitem__(self, key):
                return 'warp4_tesla_ble_iram_bench'
        dest = self.root / '.pio/bench-exports/ordinary'
        dest.mkdir(parents=True)
        (dest / 'stale.txt').write_text('old success')
        source = ROOT / 'software/bench_guard_hook.py'
        exec(compile(source.read_text(), str(source), 'exec'), {'Import': lambda name: None, 'env': Env()})
        self.assertFalse(dest.exists())

    def assert_image_rejected(self, data, reason):
        (self.build / 'firmware.bin').write_bytes(data)
        self.merge()
        with self.assertRaisesRegex(ValueError, reason):
            self.validate()
        dest = self.root / '.pio/bench-exports/ordinary'
        with self.assertRaisesRegex(ValueError, reason):
            bench_review_export.export_review(self.build, self.sdk, self.builder, self.ota, dest)
        self.assertFalse(dest.exists())
        self.assertFalse(dest.with_name('ordinary.unchecked').exists())

    def test_f2_extension_of_backed_segment_rejected_by_validate_and_export(self):
        data = (self.build / 'firmware.bin').read_bytes()
        pieces = [(a, p) for a, _, p in report.esp_image(data, 'application')]
        pieces[-1] = (pieces[-1][0], pieces[-1][1] + b'\0' * 4096)
        self.assert_image_rejected(repack(data[:24], pieces), 'unbacked|ELF|padding')

    def section_fixture(self, parts):
        # Retain the actual ELF parser, with independently sized allocated
        # sections, so an interior hole cannot be hidden in an ELF section.
        path = self.root / 'extent.elf'
        path.write_bytes(elf_fixture(b'ABCDEFGH', symbols=[('fixture', 0x3ffb0000, 4)]))
        elf = report.Elf(path)
        template = list(elf.allocated()[0])
        sections = []
        for address, offset, length in parts:
            section = template.copy()
            section[3], section[4], section[5] = address, template[4] + offset, length
            sections.append(tuple(section))
        elf.sections = sections
        return elf

    def test_f2_leading_unbacked_extent_rejected(self):
        elf = self.section_fixture([(0x3ffb0000, 0, 8)])
        with self.assertRaisesRegex(ValueError, 'unbacked|padding'):
            report.match_elf(image(b'\0' * 4 + b'ABCDEFGH', 0x3ffafffc), elf, 'bootloader')

    def test_f2_interior_unbacked_extent_rejected(self):
        elf = self.section_fixture([(0x3ffb0000, 0, 4), (0x3ffb0008, 4, 4)])
        with self.assertRaisesRegex(ValueError, 'unbacked|padding'):
            report.match_elf(image(b'ABCD' + b'\0' * 4 + b'EFGH'), elf, 'bootloader')

    def test_f2_trailing_unbacked_extent_rejected(self):
        elf = self.section_fixture([(0x3ffb0000, 0, 8)])
        with self.assertRaisesRegex(ValueError, 'unbacked|padding'):
            report.match_elf(image(b'ABCDEFGH' + b'\0' * 4), elf, 'bootloader')

    def test_f2_only_exact_section_word_padding_is_allowed(self):
        for size in (1, 2, 3):
            elf = self.section_fixture([(0x3ffb0000, 0, size)])
            padded = b'ABCDEFGH'[:size] + b'\0' * (-size % 4)
            report.match_elf(image(padded), elf, 'bootloader')
            for bad in (padded + b'\0' * 4, padded[:-1] + b'X'):
                with self.subTest(size=size, bad=bad), self.assertRaises(ValueError):
                    report.match_elf(image(bad), elf, 'bootloader')

    def test_f2_word_padding_between_adjacent_padded_sections_control(self):
        elf = self.section_fixture([(0x3ffb0000, 0, 3), (0x3ffb0004, 4, 4)])
        report.match_elf(image(b'ABC\0EFGH'), elf, 'bootloader')

    def test_f3_invalid_mode_validate_and_export_rejected(self):
        data = bytearray((self.build / 'firmware.bin').read_bytes())
        data[2] = 0xff
        data[-32:] = hashlib.sha256(data[:-32]).digest()
        self.assert_image_rejected(data, 'flash mode')

    def test_f3_invalid_frequency_validate_and_export_rejected(self):
        data = bytearray((self.build / 'firmware.bin').read_bytes())
        data[3] = (data[3] & 0xf0) | 0xe
        data[-32:] = hashlib.sha256(data[:-32]).digest()
        self.assert_image_rejected(data, 'flash frequency')

    def test_f3_bootloader_invalid_mode_and_frequency_rejected(self):
        original = (self.build / 'bootloader.bin').read_bytes()
        for index, value in ((2, 0xff), (3, 0x4e)):
            data = bytearray(original)
            data[index] = value
            data[-32:] = hashlib.sha256(data[:-32]).digest()
            for path in (self.build / 'bootloader.bin', self.sdk / 'esp32/bin/bootloader_qio_80m.bin',
                         self.builder / 'build/bootloader/bootloader.bin'):
                path.write_bytes(data)
            self.merge()
            with self.subTest(index=index), self.assertRaisesRegex(ValueError, 'flash mode|flash frequency'):
                self.validate()

    def test_f3_supported_but_unpinned_header_values_rejected(self):
        original = (self.build / 'firmware.bin').read_bytes()
        for index, value in ((2, 0), (2, 1), (2, 3), (3, 0x40), (3, 0x41), (3, 0x42)):
            data = bytearray(original)
            data[index] = value
            data[-32:] = hashlib.sha256(data[:-32]).digest()
            with self.subTest(index=index, value=value):
                self.assert_image_rejected(data, 'flash mode|flash frequency')

    def test_f3_dio_header_with_qio_runtime_config_control(self):
        self.assertEqual((self.build / 'firmware.bin').read_bytes()[2:4], b'\x02\x4f')
        self.assertEqual((self.build / 'bootloader.bin').read_bytes()[2:4], b'\x02\x4f')
        self.validate()

    def test_f3_pinned_config_cannot_change_to_dio_or_40m(self):
        path = self.sdk / 'esp32/sdkconfig'
        original = path.read_text()
        for old, new in (('CONFIG_ESPTOOLPY_FLASHMODE_QIO=y', 'CONFIG_ESPTOOLPY_FLASHMODE_QIO=n'),
                         ('CONFIG_ESPTOOLPY_FLASHMODE="dio"', 'CONFIG_ESPTOOLPY_FLASHMODE="qio"'),
                         ('CONFIG_ESPTOOLPY_FLASHFREQ="80m"', 'CONFIG_ESPTOOLPY_FLASHFREQ="40m"')):
            self.assertIn(old, original)
            path.write_text(original.replace(old, new))
            with self.subTest(old=old), self.assertRaisesRegex(ValueError, 'flash'):
                report.check_config(path, 'SDK')
        path.write_text(original)


def load_tests(loader, tests, pattern):
    return unittest.TestSuite(R8PackagingTests(name) for name in sorted(R8PackagingTests.__dict__)
                              if name.startswith('test_'))


if __name__ == '__main__':
    unittest.main()
