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
#include "tesla_ble_validation.h"
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

bool TeslaBleStorageAdapter::select_profile(int index, uint64_t mac)
{
    if (index < 0 || static_cast<unsigned>(index) >= MAX_PROFILES || !mac || mac > 0xFFFFFFFFFFFFULL) { return false; }
    snprintf(profile_prefix, sizeof(profile_prefix), "/config/tesla_p%02d_%012llx_", index, static_cast<unsigned long long>(mac));
    return true;
}

bool TeslaBleStorageAdapter::profiles_config_present()
{
    TeslaBleStorageIO::Paths paths;
    if (!paths.initialize("/config/tesla_vehicle_profiles")) { return true; }
    for (const char *p : {paths.canonical, paths.backup, paths.temporary}) {
        if (TeslaBleStorageIO::presence(p) != TeslaBleStorageIO::Presence::Absent) { return true; }
    }
    if (!paths.initialize("/config/.tesla_vehicle_profiles")) { return true; }
    return TeslaBleStorageIO::presence(paths.canonical) != TeslaBleStorageIO::Presence::Absent;
}

namespace {
// Deliberately NOT a general config restore: restorePersistentConfig can fill
// missing defaults, coerce values and remove saved defaults. Here every field
// must be explicit, unique and exactly the known predecessor's inert token.
// Only VIN may also contain 17 raw ASCII bytes accepted by the production
// validator. It is unused metadata: never imported into an unbound profile.
// Bounded/no allocations; unknown schema, escapes and alternative number forms
// fail closed. JSON member order and the four JSON whitespace bytes may vary.
bool exact_unbound_inert_legacy_config(const uint8_t *data, size_t size)
{
    size_t at = 0;
    const auto whitespace = [&]() {
        while (at < size && (data[at] == ' ' || data[at] == '\t' || data[at] == '\r' || data[at] == '\n')) { ++at; }
    };
    const auto token = [&](const char *text) {
        whitespace();
        const size_t n = strlen(text);
        if (n > size - at || memcmp(data + at, text, n)) { return false; }
        at += n;
        return true;
    };
    const auto vin_value = [&]() {
        if (!token("\"")) { return false; }
        if (at < size && data[at] == '"') { ++at; return true; }
        // No JSON unescaping/coercion. The bounded NUL-terminated copy is
        // validated identically to a new profile; embedded NUL also fails.
        if (size - at < 18 || data[at + 17] != '"') { return false; }
        char vin[18]{};
        memcpy(vin, data + at, 17);
        if (!tesla_ble_valid_vin(vin)) { return false; }
        at += 18;
        return true;
    };
    constexpr const char *names[] = {"\"enable\"", "\"vin\"", "\"ble_address\"", "\"ev_index\"", "\"poll_interval\"", "\"wake_once\""};
    constexpr const char *values[] = {"false", "\"\"", "\"\"", "-1", "300", "false"};
    if (!token("{")) { return false; }
    unsigned seen = 0;
    for (unsigned member = 0; member < 6; ++member) {
        if (member && !token(",")) { return false; }
        unsigned field = 0;
        while (field < 6 && !token(names[field])) { ++field; }
        if (field == 6 || (seen & (1U << field)) || !token(":") ||
            !(field == 1 ? vin_value() : token(values[field]))) { return false; }
        seen |= 1U << field;
    }
    if (seen != 0x3f || !token("}")) { return false; }
    whitespace();
    return at == size;
}

bool read_exact_unbound_inert_legacy_config(const char *path)
{
    TeslaBleStorageIO::File file;
    if (!file.open(path, false)) { return false; }
    uint8_t bytes[512]{};
    size_t size = 0, offset = 0;
    bool ok = file.size(size) && size > 0 && size <= sizeof(bytes);
    while (ok && offset < size) {
        const ssize_t n = file.read(bytes + offset, size - offset);
        if (n <= 0 || static_cast<size_t>(n) > size - offset) { ok = false; break; }
        offset += static_cast<size_t>(n);
    }
    // Reject a file which grew after fstat as well; never ignore trailing bytes.
    uint8_t extra = 0;
    if (ok) { ok = file.read(&extra, 1) == 0; }
    const bool closed = file.close(); // Checked POSIX close, even on parse/IO failure.
    return ok && closed && exact_unbound_inert_legacy_config(bytes, size);
}
}

bool TeslaBleStorageAdapter::legacy_material_present()
{
    // Read-only startup classification. No durable harmless marker, cleanup,
    // key/session import or mutation: an interrupted probe changes no bytes and
    // the next boot must prove the entire absence/unbound-inert predicate again.
    // Only the evidenced tesla_vehicle singleton is eligible. Older aliases,
    // API dot-file transactions and ALL backup/temp variants remain blocking.
    for (const char *path : {"/config/tesla_ble_private_key", "/config/tesla_ble_session_vcsec",
                           "/config/tesla_ble_session_info", "/config/tesla_ble_session_infotainment",
                           "/config/tesla_ble_warp_ev_mac", "/config/tesla_ble_vehicle_vin", "/config/tesla_ble_authorized",
                           "/config/tesla_ble_config", "/config/.tesla_vehicle_config", "/config/.tesla_ble_config",
                           "/tesla_ble_bench_pair_record", "/tesla_ble_bench_pair_exposed"}) {
        TeslaBleStorageIO::Paths paths;
        if (!paths.initialize(path)) { return true; }
        for (const char *p : {paths.canonical, paths.backup, paths.temporary}) {
            if (TeslaBleStorageIO::presence(p) != TeslaBleStorageIO::Presence::Absent) { return true; }
        }
    }
    TeslaBleStorageIO::Paths config;
    if (!config.initialize("/config/tesla_vehicle_config")) { return true; }
    for (const char *p : {config.backup, config.temporary}) {
        if (TeslaBleStorageIO::presence(p) != TeslaBleStorageIO::Presence::Absent) { return true; }
    }
    const auto presence = TeslaBleStorageIO::presence(config.canonical);
    if (presence == TeslaBleStorageIO::Presence::Absent) { return false; }
    return presence != TeslaBleStorageIO::Presence::Present || !read_exact_unbound_inert_legacy_config(config.canonical);
}

const char *TeslaBleStorageAdapter::path_for_key(std::string_view key) const
{
    if (profile_prefix[0]) {
        if (key != "private_key" && key != "session_vcsec" && key != "session_infotainment" &&
            key != "warp_ev_mac" && key != "vehicle_vin" && key != "authorized" && key != "ble_binding") { return nullptr; }
        snprintf(profile_path, sizeof(profile_path), "%s%.*s", profile_prefix, static_cast<int>(key.size()), key.data());
        return profile_path;
    }
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
        for (const char *name : {"private_key", "session_vcsec", "session_infotainment", "warp_ev_mac", "vehicle_vin", "authorized", "ble_binding"}) {
            if (!profile_prefix[0] && (!strcmp(name,"vehicle_vin") || !strcmp(name,"authorized") || !strcmp(name,"ble_binding"))) { continue; }
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
    if (profile_prefix[0]) {
        for (const char *key : {"vehicle_vin", "authorized", "ble_binding"}) { ok = remove_key(key) && ok; }
    }
    for (const char *key : {"private_key", "session_vcsec", "session_infotainment", "warp_ev_mac"}) {
        const bool removed = remove_key(key);
        ok = removed && ok;
    }
    return ok;
}
