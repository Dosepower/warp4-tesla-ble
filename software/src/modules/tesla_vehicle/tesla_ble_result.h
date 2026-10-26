#pragma once

#include <cmath>
#include <cstdint>
#include "tesla_ble_clean_platform.h"
#include <atomic>
#include <optional>

struct TeslaBleIdentity {
    uint64_t generation = 0;
    int index = -1;
    uint64_t mac = 0;
    bool operator==(const TeslaBleIdentity &other) const
    {
        return generation == other.generation && index == other.index && mac == other.mac;
    }
    bool operator!=(const TeslaBleIdentity &other) const { return !(*this == other); }
};

struct TeslaBleRequestIdentity {
    uint64_t epoch = 0;
    TeslaBleIdentity vehicle;
    uint64_t connection = 0;
    uint64_t request = 0;
};

// Final consumption boundary, shared by firmware and executable host tests.
inline bool tesla_ble_result_current(const TeslaBleRequestIdentity &result,
                                     const TeslaBleRequestIdentity &current,
                                     uint64_t bound_mac, int configured_index,
                                     bool connected, bool authenticated, bool correlated, float soc)
{
    return authenticated && correlated && result.epoch == current.epoch &&
           result.vehicle == current.vehicle &&
           result.vehicle.mac != 0 && result.vehicle.mac == bound_mac &&
           connected && result.connection == current.connection &&
           result.request != 0 && result.request == current.request &&
           result.vehicle.index == configured_index &&
           std::isfinite(soc) && soc >= 0.0f && soc <= 100.0f;
}

// Main and Tesla worker task writers are serialized by a scalar-publication
// critical section (native mutex on host). Never publish from a callback/ISR.
// Only the short scalar publication is locked;
// readers remain lock-free and bounded. Atomic fields avoid C++ seqlock data
// races; sequential consistency establishes one total order.
// Split the 64-bit lifecycle identity into aligned words. The bounded-read
// coherence tag is not a lifecycle ID; its odd/even wrap does not reset identity.
// No 64-bit atomics (ESP32 cannot guarantee they are lock-free).
class TeslaBleIdentitySnapshot
{
public:
    bool synchronization_initialized() const { return writer_mutex.initialized(); }
    __attribute__((noinline)) void publish(TeslaBleIdentity identity)
    {
        tesla_warp::ScalarPublicationMutex::Guard lock(writer_mutex);
        if (!lock) { return; }
        // Main (every loop) and worker (every pass) republish the unchanged
        // live identity. Each write opens an odd-version window in which a
        // concurrent reader reports "unavailable", and the worker's storage
        // mutation guard then refuses an in-progress pairing transaction.
        // Republishing equal bytes changes nothing: leave the coherence tag
        // untouched, so readers fail closed only during a genuine change.
        // Scalar writer copy (no address of the argument is taken), so the
        // caller frames of this noinline function stay as before.
        if (writer_published && writer_generation == identity.generation &&
            writer_index == identity.index && writer_mac == identity.mac) { return; }
        writer_generation = identity.generation; writer_index = identity.index; writer_mac = identity.mac;
        writer_published = true;
        version.store(++writer_version);
        generation_low.store(static_cast<uint32_t>(identity.generation));
        generation_high.store(static_cast<uint32_t>(identity.generation >> 32));
        index.store(static_cast<uint32_t>(identity.index));
        mac_low.store(static_cast<uint32_t>(identity.mac));
        mac_high.store(static_cast<uint32_t>(identity.mac >> 32));
        version.store(++writer_version);
    }
    __attribute__((noinline)) std::optional<TeslaBleIdentity> read() const
    {
        if (!synchronization_initialized()) { return std::nullopt; }
        for (int attempt = 0; attempt < 3; ++attempt) {
            const auto before = version.load();
            if (before & 1) { continue; }
            TeslaBleIdentity identity{uint64_t{generation_low.load()} | (uint64_t{generation_high.load()} << 32), static_cast<int32_t>(index.load()),
                uint64_t{mac_low.load()} | (uint64_t{mac_high.load()} << 32)};
            if (before == version.load()) { return identity; }
        }
        return std::nullopt; // Unavailable is NOT a disconnected identity.
    }
private:
    // Xtensa's conservative is_always_lock_free trait covers RMW/PSRAM, not
    // just aligned word loads/stores. Scalar accesses use no atomic helpers;
    // publish additionally enters/exits the FreeRTOS portMUX critical section.
    static_assert(alignof(std::atomic<uint32_t>) >= 4, "Aligned ESP32 word accesses required");
    tesla_warp::ScalarPublicationMutex writer_mutex;
    uint32_t writer_version = 0; // Accessed only under writer_mutex.
    // Last published value; accessed only under writer_mutex.
    uint64_t writer_generation = 0, writer_mac = 0;
    int writer_index = -1;
    bool writer_published = false;
    std::atomic<uint32_t> version{0}, generation_low{0}, generation_high{0}, index{UINT32_MAX}, mac_low{0}, mac_high{0};
};

