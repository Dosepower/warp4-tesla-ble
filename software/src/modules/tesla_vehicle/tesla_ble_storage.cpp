#include "tesla_ble_storage.h"

#ifdef TESLA_BLE_HOST_FILESYSTEM
#include <LittleFS.h>
#endif
#include <algorithm>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tesla_ble_key_validation.h"
#include "tesla_ble_storage_io.h"
#include <mbedtls/platform_util.h>

bool TeslaBleStorageAdapter::initialize_identity(tesla_clean::IdentityStore &keys, tesla_clean::Random random, bool allow_generate)
{
    uint8_t pem[512]{};
    size_t size = 0;
    const auto result = load_into("private_key", pem, sizeof(pem), size);
    bool ok = false;
    if (result == LoadResult::Loaded) {
        ok = keys.import_pem({pem, size}, random);
    } else if (result == LoadResult::Absent) {
        // Rotation publishes no identity until verified persistence commits.
        ok = !allow_generate || keys.rotate(*this, random).committed;
    }
    mbedtls_platform_zeroize(pem, sizeof(pem));
    return ok && !keys.quarantined() && !restart_required();
}

bool TeslaBleStorageAdapter::restart_required() const { return TeslaBleStorageIO::restart_required(); }

const char *TeslaBleStorageAdapter::path_for_key(std::string_view key)
{
    if (key == "private_key") {
        return "/config/tesla_ble_private_key";
    }
    if (key == "session_vcsec") {
        return "/config/tesla_ble_session_vcsec";
    }
    if (key == "session_infotainment") {
        return "/config/tesla_ble_session_info";
    }
    if (key == "warp_ev_mac") {
        return "/config/tesla_ble_warp_ev_mac";
    }
    return nullptr;
}

TeslaBleStorageAdapter::LoadResult TeslaBleStorageAdapter::load_into(const char *key, uint8_t *buffer,
                                                                      size_t capacity, size_t &size)
{
    size = 0;
    if (buffer) { std::memset(buffer, 0, capacity); }
    if (restart_required()) { return LoadResult::RestartRequired; }
    if (!key) { return LoadResult::Error; }
    const char *path = path_for_key(key);
    if (!path || !buffer || capacity == 0) { return LoadResult::Error; }
#ifdef TESLA_BLE_HOST_FILESYSTEM
    // Host filesystem boundary only; protocol/load policy remains production.
    return tesla_ble_host_read_into(path, buffer, capacity, size);
#else
    // Arduino File/VFSFileImpl use shared_ptr factories. POSIX descriptors
    // avoid those throwing control-block allocations during startup/probes.
    // The data partition is mounted at /data by tools/fs.cpp.
    TeslaBleStorageIO::Paths paths;
    if (!paths.initialize(path)) { return LoadResult::Error; }
    int fd = ::open(paths.canonical, O_RDONLY);
    if (fd < 0 && errno == ENOENT) {
        fd = ::open(paths.backup, O_RDONLY);
    }
    if (fd < 0) { return errno == ENOENT ? LoadResult::Absent : LoadResult::Error; }
    struct stat info{};
    bool ok = ::fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 &&
              static_cast<size_t>(info.st_size) <= capacity;
    while (ok && size < static_cast<size_t>(info.st_size)) {
        const ssize_t count = ::read(fd, buffer + size, static_cast<size_t>(info.st_size) - size);
        if (count < 0 && errno == EINTR) { continue; }
        if (count <= 0) { ok = false; break; }
        size += static_cast<size_t>(count);
    }
    if (!TeslaBleStorageIO::checked_close(fd)) { ok = false; }
    if (!ok) { size = 0; std::memset(buffer, 0, capacity); }
    return ok ? LoadResult::Loaded : LoadResult::Error;
#endif
}

