"""Independent byte verifier; no producer imports, devices, or writes."""
import csv
import hashlib
import io
import struct
from zlib import crc32


def need(ok, message):
    if not ok:
        raise ValueError(message)


def verify_fit(size):
    need(type(size) is int and size > 0, "invalid final app size")
    reserved = ((size + 4095) & ~4095) + 4096
    need(reserved <= 0x630000, "app slot exceeded including secure-boot signature sector")
    return reserved


def verify_partitions(text):
    rows = []
    for row in csv.reader(io.StringIO(text)):
        if not row or row[0].lstrip().startswith("#"):
            continue
        row = [s.strip() for s in row]
        need(len(row) == 6, "partition row format")
        rows.append((row[0], row[1], row[2], int(row[3], 0), int(row[4], 0), row[5]))
    expected = [("app0", "app", "ota_0", 0x10000, 0x630000, ""),
                ("otadata", "data", "ota", 0x640000, 0x2000, ""),
                ("factorydata", "data", "littlefs", 0x642000, 0xe000, ""),
                ("app1", "app", "ota_1", 0x650000, 0x630000, ""),
                ("data", "data", "littlefs", 0xc90000, 0x360000, ""),
                ("coredump", "data", "coredump", 0xff0000, 0x10000, "")]
    need(rows == expected, "WARP4 secure partition layout mismatch")


def verify_app(app):
    verify_fit(len(app))
    need(len(app) >= 24 and app[0] == 0xe9 and 1 <= app[1] <= 16, "ESP32 app header/segments")
    need(app[12:14] == b"\0\0" and app[23] in (0, 1), "ESP32 chip/hash flag")
    pos = 24
    checksum = 0xef
    for _ in range(app[1]):
        need(pos + 8 <= len(app), "truncated segment header")
        _, size = struct.unpack_from("<II", app, pos)
        pos += 8
        need(size > 0 and pos + size <= len(app), "truncated/empty segment")
        for b in app[pos:pos + size]:
            checksum ^= b
        pos += size
    end = (pos + 16) & ~15
    need(end <= len(app) and app[pos:end - 1] == bytes(end - 1 - pos), "invalid ESP checksum padding")
    need(app[end - 1] == checksum, "ESP app checksum mismatch")
    if app[23]:
        need(app[end:end + 32] == hashlib.sha256(app[:end]).digest(), "ESP app digest mismatch")
        end += 32
    need(end == len(app), "trailing app bytes/signature/foreign partition forbidden")


def verify_container(data, expected_app=None, *, expected_metadata=None):
    need(len(data) > 0xf000, "truncated OTA container")
    need(data[:0xc000] == b"\xff" * 0xc000, "unsigned OTA prefix must be FF, not bootloader/publisher")
    need(data[0xd000:0xf000] == b"\xff" * 0x2000, "OTA partition/otadata gap must be FF")
    info = data[0xc000:0xd000]
    need(info[:8] == bytes.fromhex("7121ce12f0126e04"), "firmware info v4 magic mismatch")
    need(info[8:69] == b"WARP4 Charger" + bytes(61 - len(b"WARP4 Charger")), "WARP4 product name mismatch")
    need(info[77:138] == b"warp4" + bytes(56), "WARP4 product ID mismatch")
    need(info[170:4092] == b"\xff" * (4092 - 170), "unexpected firmware-info fields")
    need(info[4092:] == crc32(info[:4092]).to_bytes(4, "little"), "firmware-info CRC mismatch")
    if expected_metadata is not None:
        need(type(expected_metadata) is dict, "expected metadata object required")
        version = expected_metadata.get('version')
        need(type(version) is list and len(version) == 4 and
             all(type(v) is int and 0 <= v <= 255 or
                 type(v) is str and v.isascii() and v.isdecimal() and 0 <= int(v) <= 255
                 for v in version), "expected metadata version invalid")
        timestamp = expected_metadata.get('build_timestamp')
        need(type(timestamp) is int and 0 <= timestamp <= 0xffffffff,
             "expected metadata timestamp invalid")
        need(expected_metadata.get('firmware_info_product_id') == 'warp4' and
             expected_metadata.get('firmware_info_product_name') == 'WARP4 Charger',
             "expected metadata product mismatch")
        need(info[69:72] == bytes(int(v) for v in version[:3]) and
             info[76] == int(version[3]) and
             int.from_bytes(info[72:76], 'little') == timestamp,
             "firmware metadata version/timestamp mismatch")
    app = data[0xf000:]
    if expected_app is not None:
        need(app == expected_app, "OTA app differs from final build app")
    verify_app(app)
    padded = app + b"\xff" * (-len(app) % 4096)
    need(info[138:170] == hashlib.sha256(padded).digest(), "padded app SHA mismatch")
    return {"product_id": "warp4", "app_bytes": len(app), "app_offset": 0xf000,
            "reserved_slot_bytes": verify_fit(len(app)),
            "ota_sha256": hashlib.sha256(data).hexdigest(),
            "app_sha256": hashlib.sha256(app).hexdigest()}


def verify_official_signature(data, public_key, publisher):
    # libsodium Ed25519ph, same normalization as the existing WARP updater.
    import ctypes
    import ctypes.util
    need(len(public_key) == 32 and len(data) > 0xf000, "public-key/container size")
    block = bytearray(data[0xb000:0xc000])
    need(block[:8] == bytes.fromhex("e6210f21ec121701"), "signature info magic/version")
    expected = publisher.encode()
    need(0 < len(expected) <= 63 and block[8:72] == expected + bytes(64 - len(expected)), "publisher mismatch")
    signature = bytes(block[72:136]); block[72:136] = b"\x55" * 64
    need(block[136:4092] == b"\xff" * (4092 - 136), "signature info reserved bytes")
    need(block[4092:] == crc32(block[:4092]).to_bytes(4, "little"), "signature CRC mismatch")
    normalized = bytearray(data); normalized[0xb048:0xb088] = b"\x55" * 64
    path = ctypes.util.find_library("sodium")
    need(path is not None, "libsodium unavailable")
    lib = ctypes.CDLL(path)
    lib.crypto_sign_statebytes.restype = ctypes.c_size_t
    lib.crypto_sign_update.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulonglong]
    lib.crypto_sign_init.argtypes = [ctypes.c_void_p]
    lib.crypto_sign_final_verify.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    state = ctypes.create_string_buffer(lib.crypto_sign_statebytes())
    need(lib.sodium_init() >= 0 and lib.crypto_sign_init(state) == 0, "libsodium initialization")
    need(lib.crypto_sign_update(state, bytes(normalized), len(normalized)) == 0 and
         lib.crypto_sign_final_verify(state, signature, public_key) == 0, "official Ed25519ph signature invalid")
    # Verify the underlying OTA structure too after removing the publisher block.
    unsigned = bytearray(data); unsigned[0xb000:0xc000] = b"\xff" * 4096
    return verify_container(bytes(unsigned))


if __name__ == "__main__":
    import argparse
    import json
    from pathlib import Path
    import sys
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("container", type=Path)
    parser.add_argument("--app", type=Path)
    args = parser.parse_args()
    try:
        verify_partitions((Path(__file__).resolve().parent / "default_16MB_secure.csv").read_text())
        result = verify_container(args.container.read_bytes(), args.app.read_bytes() if args.app else None)
        print(json.dumps(result, sort_keys=True))
    except (ValueError, OSError) as exc:
        print("OTA VERIFICATION FAILED: " + str(exc), file=sys.stderr)
        raise SystemExit(1)
