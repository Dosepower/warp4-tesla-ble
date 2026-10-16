#pragma once
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "bluetooth_littlefs_backend.h"

// Main-task-only, bounded JSON transaction. A .bak is an undo journal and
// ALWAYS wins on boot, including when rollback rename failed. No key paths.
// Checked POSIX close avoids Arduino File::close's void success ambiguity.
// Recoverable I/O contract, not a power-loss/flash durability certification.
#ifndef BLUETOOTH_CONFIG_PATH
#define BLUETOOTH_CONFIG_PATH "/data/config/bluetooth_config"
#endif
class BluetoothConfigStore {
public:
    explicit BluetoothConfigStore(const char *path = BLUETOOTH_CONFIG_PATH) {
        valid = snprintf(canonical, sizeof(canonical), "%s", path) > 0 && strlen(path) < sizeof(canonical) &&
                snprintf(temporary, sizeof(temporary), "%s.tmp", path) > 0 && strlen(path) + 4 < sizeof(temporary) &&
                snprintf(backup, sizeof(backup), "%s.bak", path) > 0 && strlen(path) + 4 < sizeof(backup);
    }
    bool load(bool &enabled) {
        enabled = false;
        Snapshot snapshot;
        if (!preflight(snapshot, false)) { return false; }
        return snapshot.exists[2] ? read(backup, enabled) : snapshot.exists[0] ? read(canonical, enabled) : true;
    }
    bool save(bool enabled) {
        Snapshot snapshot;
        // ALL leaves and ancestors, including stale temporary files, must be
        // safe before recovery, unlink, create, truncate, rename or journal IO.
        if (!preflight(snapshot, true)) { return false; }
        bool previous = false;
        if (!load(previous)) { return false; }
        // Recover an interrupted/failed transaction before any new write.
        if (snapshot.exists[2] && !move(backup, canonical)) { return false; }
        if (snapshot.exists[1] && !erase(temporary)) { return false; }
        if (!snapshot.exists[0] && !snapshot.exists[2]) {
            // Materialize the default OFF as rollback authority for first enable.
            if (!write(temporary, false) || !move(temporary, canonical)) { return false; }
        }
        if (!write(temporary, enabled)) { return false; }
        if (!move(canonical, backup)) { return false; }
        if (!move(temporary, canonical)) {
            move(backup, canonical); // If refused, boot recovers .bak.
            return false;
        }
        bool verified = !enabled;
        if (!read(canonical, verified) || verified != enabled || !erase(backup)) {
            // A failed close quarantines IO but still permits checked rollback
            // metadata. If rollback fails, the complete backup wins on boot.
            move(backup, canonical, true);
            return false;
        }
        return true; // Only after sync, checked closes, readback and journal removal.
    }
private:
    char canonical[192]{}, temporary[192]{}, backup[192]{};
    bool valid = false, quarantined = false;
    struct Snapshot { struct stat leaves[3]{}; bool exists[3]{}; };
#if BLUETOOTH_CONFIG_TARGET_VFS_ADAPTER
    bool target_preflight(Snapshot &snapshot, bool rollback) {
        if (!valid || (quarantined && !rollback) || !BluetoothLittleFSBackend::mounted() ||
            strncmp(canonical, "/data/", 6) != 0) { return false; }
        using Entry = BluetoothLittleFSBackend::Entry;
        // A checked descriptor close failure permits metadata-only rollback.
        // Each new directory lookup must itself close successfully.
        bool directory_quarantine = false;
        char parent[sizeof(canonical)]; strcpy(parent, canonical);
        for (char *part = parent + 6; *part;) {
            char *end = strchr(part, '/');
            const size_t length = end ? static_cast<size_t>(end - part) : strlen(part);
            if (!length || (length == 1 && part[0] == '.') ||
                (length == 2 && part[0] == '.' && part[1] == '.')) { return false; }
            if (!end) { break; }
            *end = '\0';
            const Entry entry = BluetoothLittleFSBackend::lookup(parent, directory_quarantine);
            *end = '/';
            if (entry != Entry::Directory) { quarantined |= directory_quarantine; return false; }
            part = end + 1;
            if (!*part) { return false; }
        }
        for (unsigned i = 0; i < 3; ++i) {
            const Entry entry = BluetoothLittleFSBackend::lookup(leaf(i), directory_quarantine);
            if (entry != Entry::Regular && entry != Entry::Missing) {
                quarantined |= directory_quarantine; return false;
            }
            snapshot.exists[i] = entry == Entry::Regular;
        }
        return true;
    }
#endif
    static int nofollow_metadata(const char *path, struct stat *info) {
#ifdef ESP_PLATFORM
        // Never use following stat as boot authority on ESP. Only the pinned
        // directory-entry adapter above may classify target paths.
        (void)path; (void)info; errno = ENOTSUP; return -1;
#else
        return ::lstat(path, info);
#endif
    }
    const char *leaf(unsigned i) const { return i == 0 ? canonical : i == 1 ? temporary : backup; }
    static bool same(const struct stat &a, const struct stat &b) {
        return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_mode == b.st_mode &&
               S_ISREG(a.st_mode) && a.st_nlink == 1 && b.st_nlink == 1;
    }
    bool preflight(Snapshot &snapshot, bool require_parents, bool rollback = false) {
#ifdef ESP_PLATFORM
        (void)require_parents;
#if BLUETOOTH_CONFIG_TARGET_VFS_ADAPTER
        return target_preflight(snapshot, rollback);
#else
        (void)snapshot; (void)rollback; return false;
#endif
#else
        if (!valid || (quarantined && !rollback) || canonical[0] != '/') { return false; }
        char parent[sizeof(canonical)];
        strcpy(parent, canonical);
        struct stat info{};
        if (nofollow_metadata("/", &info) != 0 || !S_ISDIR(info.st_mode)) { return false; }
        // Lexical ancestry only: never normalize through a symlink or '..'.
        for (char *part = parent + 1; *part;) {
            char *end = strchr(part, '/');
            const size_t length = end ? static_cast<size_t>(end - part) : strlen(part);
            if (length == 0 || (length == 1 && part[0] == '.') ||
                (length == 2 && part[0] == '.' && part[1] == '.')) { return false; }
            if (!end) { break; }
            *end = '\0';
            const int rc = nofollow_metadata(parent, &info);
            *end = '/';
            if (rc != 0) { return errno == ENOENT && !require_parents; }
            if (!S_ISDIR(info.st_mode)) { return false; }
            part = end + 1;
            if (!*part) { return false; }
        }
        for (unsigned i = 0; i < 3; ++i) {
            if (nofollow_metadata(leaf(i), &snapshot.leaves[i]) != 0) {
                if (errno != ENOENT) { return false; }
                continue;
            }
            snapshot.exists[i] = true;
            const auto &st = snapshot.leaves[i];
            if (!S_ISREG(st.st_mode) || st.st_nlink != 1) { return false; }
            for (unsigned j = 0; j < i; ++j) {
                if (snapshot.exists[j] && st.st_dev == snapshot.leaves[j].st_dev &&
                    st.st_ino == snapshot.leaves[j].st_ino) { return false; }
            }
        }
        return true;
#endif
    }
    bool move(const char *from, const char *to, bool rollback = false) {
        Snapshot snapshot;
        if (!preflight(snapshot, true, rollback)) { return false; }
        return ::rename(from, to) == 0;
    }
    bool erase(const char *path) {
        Snapshot snapshot;
        if (!preflight(snapshot, true)) { return false; }
        return ::unlink(path) == 0;
    }
    bool close(int fd) {
        if (::close(fd) == 0) { return true; }
        quarantined = true; // Never retry a consumed IDF FD or promise a commit.
        return false;
    }
    bool read(const char *path, bool &enabled) {
#ifdef ESP_PLATFORM
        Snapshot snapshot;
        const unsigned index = path == backup ? 2 : path == temporary ? 1 : 0;
        if (!preflight(snapshot, false) || !snapshot.exists[index]) { return false; }
        // LittleFS has no link primitive; the target preflight establishes the
        // no-symlink/no-alias boundary before this VFS descriptor is opened.
        const int fd = ::open(path, O_RDONLY);
        if (fd < 0) { return false; }
        struct stat st{};
        bool ok = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
                  (st.st_size == 15 || st.st_size == 16);
        char data[16]{}; size_t size = 0;
        while (ok && size < static_cast<size_t>(st.st_size)) {
            const ssize_t count = ::read(fd, data + size, static_cast<size_t>(st.st_size) - size);
            if (count < 0 && errno == EINTR) { continue; }
            if (count <= 0) { ok = false; break; }
            size += static_cast<size_t>(count);
        }
        char extra{};
        ssize_t eof;
        do { eof = ok ? ::read(fd, &extra, 1) : -1; } while (ok && eof < 0 && errno == EINTR);
        struct stat after{};
        ok = ok && eof == 0 && ::fstat(fd, &after) == 0 && S_ISREG(after.st_mode) && st.st_size == after.st_size;
        const bool closed = close(fd);
        if (!ok || eof != 0 || !closed) { return false; }
        if (size == 15 && memcmp(data, "{\"enable\":true}", 15) == 0) { enabled = true; return true; }
        if (size == 16 && memcmp(data, "{\"enable\":false}", 16) == 0) { enabled = false; return true; }
        return false;
#else
#ifndef O_NOFOLLOW
        (void)path; (void)enabled;
        return false; // An SDK without no-follow opens cannot confirm this contract.
#else
        Snapshot snapshot;
        const unsigned index = path == backup ? 2 : path == temporary ? 1 : 0;
        if (!preflight(snapshot, false) || !snapshot.exists[index]) { return false; }
        const int fd = ::open(path, O_RDONLY | O_NOFOLLOW);
        if (fd < 0) { return false; }
        struct stat st{};
        bool ok = ::fstat(fd, &st) == 0 && same(st, snapshot.leaves[index]) &&
                  (st.st_size == 15 || st.st_size == 16);
        char data[16]{}; size_t size = 0;
        while (ok && size < static_cast<size_t>(st.st_size)) {
            const ssize_t count = ::read(fd, data + size, static_cast<size_t>(st.st_size) - size);
            if (count < 0 && errno == EINTR) { continue; }
            if (count <= 0) { ok = false; break; }
            size += static_cast<size_t>(count);
        }
        char extra;
        ssize_t eof;
        do { eof = ok ? ::read(fd, &extra, 1) : -1; } while (eof < 0 && errno == EINTR && ok);
        struct stat after{};
        ok = ok && eof == 0 && ::fstat(fd, &after) == 0 && same(st, after) && st.st_size == after.st_size;
        const bool closed = close(fd);
        if (!ok || !closed) { return false; }
        if (size == 15 && memcmp(data, "{\"enable\":true}", 15) == 0) { enabled = true; return true; }
        if (size == 16 && memcmp(data, "{\"enable\":false}", 16) == 0) { enabled = false; return true; }
        return false;
#endif
#endif
    }
    bool write(const char *path, bool enabled) {
#ifdef ESP_PLATFORM
        Snapshot snapshot;
        if (!preflight(snapshot, true)) { return false; }
        const char *data = enabled ? "{\"enable\":true}" : "{\"enable\":false}";
        const size_t size = strlen(data);
        // Only the attested no-link backend reaches here. Check before create;
        // truncate only after the returned descriptor passes fstat.
        const int fd = ::open(path, O_WRONLY | (snapshot.exists[1] ? 0 : O_CREAT | O_EXCL), 0600);
        if (fd < 0) { return false; }
        struct stat st{};
        bool safe = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
        bool written = safe && ::ftruncate(fd, 0) == 0;
        size_t offset = 0;
        while (written && offset < size) {
            const ssize_t count = ::write(fd, data + offset, size - offset);
            if (count < 0 && errno == EINTR) { continue; }
            if (count <= 0) { written = false; break; }
            offset += static_cast<size_t>(count);
        }
        const bool synced = written && offset == size && ::fsync(fd) == 0;
        const bool closed = close(fd);
        bool verified = !enabled;
        return synced && closed && read(path, verified) && verified == enabled;
#else
#ifndef O_NOFOLLOW
        (void)path; (void)enabled;
        return false;
#else
        Snapshot snapshot;
        if (!preflight(snapshot, true)) { return false; }
        const char *data = enabled ? "{\"enable\":true}" : "{\"enable\":false}";
        const size_t size = strlen(data);
        // Never truncate at open: validate the descriptor and complete pathset
        // first. O_EXCL also protects an absent leaf substituted before create.
        const int fd = ::open(path, O_WRONLY | O_NOFOLLOW | (snapshot.exists[1] ? 0 : O_CREAT | O_EXCL), 0600);
        if (fd < 0) { return false; }
        struct stat st{}; Snapshot current;
        const bool safe = ::fstat(fd, &st) == 0 && preflight(current, true) && current.exists[1] &&
            same(st, current.leaves[1]) && (!snapshot.exists[1] || same(st, snapshot.leaves[1]));
        const bool written = safe && ::ftruncate(fd, 0) == 0 && ::write(fd, data, size) == static_cast<ssize_t>(size);
        const bool synced = written && ::fsync(fd) == 0;
        const bool closed = close(fd);
        bool verified = !enabled;
        return synced && closed && read(path, verified) && verified == enabled;
#endif
#endif
    }
};
