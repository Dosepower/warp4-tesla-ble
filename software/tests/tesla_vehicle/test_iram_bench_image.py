#!/usr/bin/env python3
"""Bench packaging regression tests. Fixtures are not hardware measurements."""
from pathlib import Path
import hashlib
import importlib.util
import json
import struct
import subprocess
import sys
import tempfile
import unittest
import ast
import os
import runpy
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "software/tools/bench_image_report.py"
spec = importlib.util.spec_from_file_location("bench_image_report", TOOL)
assert spec is not None and spec.loader is not None
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


def image(payload=b"fixture", address=0x3ffb0000):
    header = bytearray(24)
    header[0:4] = bytes([0xe9, 1, 2, 0x4f])  # ESP32, DIO ROM header; QIO enabled later
    header[23] = 1
    data = bytes(header) + struct.pack("<II", address, len(payload)) + payload
    check = 0xef
    for value in payload:
        check ^= value
    data += b"\0" * ((15 - len(data)) % 16) + bytes([check])
    return data + hashlib.sha256(data).digest()


def elf_fixture(payload, address=0x3ffb0000, symbols=(), section=".text"):
    """Small genuine ELF32-Xtensa test file; no runtime claim."""
    names = b"\0" + section.encode() + b"\0.symtab\0.strtab\0.shstrtab\0"
    strings = b"\0"
    symtab = b"\0" * 16
    for name, value, size in symbols:
        symtab += struct.pack("<IIIBBH", len(strings), value, size, 0x12, 0, 1)
        strings += name.encode() + b"\0"
    chunks = [payload, symtab, strings, names]
    offset = 52
    offsets = []
    for chunk in chunks:
        offsets.append(offset)
        offset += len(chunk)
    ident = b"\x7fELF\x01\x01\x01" + b"\0" * 9
    header = ident + struct.pack("<HHIIIIIHHHHHH", 2, 94, 1, 0, 0, offset, 0, 52, 0, 0, 40, 5, 4)
    sections = [(0,0,0,0,0,0,0,0,0,0),
                (1,1,6,address,offsets[0],len(payload),0,0,4,0),
                (names.index(b".symtab"),2,0,0,offsets[1],len(symtab),3,1,4,16),
                (names.index(b".strtab"),3,0,0,offsets[2],len(strings),0,0,1,0),
                (names.index(b".shstrtab"),3,0,0,offsets[3],len(names),0,0,1,0)]
    return header + b"".join(chunks) + b"".join(struct.pack("<10I", *s) for s in sections)


def boot_payload(offset=0xe000):
    # Real Xtensa entry/l32r/l32r/call8 shape from pinned IDF.
    payload = bytearray(0x1a98)
    struct.pack_into("<I", payload, 0x200, offset)
    payload[0x1a8c:0x1a98] = bytes.fromhex("366100b1dbf9a1dbf9257bff")
    return bytes(payload)


def partition_table(entries=None):
    entries = report.WARP4_PARTITIONS if entries is None else entries
    data = b"".join(struct.pack("<HBBII16sI", 0x50aa, t, s, o, n, label.encode(), flags)
                    for label, t, s, o, n, flags in entries)
    data += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(data).digest()
    return data.ljust(3072, b"\xff")


class BenchImageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "firmware"
        self.sdk = self.root / "sdk"
        self.builder = self.root / "builder"
        self.build.mkdir()
        (self.sdk / "esp32/bin").mkdir(parents=True)
        (self.sdk / "esp32/qio_qspi/include").mkdir(parents=True)
        (self.builder / "build/bootloader/config").mkdir(parents=True)
        (self.builder / "build/bootloader").mkdir(exist_ok=True)
        self.config = '# CONFIG_SECURE_BOOT is not set\n# CONFIG_SECURE_FLASH_ENC_ENABLED is not set\nCONFIG_PARTITION_TABLE_OFFSET=0xe000\nCONFIG_ESPTOOLPY_FLASHSIZE="16MB"\nCONFIG_BT_ENABLED=y\nCONFIG_BT_BLUEDROID_ENABLED=y\nCONFIG_BT_GATTC_ENABLE=y\nCONFIG_BT_BTC_TASK_STACK_SIZE=8192\nCONFIG_BT_BTU_TASK_STACK_SIZE=8192\n'
        self.flash_config = 'CONFIG_ESPTOOLPY_FLASHMODE_QIO=y\nCONFIG_ESPTOOLPY_FLASHMODE="dio"\nCONFIG_ESPTOOLPY_FLASHFREQ_80M=y\nCONFIG_ESPTOOLPY_FLASHFREQ="80m"\n'
        self.config += self.flash_config + "CONFIG_BT_GATTS_ENABLE=y\nCONFIG_BT_BLE_SMP_ENABLE=y\n# CONFIG_BLE_MESH is not set\n"
        (self.sdk / "esp32/sdkconfig").write_text(self.config)
        header = '#define CONFIG_PARTITION_TABLE_OFFSET 0xe000\n#define CONFIG_ESPTOOLPY_FLASHSIZE "16MB"\n' + self.flash_config.replace('=y', '=1').replace('CONFIG_', '#define CONFIG_').replace('=', ' ')
        (self.sdk / "esp32/qio_qspi/include/sdkconfig.h").write_text(header)
        (self.builder / "build/bootloader/config/sdkconfig.h").write_text(header)
        boot = image(boot_payload(), 0x40078000)
        for p in (self.build / "bootloader.bin", self.sdk / "esp32/bin/bootloader_qio_80m.bin", self.builder / "build/bootloader/bootloader.bin"):
            p.write_bytes(boot)
        (self.build / "firmware.bin").write_bytes(image(b"application fixture"))
        (self.build / "firmware.elf").write_bytes(elf_fixture(b"application fixture", symbols=[("fixture_entry", 0x3ffb0000, 4)]))
        self.write_boot_elf()
        (self.build / "partitions.bin").write_bytes(partition_table())
        self.ota = self.root / "boot_app0.bin"
        self.ota.write_bytes(b"\xff" * 8192)
        (self.sdk / "esp32/pioarduino-build.py").write_text("# unit builder script\n")
        self.seal_fixture()
        self.merge()

    def seal_fixture(self):
        # Synthetic provenance for isolated unit data, never real build evidence.
        source = self.root / "fixture-source.c"
        source.write_text("unit fixture, not a hardware measurement\n")
        (self.build / "metadata.json").write_text(json.dumps({"signature_preset": "", "secure_boot": "", "split_esptool_ota": True,
            "options": {"factory_data_enable_write_api": 0, "esp32_common_enable_secure_boot_api": 0, "product_id_is_warp4_provisioning": 0}}))
        (self.build / "firmware.map").write_text("synthetic unit map\n")
        (self.builder / "build/bootloader/bootloader.map").write_text("synthetic unit boot map\n")
        for name in ("main", "components", "managed_components", "tools", "patches", "configs", "esp-idf"):
            (self.builder / name).mkdir(exist_ok=True)
        for name in ("CMakeLists.txt", "partitions.csv", "dependencies.lock", "sdkconfig"):
            (self.builder / name).write_text("synthetic SDK source\n")
        compiler = self.root / "toolchain/bin/gcc"
        compiler.parent.mkdir(parents=True,exist_ok=True)
        compiler.write_text("unit fixture compiler input, not executable\n")
        (self.builder / "build/compile_commands.json").write_text(json.dumps([{"command": str(compiler) + " fixture.c"}]))
        (self.builder / "build/CMakeCache.txt").write_text("unit fixture CMake input\n")
        self.write_map(self.builder / "build/bootloader/bootloader.elf", self.builder / "build/bootloader/bootloader.map")
        sources = report.identity.sdk_sources(self.builder)
        inputs = {"schema": 2, "sources": sources}
        (self.sdk / "bench_sdk_provenance.json").unlink(missing_ok=True)
        proof = {"schema": 2, "variant": "ordinary", "builder": str(self.builder), "inputs": inputs,
                 "source_identity": report.identity.digest(inputs),
                 "builder_outputs": report.identity.snapshot([self.builder / "build"]),
                 "export_sha256": {str(Path(p).relative_to(self.sdk)): h for p,h in report.identity.snapshot([self.sdk]).items()}}
        (self.sdk / "bench_sdk_provenance.json").write_text(json.dumps(proof))
        store = self.root / "blobs"
        identity = {"schema": 2, "variant": "ordinary", "environment": "warp4_tesla_ble_iram_bench",
                    "sdk_provenance_sha256": report.identity.sha(self.sdk / "bench_sdk_provenance.json"),
                    "metadata_sha256": report.identity.sha(self.build / "metadata.json"),
                    "files": report.identity.snapshot([source, self.ota], store), "store": str(store),
                    "project_options": {"custom_bench_only": "true", "custom_provisioning": "false", "custom_secure_boot": "", "custom_signature_preset": ""}}
        (self.build / "bench_build_inputs.unchecked.json").write_text(json.dumps(identity))
        self.fixture_identity = identity
        self.write_app()

    def write_app(self, payload=b"application fixture", symbols=(), heap_regions=((0x40080000, 0x40081000),)):
        data = bytes.fromhex("3254cdab") + b"\0" * 268 + payload
        symbols = list(symbols) + [("bench_inputs_" + report.identity.digest(self.fixture_identity), 0, 0)]
        if heap_regions is not None:
            data += b"\0" * (-len(data) % 4)
            table_start = 0x3f400020 + len(data)
            data += b"".join(struct.pack("<II", *row) for row in heap_regions)
            symbols += [("soc_reserved_memory_region_start", table_start, 0),
                        ("soc_reserved_memory_region_end", table_start + len(heap_regions) * 8, 0)]
        elf = elf_fixture(data, 0x3f400020, symbols, section=".flash.appdesc")
        (self.build / "firmware.elf").write_bytes(elf)
        data = data[:144] + hashlib.sha256(elf).digest() + data[176:]
        (self.build / "firmware.bin").write_bytes(image(data, 0x3f400020))
        self.write_map(self.build / "firmware.elf", self.build / "firmware.map")

    @staticmethod
    def write_map(elf, path):
        parsed = report.Elf(elf)
        path.write_text("\n".join(f"{parsed.string(parsed.names,s[0])} 0x{s[3]:08x} 0x{s[5]:x}" for s in parsed.allocated()) + "\n")

    def write_boot_elf(self, offset=0xe000):
        data = elf_fixture(boot_payload(offset), 0x40078000,
                           [("bootloader_utility_load_partition_table", 0x40079a8c, 12),
                            ("bootloader_mmap", 0x40079248, 0)])
        for p in (self.builder / "build/bootloader/bootloader.elf",
                  self.sdk / "esp32/bin/bootloader_qio_80m.elf"):
            p.write_bytes(data)

    def merge(self, target=0x1000, table=0xe000):
        data = bytearray(b"\xff" * (0x642000 - target))
        for offset, p in ((0x1000, self.build / "bootloader.bin"), (table, self.build / "partitions.bin"), (0x10000, self.build / "firmware.bin"), (0x640000, self.ota)):
            data[offset-target:offset-target+p.stat().st_size] = p.read_bytes()
        (self.build / "firmware_esptool.bin").write_bytes(data)

    def validate(self, target=0x1000):
        return report.validate(self.build, self.sdk, self.builder, self.ota, target)

    def test_valid_artifacts(self):
        result = self.validate()
        self.assertEqual(result["partition_table_offset"], 0xe000)
        self.assertEqual(result["flash_target_offset"], 0x1000)
        self.assertEqual(len(result["partitions"]), 6)
        self.assertEqual(result["factorydata"], "not_in_image_requires_erased_bench_flash")

    def test_current_ordinary_mesh_and_security_feature_policy_is_exact(self):
        for option,new in (("CONFIG_BLE_MESH","y"),("CONFIG_BT_BLE_SMP_ENABLE","n"),("CONFIG_BT_GATTS_ENABLE","n")):
            with self.subTest(option=option):
                text=self.config.replace('# CONFIG_BLE_MESH is not set','CONFIG_BLE_MESH=y') if option=='CONFIG_BLE_MESH' else self.config.replace(option+'=y',option+'='+new)
                (self.sdk/'esp32/sdkconfig').write_text(text)
                self.seal_fixture();self.merge()
                with self.assertRaisesRegex(ValueError,'disagrees with actual SDK config '+option):self.validate()

    def test_duplicate_heap_reservation_rejected_before_export(self):
        self.write_app(heap_regions=((0x3ffae6e0, 0x3ffb0000), (0x3ffae6e0, 0x3ffaff10)))
        self.merge()
        with self.assertRaisesRegex(ValueError, "heap reservation.*0x3ffae6e0"):
            self.validate()

    def test_missing_heap_table_rejected_before_export(self):
        self.write_app(heap_regions=None)
        self.merge()
        with self.assertRaisesRegex(ValueError, "heap reservation table"):
            self.validate()

    def test_exact_observed_mismatch(self):
        (self.builder / "build/bootloader/config/sdkconfig.h").write_text('#define CONFIG_PARTITION_TABLE_OFFSET 0x8000\n#define CONFIG_ESPTOOLPY_FLASHSIZE "16MB"\n')
        with self.assertRaisesRegex(ValueError, "bootloader.*0x8000.*0xe000"):
            self.validate()

    def test_application_sdk_offset_mismatch(self):
        (self.sdk / "esp32/sdkconfig").write_text(self.config.replace("0xe000", "0x8000"))
        with self.assertRaisesRegex(ValueError, "SDK.*offset"):
            self.validate()

    def test_wrong_merge_target(self):
        self.merge(target=0)
        with self.assertRaises(ValueError):
            self.validate()
        with self.assertRaisesRegex(ValueError, "target offset"):
            self.validate(target=0)

    def test_missing_required_pieces(self):
        for name in ("bootloader.bin", "partitions.bin", "firmware.bin", "firmware_esptool.bin"):
            with self.subTest(name=name):
                p = self.build / name
                data = p.read_bytes()
                p.unlink()
                with self.assertRaises(ValueError):
                    self.validate()
                p.write_bytes(data)
        self.ota.unlink()
        with self.assertRaises(ValueError):
            self.validate()

    def test_magic_md5_and_layout(self):
        p = self.build / "partitions.bin"
        original = p.read_bytes()
        for data in (b"\xff" * 3072, original[:100], original[:32] + original[:32] + original[64:], partition_table(report.WARP4_PARTITIONS[:-1]), partition_table([(l,t,s,o,n,f | 1) for l,t,s,o,n,f in report.WARP4_PARTITIONS])):
            with self.subTest(data=data[:32].hex()):
                p.write_bytes(data)
                self.merge()
                with self.assertRaises(ValueError):
                    self.validate()

    def test_every_merged_piece_is_verified_byte_for_byte(self):
        for offset in (0x1000, 0xe000, 0x10000, 0x640000):
            self.merge()
            p = self.build / "firmware_esptool.bin"
            data = bytearray(p.read_bytes())
            data[offset - 0x1000] ^= 1
            p.write_bytes(data)
            with self.subTest(offset=hex(offset)), self.assertRaises(ValueError):
                self.validate()

    def test_secure_settings_stale_bootloader_and_bad_image_rejected(self):
        for name in ("CONFIG_SECURE_BOOT", "CONFIG_SECURE_BOOT_V2_ENABLED", "CONFIG_SECURE_FLASH_ENC_ENABLED"):
            (self.sdk / "esp32/sdkconfig").write_text(self.config + name + "=y\n")
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "secure"):
                self.validate()
        (self.sdk / "esp32/sdkconfig").write_text(self.config)
        (self.build / "bootloader.bin").write_bytes(image(b"stale fixture"))
        self.merge()
        with self.assertRaisesRegex(ValueError, "bootloader.*differ"):
            self.validate()

    def test_image_checksum_hash_flash_size_and_truncation(self):
        original = (self.build / "firmware.bin").read_bytes()
        for change in (lambda b: b.__setitem__(3, 0x2f), lambda b: b.__setitem__(32, b[32] ^ 1), lambda b: b.__setitem__(-1, b[-1] ^ 1), lambda b: b.__delitem__(slice(-8,None))):
            data = bytearray(original)
            change(data)
            (self.build / "firmware.bin").write_bytes(data)
            self.merge()
            with self.assertRaises(ValueError):
                self.validate()

    def test_nvs_dependent_sdk_settings_rejected_for_warp4_layout(self):
        for name in ("CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE", "CONFIG_BT_BLE_SMP_BOND_NVS_FLASH", "CONFIG_BT_GATTC_CACHE_NVS_FLASH", "CONFIG_ESP_WIFI_NVS_ENABLED"):
            (self.sdk / "esp32/sdkconfig").write_text(self.config + name + "=y\n")
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "NVS"):
                self.validate()

    def test_client_and_prior_task_stacks_required_in_actual_sdk(self):
        for old, new in (("CONFIG_BT_GATTC_ENABLE=y", "# CONFIG_BT_GATTC_ENABLE is not set"), ("CONFIG_BT_BTC_TASK_STACK_SIZE=8192", "CONFIG_BT_BTC_TASK_STACK_SIZE=3072"), ("CONFIG_BT_BTU_TASK_STACK_SIZE=8192", "CONFIG_BT_BTU_TASK_STACK_SIZE=4352")):
            (self.sdk / "esp32/sdkconfig").write_text(self.config.replace(old, new))
            with self.subTest(old=old), self.assertRaises(ValueError):
                self.validate()

    def test_cli_failure_no_partial_json_or_traceback(self):
        p = subprocess.run([sys.executable, str(TOOL), "--build-dir", str(self.build), "--sdk", str(self.sdk), "--builder", str(self.builder), "--boot-app0", str(self.ota), "--target-offset", "0"], text=True, capture_output=True)
        self.assertEqual(p.returncode, 2)
        self.assertEqual(p.stdout, "")
        self.assertNotIn("Traceback", p.stderr)

    def test_bench_environment_explicit_and_production_unchanged(self):
        ini = (ROOT / "software/warp4_tesla_ble.ini").read_text()
        for name in ("warp4_tesla_ble_iram_bench", "warp4_tesla_ble_iram_bench_minimal"):
            self.assertIn("[env:" + name + "]", ini)
        self.assertIn("TESLA_BLE_BENCH_ONLY", ini)
        self.assertIn("TESLA_BLE_BENCH_FRAMEWORK_LIBS", ini)
        self.assertIn("TESLA_BLE_BENCH_MINIMAL_FRAMEWORK_LIBS", ini)
        # Current raw-build profile deliberately performs no merge/export.
        # It must still enforce exact SDK identity before accepting the build.
        self.assertIn("post:r13_lean_bench.py", ini)
        self.assertNotIn("post:bench_image_hook.py", ini)
        gate = (ROOT / "software/r13_lean_bench.py").read_text()
        self.assertIn("from bench_build_identity import verify_sdk", gate)
        self.assertIn("verify_sdk(sdk, env.GetProjectOption('custom_bench_builder'))", gate)
        self.assertNotIn("bench_image_report", gate)
        self.assertIn("board_build.arduino.custom_bootloader = ${sysenv.TESLA_BLE_BENCH_FRAMEWORK_LIBS}/esp32/bin/bootloader_qio_80m.bin", ini)
        bench = (ROOT / "software/lib-builder/defconfig.warp4-ble-bench").read_text()
        self.assertIn("CONFIG_PARTITION_TABLE_OFFSET=0xe000", bench)
        self.assertIn("# CONFIG_SECURE_BOOT is not set", bench)
        self.assertIn("# CONFIG_SECURE_FLASH_ENC_ENABLED is not set", bench)
        self.assertIn("CONFIG_BT_BTC_TASK_STACK_SIZE=8192", bench)
        self.assertIn("CONFIG_BT_BTU_TASK_STACK_SIZE=8192", bench)
        self.assertIn("# CONFIG_BLE_MESH is not set", bench)
        self.assertIn("# CONFIG_BT_BLE_SMP_BOND_NVS_FLASH is not set", bench)

    def test_real_master_key_provisioning_functions_are_disabled_in_bench(self):
        source = (ROOT / "software/src/modules/esp32_common/esp32_common.cpp").read_text()
        for name in ("create_complete_master_key", "create_basic_master_key", "fill_missing_master_key"):
            start = source.index("bool ESP32Common::" + name + "()")
            end = source.index("\n}", start) + 2
            function = source[start:end]
            harness = '#include <cstddef>\n#define TESLA_BLE_BENCH_ONLY 1\nstruct Logger { void printfln(const char *) {} } logger;\nstruct ESP32Common { bool ' + name + '(); };\n' + function + '\nint main() { ESP32Common c; return c.' + name + '() ? 1 : 0; }\n'
            test = self.root / "guard.cpp"
            test.write_text(harness)
            result = subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(test), "-o", str(self.root / "guard")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(subprocess.run([str(self.root / "guard")]).returncode, 0)

    def test_native_dependencies_do_not_require_pruned_scratch_tree(self):
        runner = ROOT / "software/tests/tesla_vehicle/cleanroom/run_test.sh"
        harness = ROOT / "software/tests/tesla_vehicle/host/build_target_oom_harness.py"
        self.assertIn("TESLA_BLE_HOST_MBEDTLS_SOURCE", runner.read_text())
        self.assertIn("TESLA_BLE_HOST_FRAMEWORK_SOURCE", harness.read_text())

    def test_bench_never_loads_production_export_hook(self):
        import configparser
        ini = configparser.ConfigParser(interpolation=None)
        ini.read(ROOT / "software/warp4_tesla_ble.ini")
        scripts = ini["env:warp4_tesla_ble_iram_bench"]["extra_scripts"]
        self.assertNotIn("merge_firmware_hook.py", scripts)

    def test_bootloader_min_revision_3_2_rejects_ef9h_3_1(self):
        data = bytearray(image(b"revision-incompatible bootloader"))
        struct.pack_into("<H", data, 15, 302)
        data[-32:] = hashlib.sha256(data[:-32]).digest()
        for p in (self.build / "bootloader.bin", self.sdk / "esp32/bin/bootloader_qio_80m.bin",
                  self.builder / "build/bootloader/bootloader.bin"):
            p.write_bytes(data)
        self.merge()
        with self.assertRaisesRegex(ValueError, "revision"):
            self.validate()

    def test_secure_signed_and_untracked_security_options_fail_closed(self):
        for name, value in (("CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT", "y"),
                            ("CONFIG_SECURE_SIGNED_ON_BOOT", "y"),
                            ("CONFIG_SECURE_SIGNED_ON_UPDATE", "y"),
                            ("CONFIG_SECURE_SIGNED_FUTURE_OPTION", "n"),
                            ("CONFIG_EFUSE_FUTURE_WRITE_PATH", "n"),
                            ("CONFIG_FLASH_ENCRYPTION_FUTURE_MODE", "n"),
                            ("CONFIG_VENDOR_PROVISIONING", "y")):
            (self.sdk / "esp32/sdkconfig").write_text(self.config + name + "=" + value + "\n")
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "security|secure"):
                self.validate()

    def test_bootloader_actual_8000_literal_with_unchanged_e000_config(self):
        self.write_boot_elf(offset=0x8000)
        for p in (self.build / "bootloader.bin", self.sdk / "esp32/bin/bootloader_qio_80m.bin",
                  self.builder / "build/bootloader/bootloader.bin"):
            p.write_bytes(image(boot_payload(0x8000), 0x40078000))
        self.merge()
        with self.assertRaisesRegex(ValueError, "literal.*offset"):
            self.validate()

    def test_minimal_app_bin_cannot_use_ordinary_elf_sdk_metadata(self):
        (self.build / "firmware.bin").write_bytes(image(b"minimal variant substitute"))
        self.merge()
        with self.assertRaisesRegex(ValueError, "ELF.*BIN"):
            self.validate()

    def test_old_incident_app_bin_cannot_use_current_ordinary_evidence(self):
        (self.build / "firmware.bin").write_bytes(image(b"old incident app with provisioning"))
        self.merge()
        with self.assertRaisesRegex(ValueError, "ELF.*BIN"):
            self.validate()

    def test_corresponding_incident_elf_with_efuse_write_symbols_rejected(self):
        for name in ("esp_efuse_write_field_blob", "esp_efuse_burn_new_values", "ets_efuse_program",
                     "esp_efuse_batch_write_begin", "esp_flash_encrypt_chip", "wifi_prov_mgr_start_provisioning"):
            self.write_app(symbols=[(name, 0x3f400020, 4)])
            self.merge()
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "provision|eFuse|security"):
                self.validate()

    def test_missing_build_identity_is_not_retroactively_certified(self):
        (self.build / "bench_build_inputs.unchecked.json").unlink(missing_ok=True)
        with self.assertRaisesRegex(ValueError, "identity|artifact"):
            self.validate()

    def test_unbound_metadata_and_stale_sdk_are_rejected(self):
        (self.build / "metadata.json").write_text('{"variant":"minimal"}')
        with self.assertRaisesRegex(ValueError, "identity|metadata"):
            self.validate()

    def test_stale_sdk_export_and_cross_variant_identity_rejected(self):
        p = self.sdk / "esp32/sdkconfig"
        p.write_text(self.config + "CONFIG_UNRELATED_BUT_BOUND_INPUT=y\n")
        with self.assertRaisesRegex(ValueError, "stale.*SDK"):
            self.validate()
        p.write_text(self.config)
        identity = dict(self.fixture_identity, variant="minimal", environment="warp4_tesla_ble_iram_bench_minimal")
        (self.build / "bench_build_inputs.unchecked.json").write_text(json.dumps(identity))
        with self.assertRaisesRegex(ValueError, "cross-variant"):
            self.validate()

    def test_malformed_security_directive_cannot_disappear_from_tracking(self):
        for line in ("#define CONFIG_SECURE_SIGNED_NEW_PATH", "CONFIG_VENDOR_EFUSE_BURN="):
            p = self.sdk / "esp32/sdkconfig"
            p.write_text(self.config + line + "\n")
            with self.subTest(line=line), self.assertRaisesRegex(ValueError, "security"):
                report.check_config(p, "SDK", bluetooth=True)

    def test_legacy_cleanup_quarantines_only_logged_bench_outputs(self):
        common = self.root / "build"
        latest = self.root / "build_latest"
        common.mkdir()
        latest.mkdir()
        name = "warp4_firmware-UNSIGNED_bench_esptool.bin"
        (common / name).write_bytes(b"legacy bench output")
        (common / "production.bin").write_bytes(b"real production output")
        (common / "firmware_latest_esptool.bin").symlink_to(name)
        (common / "production-latest.bin").symlink_to("production.bin")
        (latest / name).symlink_to("../build/" + name)
        log = self.root / "failed-bench.log"
        log.write_text("Copying .pio/build/warp4_tesla_ble_iram_bench/firmware_esptool.bin -> build/" + name + "\nFAILED\n")
        manifest = report.identity.quarantine_legacy(self.root, [log])
        self.assertEqual(len(manifest), 3)
        self.assertFalse((common / name).exists())
        self.assertFalse((common / "firmware_latest_esptool.bin").is_symlink())
        self.assertFalse((latest / name).is_symlink())
        self.assertEqual((common / "production.bin").read_bytes(), b"real production output")
        self.assertEqual((common / "production-latest.bin").read_bytes(), b"real production output")

    def test_legacy_cleanup_accepts_actual_absolute_pio_log_paths(self):
        common = self.root / "build"
        common.mkdir()
        name = "logged-bench.elf"
        (common / name).write_bytes(b"bench")
        log = self.root / "absolute.log"
        log.write_text("Copying " + str(self.root) + "/.pio/build/warp4_tesla_ble_iram_bench/firmware.elf -> build/" + name + "\n")
        self.assertEqual(len(report.identity.quarantine_legacy(self.root, [log])), 1)

    def test_clean_revokes_exports_without_requiring_build_metadata(self):
        export = self.root / ".pio/bench-exports/ordinary"
        export.mkdir(parents=True)
        (export / "stale-report.json").write_text("old success")
        (self.root / "tools").mkdir()
        (self.root / "tools/bench_image_report.py").symlink_to(TOOL)
        outer = self
        class Env:
            def subst(self, value):
                return value.replace("$PROJECT_DIR", str(outer.root)).replace("$BUILD_DIR", str(outer.build))
            def __getitem__(self, key):
                return "warp4_tesla_ble_iram_bench"
            def IsCleanTarget(self):
                return True
        class CleanReturn(Exception):
            pass
        def clean_return():
            raise CleanReturn()
        with self.assertRaises(CleanReturn):
            runpy.run_path(str(ROOT / "software/bench_image_hook.py"),
                          init_globals={"Import": lambda name: None, "env": Env(), "Return": clean_return})
        self.assertFalse(export.exists())

    def test_sdk_header_moved_by_export_is_bound_before_build(self):
        b = self.root / "fresh-sdk"
        (b / "build/config").mkdir(parents=True)
        header = b / "build/config/sdkconfig.h"
        header.write_text("#define CONFIG_PARTITION_TABLE_OFFSET 0xe000\n")
        sources = {str(self.root / "fixture-source.c"): report.identity.sha(self.root / "fixture-source.c")}
        with patch.object(report.identity, "sdk_sources", return_value=sources), \
             patch.object(report.identity, "export_header_suffix", return_value="unit metadata\n"):
            report.identity.begin_sdk(b)
            start = json.loads((b / "bench_sdk_start.unchecked.json").read_text())
            self.assertEqual(start["generated_config_sha256"], report.identity.sha(header))
            sdk = b / "out/tools/esp32-arduino-libs"
            target = sdk / "esp32/qio_qspi/include/sdkconfig.h"
            target.parent.mkdir(parents=True)
            header.rename(target)
            with target.open("a") as f:
                f.write(start["export_header_suffix"])
            for name in ("bootloader/bootloader.bin", "bootloader/bootloader.elf", "bootloader/bootloader.map",
                         "bootloader/config/sdkconfig.h", "compile_commands.json", "CMakeCache.txt"):
                p = b / "build" / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text("unit build output\n")
            proof = report.identity.seal_sdk(b, False)
            self.assertEqual(proof["inputs"]["generated_config_sha256"], hashlib.sha256(
                target.read_bytes()[:-len(start["export_header_suffix"])]).hexdigest())

    def test_explicit_final_export_is_validated_not_a_different_work_image(self):
        final = self.root / "BENCH_ONLY-ordinary-full.bin"
        final.write_bytes((self.build / "firmware_esptool.bin").read_bytes())
        result = report.validate(self.build, self.sdk, self.builder, self.ota, full_image=final)
        self.assertEqual(result["image"]["path"], str(final))
        bad = bytearray(final.read_bytes())
        bad[0] ^= 1
        final.write_bytes(bad)
        with self.assertRaises(ValueError):
            report.validate(self.build, self.sdk, self.builder, self.ota, full_image=final)

    def test_pre_guard_revokes_success_before_any_other_build_hook(self):
        import configparser
        ini = configparser.ConfigParser(interpolation=None)
        ini.read(ROOT / "software/warp4_tesla_ble.ini")
        scripts = ini["env:warp4_tesla_ble_iram_bench"]["extra_scripts"].split()
        self.assertEqual(scripts[0], "pre:bench_guard_hook.py")
        export = self.root / ".pio/bench-exports/ordinary"
        export.mkdir(parents=True)
        (export / "stale-report.json").write_text("old success")
        outer = self
        class Env:
            def subst(self, value):
                return value.replace("$PROJECT_DIR", str(outer.root)).replace("$BUILD_DIR", str(outer.build))
            def __getitem__(self, key):
                return "warp4_tesla_ble_iram_bench"
        runpy.run_path(str(ROOT / "software/bench_guard_hook.py"),
                      init_globals={"Import": lambda name: None, "env": Env()})
        self.assertFalse(export.exists())

    def test_sdk_export_allows_only_exact_recorded_metadata_suffix(self):
        header = self.root / "exported-sdkconfig.h"
        compiled = b"#define CONFIG_PARTITION_TABLE_OFFSET 0xe000\n"
        suffix = '#define CONFIG_ARDUINO_IDF_COMMIT "fixture"\n#define CONFIG_ARDUINO_IDF_BRANCH ""\n'
        start = {"generated_config_sha256": hashlib.sha256(compiled).hexdigest(), "export_header_suffix": suffix}
        header.write_bytes(compiled + suffix.encode())
        report.identity.verify_export_header(header, start)
        for wrong in (compiled.replace(b"0xe000", b"0x8000") + suffix.encode(),
                      compiled + suffix.replace("fixture", "stale").encode(),
                      compiled + suffix.encode() + b"#define CONFIG_SECURE_SIGNED_ON_BOOT 1\n"):
            header.write_bytes(wrong)
            with self.assertRaises(ValueError):
                report.identity.verify_export_header(header, start)

    def test_variant_claim_must_agree_with_bound_actual_sdk_config(self):
        p = self.sdk / "bench_sdk_provenance.json"
        proof = json.loads(p.read_text())
        proof["variant"] = "minimal"
        p.write_text(json.dumps(proof))
        self.fixture_identity.update(variant="minimal", environment="warp4_tesla_ble_iram_bench_minimal",
                                     sdk_provenance_sha256=report.identity.sha(p))
        (self.build / "bench_build_inputs.unchecked.json").write_text(json.dumps(self.fixture_identity))
        self.write_app()
        self.merge()
        with self.assertRaisesRegex(ValueError, "variant.*SDK|SDK.*variant"):
            self.validate()

    def test_added_source_file_invalidates_pre_compilation_input_set(self):
        directory = self.root / "compiler-inputs"
        directory.mkdir()
        (directory / "a.cpp").write_text("original\n")
        record = {"roots": [str(directory)], "files": report.identity.snapshot([directory])}
        report.identity.verify_live_inputs(record)
        (directory / "new.cpp").write_text("untracked compiled input\n")
        with self.assertRaisesRegex(ValueError, "input set"):
            report.identity.verify_live_inputs(record)

    def test_added_sdk_source_file_invalidates_sdk_identity(self):
        (self.builder / "main").mkdir(exist_ok=True)
        (self.builder / "main/untracked.c").write_text("new SDK input\n")
        with self.assertRaisesRegex(ValueError, "SDK.*input set"):
            self.validate()

    def test_platformio_backup_must_be_an_exact_copy_of_bound_build_script(self):
        backup = self.sdk / "esp32/pioarduino-build.py.esp32"
        backup.write_bytes((self.sdk / "esp32/pioarduino-build.py").read_bytes())
        self.validate()
        backup.write_text("tampered platform build script\n")
        with self.assertRaisesRegex(ValueError, "SDK.*identity"):
            self.validate()

    def test_bench_preparation_preserves_production_latest(self):
        # Execute the actual pre-hook cleanup block, not a parallel test model.
        tree = ast.parse((ROOT / "software/pio_hooks.py").read_text())
        cleanup = next(n for n in ast.walk(tree) if isinstance(n, ast.If)
                       and any(isinstance(x, ast.Name) and x.id == "symlinks_to_remove"
                               for x in ast.walk(n)))
        import glob
        class Env:
            def GetProjectOption(self, name, default=None):
                return "true" if name == "custom_bench_only" else default
        (self.root / "build").mkdir()
        latest = self.root / "build/firmware_latest.elf"
        latest.symlink_to("real-production.elf")
        with patch("os.getcwd", return_value=str(self.root)):
            old = os.getcwd()
        old = Path.cwd()
        try:
            os.chdir(self.root)
            exec(compile(ast.Module(body=[cleanup], type_ignores=[]), "pio_cleanup", "exec"),
                 {"build": True, "product_id": "warp4", "env": Env(), "os": os, "glob": glob})
        finally:
            os.chdir(old)
        self.assertTrue(latest.is_symlink(), "bench pre-hook removed production latest")

    def test_failed_validation_leaves_no_exports_or_checked_report(self):
        # Hook registration/execution with SCons-like callbacks and a failing gate.
        # The final destination must be revoked even after a previous good build.
        export = self.root / ".pio/bench-exports/ordinary"
        export.mkdir(parents=True)
        (export / "BENCH_ONLY-ordinary-report.json").write_text("stale success")
        (self.build / "bench_image_report.json").write_text("stale success")
        actions = []
        outer = self
        class Env:
            def subst(self, value):
                return value.replace("$PROJECT_DIR", str(outer.root)).replace("$BUILD_DIR", str(outer.build))
            def GetProjectOption(self, key, default=None):
                return {"board_build.arduino.custom_bootloader": str(outer.build / "bootloader.bin"),
                        "custom_provisioning": "false", "custom_bench_only": "true",
                        "custom_bench_builder": str(outer.builder)}.get(key, default)
            def AddPostAction(self, target, action):
                actions.append(action)
            def AddPreAction(self, target, action):
                pass
            def Append(self, **kwargs):
                pass
            def IsCleanTarget(self):
                return False
            def PioPlatform(self):
                return self
            def get_package_dir(self, name):
                return str(outer.sdk)
            def __getitem__(self, name):
                return "warp4_tesla_ble_iram_bench"
        (self.root / "tools").mkdir()
        (self.root / "tools/bench_image_report.py").symlink_to(TOOL)
        (self.build / "metadata.json").write_text(json.dumps({"signature_preset": "", "secure_boot": "", "split_esptool_ota": True}))
        env = Env()
        with patch.object(report.identity, "capture_firmware", return_value={"files": {}}):
            namespace = runpy.run_path(str(ROOT / "software/bench_image_hook.py"),
                                      init_globals={"Import": lambda name: None, "env": env})
        with patch.object(namespace["report"], "validate", side_effect=ValueError("rejected fixture")), \
             patch("subprocess.check_call"):
            with self.assertRaises(ValueError):
                for action in actions:
                    action(source=[], target=[], env=env)
        self.assertFalse(export.exists(), "failed validation retained a valid-looking export")
        self.assertFalse((self.build / "bench_image_report.json").exists())


if __name__ == "__main__":
    unittest.main()