// Callback producer, worker consumer. Presence is separate from the generation:
// zero is a valid lifecycle ID, never an implicit "nothing to clean up" sentinel.
// Lock covers the token and presence together; no split-atomic publication race.
class TeslaBlePendingDisconnect
{
public:
    bool synchronization_initialized() const { return mutex.initialized(); }
    void store(uint64_t generation)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (lock) { value = generation; pending = true; }
    }
    std::optional<uint64_t> take()
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock || !pending) { return std::nullopt; }
        pending = false;
        return value;
    }
    uint64_t load() const // Diagnostic/test observation, not a presence test.
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        return lock && pending ? value : 0;
    }
private:
    mutable tesla_warp::CheckedMutex mutex;
    uint64_t value = 0;
    bool pending = false;
};

// State lock is never held across BLE/filesystem/protocol/queue work. The
// only main-context state operation is consume(), with a zero-wait try_lock.
// Identity publication is independent of this lock.
class TeslaBleSession
{
public:
    bool synchronization_initialized() const { return mutex.initialized() && published.synchronization_initialized(); }
    bool exhausted() const {
        tesla_warp::CheckedMutex::Guard lock(mutex, true);
        return lock && retired;
    }
    void publish(TeslaBleIdentity identity)
    {
        published.publish(identity);
    }
    std::optional<TeslaBleIdentity> identity() const
    {
        return published.read();
    }
    uint64_t disconnect()
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock) { return 0; }
        connected = false;
        if (current.connection == UINT64_MAX) { retired = true; }
        else { ++current.connection; }
        current.request = 0;
        return current.connection;
    }
    void connect()
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock || retired) { return; }
        connected = true;
    }
    uint64_t connection() const
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock) { return 0; }
        return current.connection;
    }
    template <typename Ready>
    bool connect(uint64_t expected, Ready ready)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock) { return false; }
        if (retired || current.connection != expected) { return false; }
        connected = true;
        ready();
        return true;
    }
    void bind(uint64_t mac)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock) { return; }
        bound_mac = mac;
        current.request = 0;
    }
    TeslaBleRequestIdentity arm(uint64_t epoch, TeslaBleIdentity captured, int index)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock) { return {}; }
        const auto snapshot = identity();
        if (sequence == UINT64_MAX) { retired = true; connected = false; current.request = 0; }
        if (retired || sequence == UINT64_MAX || !snapshot || !connected || captured != *snapshot || captured.index != index ||
            captured.mac == 0 || captured.mac != bound_mac) {
            return {};
        }
        current.epoch = epoch;
        current.vehicle = captured;
        current.request = ++sequence;
        return current;
    }
    void disarm(const TeslaBleRequestIdentity &token)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (lock && current.request == token.request && current.epoch == token.epoch && current.connection == token.connection) {
            current.request = 0;
        }
    }
    bool matches(const TeslaBleRequestIdentity &token) const
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock) { return false; }
        const auto snapshot = identity();
        if (!snapshot) { return false; }
        auto latest = current;
        latest.vehicle = *snapshot;
        return tesla_ble_result_current(token, latest, bound_mac, latest.vehicle.index,
                                         connected, true, true, 50);
    }
    template <typename Apply>
    bool consume(const TeslaBleRequestIdentity &token, TeslaBleIdentity actual,
                 int index, bool authenticated, bool correlated, float soc, Apply apply, bool *retry = nullptr)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex, true);
        if (retry) { *retry = false; }
        if (!lock) { if (retry) { *retry = true; } return false; }
        const auto snapshot = identity();
        if (!snapshot) { if (retry) { *retry = true; } return false; }
        auto latest = current;
        latest.vehicle = *snapshot;
        if (actual != latest.vehicle ||
            !tesla_ble_result_current(token, latest, bound_mac, index, connected,
                                      authenticated, correlated, soc)) {
            return false;
        }
        // Linearization point against disconnect; the final SoC setter alone
        // runs here. All API/pairing/logging updates run after releasing the lock.
        apply(soc);
        current.request = 0;
        return true;
    }
private:
    mutable tesla_warp::CheckedMutex mutex;
    TeslaBleIdentitySnapshot published;
    TeslaBleRequestIdentity current;
    uint64_t bound_mac = 0;
    uint64_t sequence = 0;
    bool connected = false;
    bool retired = false;
};

template <typename Connect>
TeslaBleRequestIdentity tesla_ble_prepare_request(TeslaBleSession &session, uint64_t epoch,
                                                  TeslaBleIdentity captured, int index, Connect connect)
{
    if (!connect()) { return {}; }
    return session.arm(epoch, captured, index);
}
