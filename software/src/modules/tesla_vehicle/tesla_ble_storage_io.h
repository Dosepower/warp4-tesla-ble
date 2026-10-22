#pragma once
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>

// Single-worker synchronous file contract. The target branch owns a descriptor,
// never an Arduino String/File/shared_ptr. Host File bridging keeps all legacy
// fault tests, but is not used as evidence for target allocation behavior.
namespace TeslaBleStorageIO {
// No supported per-FD cleanup API exists after esp_littlefs 1.22.3 failed close.
// Never unmount shared /data or inspect private ABI. Quarantine Tesla storage
// for this boot; the orphan remains until reboot. This is NOT leak-free cleanup.
inline std::atomic<bool> close_quarantine{false};
inline bool restart_required() { return close_quarantine.load(std::memory_order_acquire); }
inline bool checked_close(int fd) {
    const bool ok = ::close(fd) == 0;
#if defined(ESP_PLATFORM) || defined(TESLA_BLE_CLOSE_QUARANTINE_TEST)
    if (!ok) { close_quarantine.store(true, std::memory_order_release); }
#endif
    return ok;
}
enum class Presence { Absent, Present, Error };
struct Paths {
    char canonical[192]{}, backup[192]{}, temporary[192]{};
    bool initialize(const char *path) {
#ifdef TESLA_CLEAN_STORAGE_PREFIX
        const char *prefix = TESLA_CLEAN_STORAGE_PREFIX;
#elif defined(TESLA_BLE_HOST_FILESYSTEM)
        const char *prefix = "";
#else
        const char *prefix = "/data";
#endif
        if (!path) { return false; }
        const int n = snprintf(canonical, sizeof(canonical), "%s%s", prefix, path);
        const int b = snprintf(backup, sizeof(backup), "%s%s.bak", prefix, path);
        const int t = snprintf(temporary, sizeof(temporary), "%s%s.tmp", prefix, path);
        return n > 0 && static_cast<size_t>(n) < sizeof(canonical) && b > 0 && static_cast<size_t>(b) < sizeof(backup) &&
               t > 0 && static_cast<size_t>(t) < sizeof(temporary);
    }
};
inline Presence presence(const char *path) {
    if (restart_required()) { return Presence::Error; }
#ifdef TESLA_BLE_HOST_FILESYSTEM
    return LittleFS.exists(path) ? Presence::Present : Presence::Absent;
#else
    struct stat st{};
    if (::stat(path, &st) == 0) { return S_ISREG(st.st_mode) ? Presence::Present : Presence::Error; }
    return errno == ENOENT ? Presence::Absent : Presence::Error;
#endif
}
inline bool remove(const char *path) {
    if (restart_required()) { return false; }
#ifdef TESLA_BLE_HOST_FILESYSTEM
    return LittleFS.remove(path);
#else
    return ::unlink(path) == 0;
#endif
}
inline bool rename(const char *from, const char *to) {
    if (restart_required()) { return false; }
#ifdef TESLA_BLE_HOST_FILESYSTEM
    return LittleFS.rename(from, to);
#else
    return ::rename(from, to) == 0;
#endif
}
class File {
public:
    File() = default;
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    ~File() { close(); }
    bool open(const char *path, bool write) {
        if (restart_required()) { return false; }
#ifdef TESLA_BLE_HOST_FILESYSTEM
        file = LittleFS.open(path, write ? "w" : "r");
        return static_cast<bool>(file) && !file.isDirectory();
#else
        fd = write ? ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600) : ::open(path, O_RDONLY);
        return fd >= 0;
#endif
    }
    bool size(size_t &size) {
#ifdef TESLA_BLE_HOST_FILESYSTEM
        if (!file) { return false; } size = file.size(); return true;
#else
        struct stat st{};
        if (fd < 0 || ::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) { return false; }
        size = static_cast<size_t>(st.st_size); return true;
#endif
    }
    ssize_t read(uint8_t *data, size_t count) {
#ifdef TESLA_BLE_HOST_FILESYSTEM
        return file.read(data, count);
#else
        ssize_t result; do { result = ::read(fd, data, count); } while (result < 0 && errno == EINTR); return result;
#endif
    }
    ssize_t write(const uint8_t *data, size_t count) {
#ifdef TESLA_BLE_HOST_FILESYSTEM
        return file.write(data, count);
#else
        ssize_t result; do { result = ::write(fd, data, count); } while (result < 0 && errno == EINTR); return result;
#endif
    }
    bool sync() {
#ifdef TESLA_BLE_HOST_FILESYSTEM
        file.flush(); return file.getWriteError() == 0;
#else
        return fd >= 0 && ::fsync(fd) == 0;
#endif
    }
    bool close() {
#ifdef TESLA_BLE_HOST_FILESYSTEM
        file.close(); return true; // Void host API; reopen/verify catches write-close data loss.
#else
        if (fd < 0) { return true; }
        const int old = fd; fd = -1;
        // Exactly once: IDF consumes public FD on error; LittleFS detaches file
        // but esp_littlefs retains its wrapper. Latch rather than pretend cleanup.
        return checked_close(old);
#endif
    }
private:
#ifdef TESLA_BLE_HOST_FILESYSTEM
    ::File file;
#else
    int fd = -1;
#endif
};
}
