#pragma once
#include "bluetooth_littlefs_binding.h"
#if BLUETOOTH_CONFIG_TARGET_VFS_ADAPTER
#include <LittleFS.h>
#include <esp_littlefs.h>
#include <dirent.h>
#include <cerrno>
#include <cstring>
#include <type_traits>

// This is the production adapter seam. Tests replace only the bound mount
// API and POSIX calls, never this lookup algorithm or Store transaction.
// No Arduino File object: its void close discards fclose/closedir status.
struct BluetoothLittleFSBackend {
    enum class Entry { Missing, Regular, Directory, Error };
    static_assert(std::is_same<decltype(LittleFS), fs::LittleFSFS>::value,
                  "Unexpected Arduino LittleFS object type");
    static bool mounted() {
        const char *mount = LittleFS.mountpoint();
        return mount && strcmp(mount, "/data") == 0 &&
               (esp_littlefs_mounted("data") || esp_littlefs_mounted("spiffs"));
    }
    static Entry lookup(const char *path, bool &quarantined) {
        if (quarantined || !mounted() || strncmp(path, "/data/", 6) != 0) { return Entry::Error; }
        char parent[192];
        if (strlen(path) >= sizeof(parent)) { return Entry::Error; }
        strcpy(parent, path);
        char *last = strrchr(parent, '/');
        if (!last || !last[1]) { return Entry::Error; }
        *last = '\0';
        const char *name = last + 1;
        DIR *dir = ::opendir(parent);
        if (!dir) { return Entry::Error; } // ENOENT ancestor is NOT a missing leaf.
        Entry found = Entry::Missing;
        while (true) {
            errno = 0;
            const struct dirent *entry = ::readdir(dir);
            if (!entry) { if (errno != 0) { found = Entry::Error; } break; }
            if (strcmp(entry->d_name, name) != 0) { continue; }
            if (found != Entry::Missing) { found = Entry::Error; break; }
            found = entry->d_type == DT_REG ? Entry::Regular :
                    entry->d_type == DT_DIR ? Entry::Directory : Entry::Error;
            if (found == Entry::Error) { break; }
        }
        // Raw closedir, unlike File.close(), propagates the pinned backend's
        // lfs_dir_close result. Never retry a failed/consumed handle.
        if (::closedir(dir) != 0) { quarantined = true; return Entry::Error; }
        return found;
    }
};
#endif
