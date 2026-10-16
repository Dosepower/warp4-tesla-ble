#pragma once
// Set ONLY by r13_vfs_guard.py after hashing the bound Arduino adapter and
// SDK LittleFS/VFS artifacts. Unknown/changed packages never inherit ESP support.
#if defined(ESP_PLATFORM) && defined(WARP_BLUETOOTH_LITTLEFS_BINDING) && WARP_BLUETOOTH_LITTLEFS_BINDING == 0x52313302
#define BLUETOOTH_CONFIG_TARGET_VFS_ADAPTER 1
#else
#define BLUETOOTH_CONFIG_TARGET_VFS_ADAPTER 0
#endif
