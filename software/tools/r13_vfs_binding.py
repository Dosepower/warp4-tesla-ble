"""Exact read-only attestation for the R13 LittleFS/VFS adapter.

No package name, ESP_PLATFORM flag, mountpoint, or version string is proof.
Unknown bytes leave production support disabled. Rebuilt BLE SDKs need separate
review of these exact artifacts; this does not waive the target build gate.
"""
import hashlib
from pathlib import Path

BINDING = 0x52313302
CORE_HASHES = {
    'libraries/FS/src/FS.h': 'b0f42cd30763dbcd70d1a465f407a8b257749459b8169f603d92df4a8ef9100f',
    'libraries/FS/src/FS.cpp': 'b031b23337d69cac448e7e19184be751428aeb1c282caf3cdc8dfd604b36dae2',
    'libraries/FS/src/vfs_api.h': '9d1c70f896bd68a7336792a8456c2ef567a66bf26c09ebdcc448380be8dc9f7f',
    'libraries/FS/src/vfs_api.cpp': '3d345bcb3b764f08c6e923dbacc96f27da10edb12beb774de439228057d14a54',
    'libraries/LittleFS/src/LittleFS.h': '394d3cf42f869417e247d26c6dff7cff59a26d4393d7a183a801fc6c949f18ae',
    'libraries/LittleFS/src/LittleFS.cpp': '2665e4835f014cdfb62642110ece47fc79037f540d630b8eb3bc2ac2b48b3164',
}
SDK_HASHES = {
    'esp32/include/joltwallet__littlefs/include/esp_littlefs.h': '38d3580e400d21751181b98a302269e40da9fdc1755a238e1f6af9d30005283a',
    'esp32/lib/libjoltwallet__littlefs.a': '80ea5b9cfea216b38e98aaa72a94cecfb15ded23c84b487bf92c8b230560f892',
    'esp32/lib/libvfs.a': 'de9610d0fba2a06ae57a6e6925430092f06daf18b8136ef9ba58420cf8938e00',
    'esp32/qio_qspi/include/sdkconfig.h': '434a3dc4bba9d12fa3705ae8fe600e94b2a91e90ebf52b2f39307ed4d4d1d801',
}

# Pre-V4 BLE-capable normal-profile export. Same reviewed FS/VFS adapter and
# ordinary LittleFS sync/rename contract; exact archive/config identity only.
# Does not attest secure boot, power-loss resilience or target runtime headroom.
LEAN_SDK_HASHES = {
    'esp32/include/joltwallet__littlefs/include/esp_littlefs.h': '38d3580e400d21751181b98a302269e40da9fdc1755a238e1f6af9d30005283a',
    'esp32/lib/libjoltwallet__littlefs.a': 'c8e5cb318c837116de55709a95c860c2e26be3787964822456bd901c8b22a9ff',
    'esp32/lib/libvfs.a': '378e982d5ddc3ed0d8c8f29db3ad0487a099a91941ec590a79723de0f97287ef',
    'esp32/qio_qspi/include/sdkconfig.h': '3cbcb40e1ba079c286d6dbdd7ab2077cdbda9f7e3a2f2216e667b929e17d19b1',
}

def verified(core, sdk):
    try:
        core_ok = all(hashlib.sha256((Path(core)/rel).read_bytes()).hexdigest() == digest
                      for rel, digest in CORE_HASHES.items())
        return core_ok and any(all(hashlib.sha256((Path(sdk)/rel).read_bytes()).hexdigest() == digest
                                  for rel, digest in manifest.items())
                               for manifest in (SDK_HASHES, LEAN_SDK_HASHES))
    except (OSError, TypeError):
        return False

# Separate recoverable-I/O attestation, never production approval. The ordinary
# The canonical test SDK uses LittleFS prog_size=128 (production=16). Close/sync/rename/directory
# semantics were reviewed against its exact source-bound archives. Native
# backend tests exercise 128-byte programming; this is NOT power-loss evidence.
BENCH_BINDING = 0x523133B1
BENCH_SDK_HASHES = {
    'esp32/include/joltwallet__littlefs/include/esp_littlefs.h': '38d3580e400d21751181b98a302269e40da9fdc1755a238e1f6af9d30005283a',
    'esp32/lib/libjoltwallet__littlefs.a': 'f24fdf7d18f647595c91223191841e61653fe7865d0f82b92966a57ebf573481',
    'esp32/lib/libvfs.a': '609724a637cfd62e282e6c2cf65c9b60ff64d583c06f5fff352c76e3e5accc56',
    'esp32/qio_qspi/include/sdkconfig.h': 'a4fc3ad52bbfc6f41610050154511deaa1a6bf79c378f7d1d3809cca4c680231',
}

def verified_bench(core, sdk):
    try:
        return all(hashlib.sha256((Path(root)/rel).read_bytes()).hexdigest() == digest
                   for root, manifest in ((core, CORE_HASHES), (sdk, BENCH_SDK_HASHES))
                   for rel, digest in manifest.items())
    except (OSError, TypeError):
        return False