TeslaBleStorageAdapter::SaveResult TeslaBleStorageAdapter::save_transaction(const char *key, const uint8_t *buffer, size_t size)
{
    if (restart_required()) { return SaveResult::RestartRequired; }
    if (!key || !buffer) { return SaveResult::NotCommitted; }
    TeslaBleStorageIO::Paths paths;
    if (!paths.initialize(path_for_key(key)) || size == 0 || size > 4096) { return SaveResult::NotCommitted; }
    TeslaBleStorageIO::File file;
    // Even a refused open/cancel can encounter a stale temp from an earlier
    // attempt. Conservatively expose cleanup pending without a fallible probe.
    const auto pending = SaveResult::NotCommittedCleanupPending;
    // Close before deciding failure, even on error/cancellation. Destructor-only
    // failed close must not conceal a restart-required outcome.
    const auto fail = [&]() {
        file.close();
        return restart_required() ? SaveResult::RestartRequired : pending;
    };
    if (!may_mutate() || !file.open(paths.temporary, true)) { return fail(); }
    size_t written = 0;
    while (written < size) {
        if (!may_mutate()) { return fail(); }
        const ssize_t count = file.write(buffer + written, size - written);
        if (count <= 0 || static_cast<size_t>(count) > size - written) { return fail(); }
        written += static_cast<size_t>(count);
    }
    if (!may_mutate() || !file.sync()) { return fail(); }
    if (!file.close()) { return fail(); }

    // Verify all bytes and the checked read-close before touching committed paths.
    // This is a recoverable transaction, not a claim of power-loss atomicity.
    if (!file.open(paths.temporary, false)) { return fail(); }
    size_t verified_size = 0;
    if (!file.size(verified_size) || verified_size != size) { return fail(); }
    uint8_t chunk[32];
    for (size_t offset = 0; offset < size; offset += sizeof(chunk)) {
        const size_t count = std::min(sizeof(chunk), size - offset);
        if (file.read(chunk, count) != static_cast<ssize_t>(count) || std::memcmp(chunk, buffer + offset, count)) { return fail(); }
    }
    if (!file.close() || !may_mutate()) { return fail(); }

    using Presence = TeslaBleStorageIO::Presence;
    const auto canonical = TeslaBleStorageIO::presence(paths.canonical);
    const auto backup = TeslaBleStorageIO::presence(paths.backup);
    if (canonical == Presence::Error || backup == Presence::Error) { return pending; }
    // Never delete the only committed copy; backup-only is a valid commit.
    if (canonical == Presence::Present && backup == Presence::Present && !TeslaBleStorageIO::remove(paths.backup)) { return pending; }
    if (!may_mutate()) { return pending; }
    if (canonical == Presence::Present && !TeslaBleStorageIO::rename(paths.canonical, paths.backup)) { return pending; }
    if (!may_mutate() || !TeslaBleStorageIO::rename(paths.temporary, paths.canonical)) { return pending; }
    // COMMITTED: new canonical verified/synced/closed before promotion. No
    // fallible rollback or reparse is allowed after publishing durable identity.
    if (!may_mutate()) { return SaveResult::CommittedCleanupPending; }
    // No post-commit fallible presence probe: we already know whether old material
    // exists. Cleanup refusal retains that complete backup, but is a COMMIT.
    if ((canonical == Presence::Present || backup == Presence::Present) && !TeslaBleStorageIO::remove(paths.backup)) { return SaveResult::CommittedCleanupPending; }
    return SaveResult::Committed;
}


bool TeslaBleStorageAdapter::remove_key(const char *key)
{
    if (restart_required()) { return false; }
    if (!key) { return false; }
    TeslaBleStorageIO::Paths paths;
    if (!paths.initialize(path_for_key(key))) { return false; }
    bool ok = true;
    for (const char *entry : {paths.canonical, paths.backup, paths.temporary}) {
        const auto presence = TeslaBleStorageIO::presence(entry);
        const bool removed = presence == TeslaBleStorageIO::Presence::Absent ||
            (presence == TeslaBleStorageIO::Presence::Present && may_mutate() && TeslaBleStorageIO::remove(entry));
        ok = removed && ok; // Always attempt every variant, even after failure.
    }
    return ok;
}

TeslaBleStorageAdapter::KeyPresence TeslaBleStorageAdapter::private_key_presence()
{
    uint8_t key[4096]{};
    size_t size = 0;
    const auto loaded = load_into("private_key", key, sizeof(key), size);
    KeyPresence presence = KeyPresence::Unknown;
    if (loaded == LoadResult::Absent) {
        // No committed identity: orphaned session/binding files are not usable
        // either. Keep deletion offered after partial erasure and restart.
        presence = KeyPresence::Absent;
        for (const char *name : {"private_key", "session_vcsec", "session_infotainment", "warp_ev_mac"}) {
            TeslaBleStorageIO::Paths paths;
            if (!paths.initialize(path_for_key(name))) { presence = KeyPresence::Unknown; break; }
            for (const char *file : {paths.canonical, paths.backup, paths.temporary}) {
                const auto material = TeslaBleStorageIO::presence(file);
                if (material == TeslaBleStorageIO::Presence::Error) { presence = KeyPresence::Unknown; break; }
                if (material == TeslaBleStorageIO::Presence::Present) { presence = KeyPresence::CleanupRequired; }
            }
            if (presence == KeyPresence::Unknown) { break; }
        }
    }
    if (loaded == LoadResult::Loaded &&
        tesla_ble_valid_private_key(std::string_view(reinterpret_cast<const char *>(key), size))) { presence = KeyPresence::Present; }
    volatile uint8_t *wipe = key;
    for (size_t i = 0; i < sizeof(key); ++i) { wipe[i] = 0; }
    return presence;
}

bool TeslaBleStorageAdapter::has_private_key()
{
    return private_key_presence() == KeyPresence::Present;
}

bool TeslaBleStorageAdapter::forget_material()
{
    bool ok = true;
    for (const char *key : {"private_key", "session_vcsec", "session_infotainment", "warp_ev_mac"}) {
        const bool removed = remove_key(key);
        ok = removed && ok;
    }
    return ok;
}
