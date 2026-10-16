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

def verified(core, sdk):
    try:
        return all(hashlib.sha256((Path(root)/rel).read_bytes()).hexdigest() == digest
                   for root, manifest in ((core, CORE_HASHES), (sdk, SDK_HASHES))
                   for rel, digest in manifest.items())
    except (OSError, TypeError):
        return False
