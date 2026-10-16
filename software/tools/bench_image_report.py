#!/usr/bin/env python3
"""Fail-closed validation of non-secure WARP4 bench build artifacts.

This validates packaging, not successful hardware boot or Bluetooth runtime.
No serial access, flashing, eFuse operations, signing or encryption.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench_build_identity as identity

WARP4_PARTITIONS = [
    ("app0", 0, 0x10, 0x10000, 0x630000, 0),
    ("otadata", 1, 0, 0x640000, 0x2000, 0),
    ("factorydata", 1, 0x83, 0x642000, 0xe000, 0),
    ("app1", 0, 0x11, 0x650000, 0x630000, 0),
    ("data", 1, 0x83, 0xc90000, 0x360000, 0),
    ("coredump", 1, 3, 0xff0000, 0x10000, 0),
]
SECURE_OPTIONS = ("CONFIG_SECURE_BOOT", "CONFIG_SECURE_BOOT_V2_ENABLED",
                  "CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES", "CONFIG_SECURE_FLASH_ENC_ENABLED",
                  "CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE", "CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK",
                  "CONFIG_APP_ANTI_ROLLBACK", "CONFIG_FLASH_ENCRYPTION_ENABLED",
                  "CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT", "CONFIG_SECURE_SIGNED_ON_BOOT",
                  "CONFIG_SECURE_SIGNED_ON_UPDATE", "CONFIG_EFUSE_CUSTOM_TABLE", "CONFIG_EFUSE_VIRTUAL",
                  "CONFIG_EFUSE_CODE_SCHEME_COMPAT_NONE", "CONFIG_EFUSE_CODE_SCHEME_COMPAT_REPEAT",
                  "CONFIG_BLE_MESH_PROVISIONER", "CONFIG_ESP_TLS_INSECURE", "CONFIG_ESP_TLS_USE_SECURE_ELEMENT",
                  "CONFIG_BOOTLOADER_SKIP_VALIDATE_ON_POWER_ON", "CONFIG_BOOTLOADER_SKIP_VALIDATE_ALWAYS",
                  "CONFIG_BT_GATTS_SECURITY_LEVELS_CHAR", "CONFIG_SPI_FLASH_ENABLE_ENCRYPTED_READ_WRITE",
                  "CONFIG_WIFI_PROV_BLE_FORCE_ENCRYPTION", "CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_1",
                  "CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2")
# Explicit pinned-IDF capability/read-only exceptions. No wildcard allows a new
# security switch through, even if it is currently disabled.
SECURITY_CONSTANTS = {
    **{name: {"y", "1"} for name in (
        "CONFIG_SOC_EFUSE_SUPPORTED", "CONFIG_SOC_EFUSE_SECURE_BOOT_KEY_DIGESTS",
        "CONFIG_SOC_SECURE_BOOT_SUPPORTED", "CONFIG_SOC_SECURE_BOOT_V1",
        "CONFIG_SOC_SPI_MEM_SUPPORT_CONFIG_GPIO_BY_EFUSE", "CONFIG_SECURE_BOOT_V1_SUPPORTED",
        "CONFIG_SECURE_BOOT_V2_PREFERRED", "CONFIG_SECURE_BOOT_V2_RSA_SUPPORTED",
        "CONFIG_EFUSE_CODE_SCHEME_COMPAT_3_4", "CONFIG_ADC_CALI_EFUSE_TP_ENABLE",
        "CONFIG_ADC_CALI_EFUSE_VREF_ENABLE", "CONFIG_ADC_CAL_EFUSE_TP_ENABLE",
        "CONFIG_ADC_CAL_EFUSE_VREF_ENABLE", "CONFIG_MQTT_TRANSPORT_WEBSOCKET_SECURE",
        "CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_0", "CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION",
        "CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP")},
    "CONFIG_SOC_MEMSPI_ENCRYPTION_ALIGNMENT": {"16"},
    "CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM": {"7"},
    "CONFIG_SOC_FLASH_ENCRYPTED_XTS_AES_BLOCK_MAX": {"32"},
    "CONFIG_SECURE_BOOT_IMAGE_DIGEST_LEN": {"32"},
    "CONFIG_SECURE_BOOT_ROM_FAST_WAKE_RESERVE_SIZE": {"0"},
    "CONFIG_EFUSE_MAX_BLK_LEN": {"192"},
    "CONFIG_ESP_EFUSE_BLOCK_REV_MIN_FULL": {"0"},
    "CONFIG_ESP_EFUSE_BLOCK_REV_MAX_FULL": {"99"},
}
SECURITY_KEY = re.compile(r"CONFIG_\w*(?:SECUR|ENCRYPT|EFUSE|PROVISION|ANTI_ROLLBACK|SKIP_VALIDATE)\w*")


def read(path):
    try:
        data = Path(path).read_bytes()
    except OSError as exc:
        raise ValueError(f"required artifact {path}: {exc.strerror}") from exc
    if not data:
        raise ValueError(f"empty required artifact: {path}")
    return data


def config(path):
    values = {}
    for line in read(path).decode().splitlines():
        disabled = re.fullmatch(r"# (CONFIG_\w+) is not set", line)
        if disabled:
            line = disabled[1] + "=n"
        match = re.fullmatch(r"(CONFIG_\w+)=(.+)", line) or re.fullmatch(r"#define (CONFIG_\w+) (.+)", line)
        directive = re.match(r"(?:#define\s+)?(CONFIG_\w+)", line)
        if not match and directive and SECURITY_KEY.fullmatch(directive[1]):
            raise ValueError(f"malformed/untracked security config directive: {line}")
        if match:
            key, value = match.groups()
            if key in values and values[key] != value:
                raise ValueError(f"conflicting secure/config entry: {key} in {path}")
            values[key] = value
    return values


def check_config(path, label, bluetooth=False):
    values = config(path)
    offset = int(values.get("CONFIG_PARTITION_TABLE_OFFSET", "0"), 0)
    if offset != 0xe000:
        raise ValueError(f"{label} partition table offset {offset:#x} != merged WARP4 offset 0xe000")
    if values.get("CONFIG_ESPTOOLPY_FLASHSIZE") != '"16MB"':
        raise ValueError(f"{label} flash size is not 16MB")
    for name, value in values.items():
        if not SECURITY_KEY.fullmatch(name):
            continue
        allowed = {"n", "0"} if name in SECURE_OPTIONS else SECURITY_CONSTANTS.get(name)
        if allowed is None or value not in allowed:
            raise ValueError(f"{label} unknown/unsafe secure security option: {name}={value}")
    if bluetooth:
        for name in ("CONFIG_BT_ENABLED", "CONFIG_BT_BLUEDROID_ENABLED", "CONFIG_BT_GATTC_ENABLE"):
            if values.get(name) != "y":
                raise ValueError(f"{label} missing Bluedroid option: {name}")
        for name in ("CONFIG_BT_BTC_TASK_STACK_SIZE", "CONFIG_BT_BTU_TASK_STACK_SIZE"):
            if values.get(name) != "8192":
                raise ValueError(f"{label} must preserve prior 8192-byte task stack: {name}")
        for name in ("CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE", "CONFIG_BT_BLE_SMP_BOND_NVS_FLASH", "CONFIG_BT_GATTC_CACHE_NVS_FLASH", "CONFIG_ESP_WIFI_NVS_ENABLED"):
            if values.get(name) == "y":
                raise ValueError(f"{label} requires absent NVS partition: {name}")
    # Pinned qio/80m SDK: ROM loads DIO, then IDF enables QIO. The generated
    # FLASHMODE string deliberately differs from the selected QIO boolean.
    for name, allowed in {
        "CONFIG_ESPTOOLPY_FLASHMODE_QIO": {"y", "1"},
        "CONFIG_ESPTOOLPY_FLASHMODE": {'"dio"'},
        "CONFIG_ESPTOOLPY_FLASHFREQ_80M": {"y", "1"},
        "CONFIG_ESPTOOLPY_FLASHFREQ": {'"80m"'},
    }.items():
        if values.get(name) not in allowed:
            raise ValueError(f"{label} flash configuration differs from pinned QIO/DIO-header 80m: {name}")
    return values


def partitions(data):
    if len(data) != 3072:
        raise ValueError("partition table must be 3072 bytes")
    entries = []
    for pos in range(0, len(data), 32):
        row = data[pos:pos+32]
        magic = struct.unpack_from("<H", row)[0]
        if magic == 0xebeb:
            if row[2:16] != b"\xff" * 14 or row[16:] != hashlib.md5(data[:pos]).digest():
                raise ValueError("partition table MD5 invalid")
            if data[pos+32:] != b"\xff" * (len(data)-pos-32):
                raise ValueError("partition table trailing data")
            if entries != WARP4_PARTITIONS:
                raise ValueError(f"partition table contents/layout mismatch: {entries!r}")
            return entries
        if magic != 0x50aa:
            raise ValueError(f"partition table invalid magic number {magic:#x} at {pos:#x}")
        _, kind, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", row)
        entries.append((label.rstrip(b"\0").decode("ascii"), kind, subtype, offset, size, flags))
    raise ValueError("partition table missing MD5 record")


class Elf:
    """Bounded ELF32 little-endian Xtensa section/symbol reader (no tools)."""
    def __init__(self, path):
        self.data = read(path)
        if self.data[:7] != b"\x7fELF\x01\x01\x01" or len(self.data) < 52:
            raise ValueError(f"invalid ELF32: {path}")
        header = struct.unpack_from("<HHIIIIIHHHHHH", self.data, 16)
        if header[0:3] != (2, 94, 1) or header[10] != 40 or not 1 <= header[11] <= 256:
            raise ValueError(f"unsupported ELF headers: {path}")
        self.entry = header[3]
        self.sections = [struct.unpack("<10I", self.slice(header[5] + n * 40, 40))
                         for n in range(header[11])]
        names = self.sections[header[12]]
        self.names = self.slice(names[4], names[5])
        self.symbols = {}
        for s in self.sections:
            if s[1] != 2:
                continue
            if s[9] != 16 or s[5] % 16:
                raise ValueError("invalid ELF symbol table")
            strings = self.sections[s[6]]
            strings = self.slice(strings[4], strings[5])
            for pos in range(s[4], s[4] + s[5], 16):
                name, value, size, info, other, index = struct.unpack("<IIIBBH", self.slice(pos, 16))
                if name and index:
                    self.symbols[self.string(strings, name)] = (value, size, info, index)
        if not self.symbols:
            raise ValueError("ELF missing symbol evidence")

    @staticmethod
    def string(data, offset):
        if not 0 <= offset < len(data) or b"\0" not in data[offset:]:
            raise ValueError("invalid ELF string")
        return data[offset:data.index(0, offset)].decode("utf-8")

    def slice(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError("truncated ELF evidence")
        return self.data[offset:offset + size]

    def allocated(self):
        return [s for s in self.sections if s[1] == 1 and s[2] & 2 and s[5]]

    def at(self, address, size):
        matches = [s for s in self.allocated() if s[3] <= address and address + size <= s[3] + s[5]]
        if len(matches) != 1:
            raise ValueError(f"ELF address not uniquely backed: {address:#x}")
        s = matches[0]
        return self.slice(s[4] + address - s[3], size)


def heap_reservations(elf):
    """Validate the actual ESP32 linker-collected heap exclusion table.

    Match memory_layout_utils.c's sort, word expansion and strict start/overlap
    checks. ESP32 has no runtime ROM-layout-table entry. Table addresses and
    region endpoints are read from ELF bytes, not reconstructed from config.
    """
    try:
        start = elf.symbols["soc_reserved_memory_region_start"][0]
        end = elf.symbols["soc_reserved_memory_region_end"][0]
    except KeyError as exc:
        raise ValueError("ELF missing heap reservation table") from exc
    if end <= start or (end - start) % 8:
        raise ValueError("invalid ELF heap reservation table extent")
    names = {value[0]: name for name, value in elf.symbols.items()
             if name.startswith("reserved_region_") and value[1] == 8}
    regions = []
    for index, (low, high) in enumerate(struct.iter_unpack("<II", elf.at(start, end - start))):
        regions.append({"name": names.get(start + index * 8, f"table_entry_{index}"),
                        "start": low & ~3, "end": (high + 3) & ~3,
                        "table_address": start + index * 8})
    regions.sort(key=lambda region: region["start"])
    for index, region in enumerate(regions):
        if region["end"] < region["start"]:
            raise ValueError(f"reversed heap reservation: {region}")
        if index + 1 < len(regions):
            following = regions[index + 1]
            if following["start"] <= region["start"] or region["end"] > following["start"]:
                raise ValueError("heap reservation duplicate start/overlap: "
                                 f"{region['name']} {region['start']:#010x}..{region['end']:#010x} vs "
                                 f"{following['name']} {following['start']:#010x}..{following['end']:#010x}")
    return {"count": len(regions), "table_start": start, "table_end": end, "regions": regions}


def bootloader_literal(elf):
    function = elf.symbols.get("bootloader_utility_load_partition_table")
    mmap = elf.symbols.get("bootloader_mmap")
    if not function or not mmap:
        raise ValueError("bootloader missing partition reader ELF symbols")
    address = function[0]
    code = elf.at(address, 12)
    # Pinned IDF instruction shape: entry; l32r a11,size; l32r a10,offset;
    # call8 bootloader_mmap. Reject unfamiliar code instead of guessing.
    call = int.from_bytes(code[9:12], "little")
    displacement = call >> 6
    if displacement & (1 << 17):
        displacement -= 1 << 18
    target = ((address + 9) & ~3) + 4 + displacement * 4
    if code[:3] != bytes.fromhex("366100") or code[3] != 0xb1 or code[6] != 0xa1 or call & 63 != 0x25 or target != mmap[0]:
        raise ValueError("bootloader partition literal instruction shape unproven")
    literal_address = ((address + 6 + 3) & ~3) + (int.from_bytes(code[7:9], "little") - 65536) * 4
    offset = struct.unpack("<I", elf.at(literal_address, 4))[0]
    if offset != 0xe000:
        raise ValueError(f"bootloader actual literal partition offset {offset:#x} != 0xe000")
    return {"function": address, "literal_address": literal_address, "offset": offset}


def esp_image(data, label):
    if len(data) < 24 or data[0] != 0xe9 or not 1 <= data[1] <= 16:
        raise ValueError(f"{label}: invalid ESP image header")
    if struct.unpack_from("<H", data, 12)[0] != 0:
        raise ValueError(f"{label}: image is not classic ESP32")
    # Classic ESP32 supports QIO/QOUT/DIO/DOUT and 40/26/20/80 MHz encodings.
    # This validator is narrower: the pinned qio/80m SDK generates a DIO ROM
    # header (CONFIG_ESPTOOLPY_FLASHMODE="dio"), then enables QIO in IDF.
    if data[2] not in (0, 1, 2, 3) or data[2] != 2:
        raise ValueError(f"{label}: unsupported/unpinned flash mode {data[2]:#x}; DIO header required")
    if data[3] & 15 not in (0, 1, 2, 15) or data[3] & 15 != 15:
        raise ValueError(f"{label}: unsupported/unpinned flash frequency {data[3] & 15:#x}; 80m required")
    if data[3] >> 4 != 4:
        raise ValueError(f"{label}: image flash size is not 16MB")
    minimum = max(data[14] * 100, struct.unpack_from("<H", data, 15)[0])
    maximum = struct.unpack_from("<H", data, 17)[0]
    if minimum > 301 or (maximum not in (0, 65535) and maximum < 301):
        raise ValueError(f"{label}: chip revision range {minimum}..{maximum} excludes ef9h rev 3.1")
    pos, checksum, segments = 24, 0xef, []
    for _ in range(data[1]):
        if pos + 8 > len(data):
            raise ValueError(f"{label}: truncated segment header")
        address, size = struct.unpack_from("<II", data, pos)
        pos += 8
        if pos + size > len(data):
            raise ValueError(f"{label}: truncated segment")
        segments.append((address, pos, data[pos:pos+size]))
        for value in data[pos:pos+size]:
            checksum ^= value
        pos += size
    check_pos = pos + (15 - pos) % 16
    if check_pos >= len(data) or data[check_pos] != checksum:
        raise ValueError(f"{label}: image checksum invalid")
    end = check_pos + 1
    if data[23] != 1:
        raise ValueError(f"{label}: missing appended SHA256")
    if data[end:end+32] != hashlib.sha256(data[:end]).digest():
        raise ValueError(f"{label}: image SHA256 invalid")
    if len(data) != end+32:
        raise ValueError(f"{label}: unexpected trailing image/signature data")
    return segments


def match_elf(data, elf, label):
    segments = esp_image(data, label)
    ranges = sorted((address, address + len(payload)) for address, _, payload in segments if address)
    if any(right[0] < left[1] for left, right in zip(ranges, ranges[1:])):
        raise ValueError(f"{label}: overlapping loadable segments")
    for address, _, payload in segments:
        if address and not any(address < section[3] + section[5] and section[3] < address + len(payload)
                               for section in elf.allocated()):
            raise ValueError(f"{label}: ELF/BIN unbacked loadable segment")
    if label == "application" and not any(elf.string(elf.names, s[0]) == ".flash.appdesc" for s in elf.allocated()):
        raise ValueError("application ELF/BIN missing checksum-bound app descriptor")
    expected = [bytearray(len(payload)) for _, _, payload in segments]
    sections = sorted(elf.allocated(), key=lambda s: s[3])
    if any(right[3] < left[3] + left[5] for left, right in zip(sections, sections[1:])):
        raise ValueError(f"{label}: overlapping allocated ELF sections")
    # esptool ImageSegment/ELFSection pads each nonzero-address section LENGTH
    # to 4 bytes before merging adjacent sections. Permit only those exact
    # zero-filled 0..3-byte tails, never general zero-fill for holes/extensions.
    backed = [(s[3], s[3] + s[5] + (-s[5] % 4)) for s in sections]
    for address, _, payload in segments:
        if not address:
            continue  # Non-loadable MMU alignment segment; bytes checked below.
        end, cursor = address + len(payload), address
        for low, high in backed:
            start, stop = max(address, low), min(end, high)
            if start < stop:
                if start > cursor:
                    raise ValueError(f"{label}: ELF/BIN unbacked loadable extent {cursor:#x}..{start:#x}")
                cursor = max(cursor, stop)
        if cursor != end:
            raise ValueError(f"{label}: ELF/BIN unbacked loadable extent {cursor:#x}..{end:#x}")
    for s in elf.allocated():
        covered = []
        for i, (address, _, payload) in enumerate(segments):
            start, end = max(address, s[3]), min(address + len(payload), s[3] + s[5])
            if start < end:
                covered.append((start, end))
                expected[i][start-address:end-address] = elf.slice(s[4] + start-s[3], end-start)
        covered.sort()
        cursor = s[3]
        for start, end in covered:
            if start != cursor:
                raise ValueError(f"{label}: ELF section not uniquely in BIN: {elf.string(elf.names, s[0])}")
            cursor = end
        if cursor != s[3] + s[5]:
            raise ValueError(f"{label}: ELF section missing in BIN: {elf.string(elf.names, s[0])}")
    if any(elf.string(elf.names, s[0]) == ".flash.appdesc" for s in elf.allocated()):
        if data[0x20:0x24] != bytes.fromhex("3254cdab"):
            raise ValueError(f"{label}: missing app descriptor")
        for i, (_, pos, payload) in enumerate(segments):
            if pos <= 0xb0 and 0xd0 <= pos + len(payload):
                expected[i][0xb0-pos:0xd0-pos] = hashlib.sha256(elf.data).digest()
    if struct.unpack_from("<I", data, 4)[0] != elf.entry or any(bytes(want) != got[2] for want, got in zip(expected, segments)):
        raise ValueError(f"{label}: exact ELF section/BIN correspondence failed")


def exclude_provisioning(elf, label):
    forbidden = re.compile(r"(?:efuse.*(?:write|burn|program|batch|commit|protect|set_|update)|"
                           r"(?:flash_encrypt|flash_encryption).*(?:chip|start|write|init|key)|"
                           r"secure_boot.*(?:enable|generate|sign|init|burn)|"
                           r"(?:wifi_prov_|esp_prov_|blufi_).*|.*start_provisioning)", re.I)
    bad = [name for name, (_, _, info, index) in elf.symbols.items()
           if info & 15 != 4 and forbidden.search(name)]
    if bad:
        raise ValueError(f"{label}: eFuse/security/provisioning symbols forbidden: {sorted(bad)}")


def match_map(path, elf):
    """Require linker output-section identity, not merely a supplied map hash."""
    data = read(path)
    rows = {(name, int(address, 16), int(size, 16)) for name, address, size in
            re.findall(r"(?m)^(\.[\w.$-]+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)", data.decode())}
    for section in elf.allocated():
        expected = (elf.string(elf.names, section[0]), section[3], section[5])
        if expected not in rows:
            raise ValueError(f"map/ELF output section identity mismatch: {expected[0]}")
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def validate(build_dir, sdk, builder, boot_app0, target_offset=0x1000, full_image=None):
    build_dir, sdk, builder = Path(build_dir), Path(sdk), Path(builder)
    if target_offset != 0x1000:
        raise ValueError("bench merge target offset must be 0x1000")
    check_config(builder / "build/bootloader/config/sdkconfig.h", "bootloader")
    sdk_config = check_config(sdk / "esp32/sdkconfig", "SDK", bluetooth=True)
    check_config(sdk / "esp32/qio_qspi/include/sdkconfig.h", "SDK header")
    bootloader = read(build_dir / "bootloader.bin")
    if bootloader != read(sdk / "esp32/bin/bootloader_qio_80m.bin") or bootloader != read(builder / "build/bootloader/bootloader.bin"):
        raise ValueError("bootloader artifacts differ: firmware, exported SDK and builder must match")
    application = read(build_dir / "firmware.bin")
    table = read(build_dir / "partitions.bin")
    entries = partitions(table)
    esp_image(bootloader, "bootloader")
    esp_image(application, "application")
    boot_elf = Elf(builder / "build/bootloader/bootloader.elf")
    if boot_elf.data != read(sdk / "esp32/bin/bootloader_qio_80m.elf"):
        raise ValueError("bootloader ELF exports differ")
    actual_partition = bootloader_literal(boot_elf)
    match_elf(bootloader, boot_elf, "bootloader")
    app_elf = Elf(build_dir / "firmware.elf")
    match_elf(application, app_elf, "application")
    heap_regions = heap_reservations(app_elf)
    exclude_provisioning(boot_elf, "bootloader")
    exclude_provisioning(app_elf, "application")
    binding = identity.verify_firmware(build_dir, sdk, builder, app_elf)
    # Current ordinary bench intentionally drops only unused Mesh pools; GATTS
    # and SMP stay enabled. Minimal retains its separate reduced feature set.
    expected = "y" if binding["variant"] == "ordinary" else "n"
    for option in ("CONFIG_BT_GATTS_ENABLE", "CONFIG_BT_BLE_SMP_ENABLE", "CONFIG_BLE_MESH"):
        wanted="n" if option=="CONFIG_BLE_MESH" else expected
        if sdk_config.get(option, "n") != wanted:
            raise ValueError(f"bound variant {binding['variant']} disagrees with actual SDK config {option}")
    if len(bootloader) > 0xc000 or len(application) > 0x630000:
        raise ValueError("bootloader/application exceeds WARP4 partition capacity")
    ota = read(boot_app0)
    if len(ota) != 0x2000:
        raise ValueError("boot_app0 must initialize both 4KB otadata sectors")
    ota_hashes = {checksum for path, checksum in binding["inputs"]["files"].items()
                  if Path(path).name == "boot_app0.bin"}
    if ota_hashes != {hashlib.sha256(ota).hexdigest()}:
        raise ValueError("boot_app0 differs from exact pre-compilation OTA identity")
    app_map = match_map(build_dir / "firmware.map", app_elf)
    boot_map = match_map(builder / "build/bootloader/bootloader.map", boot_elf)
    full_image = Path(full_image) if full_image is not None else build_dir / "firmware_esptool.bin"
    merged = read(full_image)
    pieces = [(0x1000, bootloader, "bootloader"), (0xe000, table, "partition table"),
              (0x10000, application, "application"), (0x640000, ota, "otadata")]
    if len(merged) != 0x642000 - target_offset:
        raise ValueError("merged image length/target offset invalid (must end before factorydata)")
    previous_end = 0
    for offset, data, label in pieces:
        start = offset - target_offset
        if merged[start:start+len(data)] != data:
            raise ValueError(f"merged image {label} missing/different at absolute {offset:#x}, relative {start:#x}")
        if merged[previous_end:start] != b"\xff" * (start-previous_end):
            raise ValueError(f"unexpected merged data before {label}")
        previous_end = start + len(data)
    return {
        "installable": False, "status": "NON_INSTALLABLE_BENCH_REVIEW_ONLY_NOT_A_RELEASE",
        "validation": "software_packaging_only_not_hardware_boot_or_runtime",
        "flash_target_offset": target_offset, "partition_table_offset": 0xe000,
        "flash_size": 0x1000000, "secure_boot": False, "flash_encryption": False,
        "bootloader_partition_literal": actual_partition,
        "heap_reservations": heap_regions,
        "binding": binding,
        "artifacts": {
            str((build_dir / "firmware.elf").resolve()): {"bytes": len(app_elf.data), "sha256": hashlib.sha256(app_elf.data).hexdigest()},
            str((build_dir / "firmware.map").resolve()): app_map,
            str((build_dir / "metadata.json").resolve()): {"bytes": len(read(build_dir / "metadata.json")), "sha256": binding["inputs"]["metadata_sha256"]},
            str((build_dir / "bench_build_inputs.unchecked.json").resolve()): {"bytes": len(read(build_dir / "bench_build_inputs.unchecked.json")), "sha256": identity.sha(build_dir / "bench_build_inputs.unchecked.json")},
            str((builder / "build/bootloader/bootloader.elf").resolve()): {"bytes": len(boot_elf.data), "sha256": hashlib.sha256(boot_elf.data).hexdigest()},
            str((builder / "build/bootloader/bootloader.map").resolve()): boot_map},
        "factorydata": "not_in_image_requires_erased_bench_flash",
        "partitions": [dict(zip(("name", "type", "subtype", "offset", "size", "flags"), row)) for row in entries],
        "pieces": [{"name": label, "offset": offset, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()} for offset, data, label in pieces],
        "image": {"path": str(full_image.resolve()), "bytes": len(merged), "sha256": hashlib.sha256(merged).hexdigest()},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--builder", type=Path, required=True)
    parser.add_argument("--boot-app0", type=Path, required=True)
    parser.add_argument("--image", type=Path, help="Validate this exact isolated final export, not the work image")
    parser.add_argument("--target-offset", type=lambda s: int(s, 0), default=0x1000)
    args = parser.parse_args()
    try:
        result = validate(args.build_dir, args.sdk, args.builder, args.boot_app0, args.target_offset, args.image)
    except (ValueError, OSError, UnicodeError, struct.error, KeyError, IndexError, TypeError) as exc:
        print(f"bench image validation failed: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
