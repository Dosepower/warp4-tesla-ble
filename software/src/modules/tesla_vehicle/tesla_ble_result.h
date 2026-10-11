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

// Single MAIN writer, bounded worker/callback readers. Atomic fields avoid
// C++ seqlock data races; sequential consistency establishes one total order.
// Split the 64-bit lifecycle identity into aligned words. The bounded-read
// coherence tag is not a lifecycle ID; its odd/even wrap does not reset identity.
// No 64-bit atomics (ESP32 cannot guarantee they are lock-free).
class TeslaBleIdentitySnapshot
{
public:
    __attribute__((noinline)) void publish(TeslaBleIdentity identity)
    {
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
    // just aligned word loads/stores. Do not use RMW here; final ELF audit must
    // verify these named methods emit only l32i/s32i/memw, no atomic helpers.
    static_assert(alignof(std::atomic<uint32_t>) >= 4, "Aligned ESP32 word accesses required");
    uint32_t writer_version = 0; // Single main writer, never read by a worker.
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
    bool synchronization_initialized() const { return mutex.initialized(); }
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
        ++current.connection;
        current.request = 0;
        return current.connection;
    }
    void connect()
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock) { return; }
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
        if (current.connection != expected) { return false; }
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
        if (!snapshot || !connected || captured != *snapshot || captured.index != index ||
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
                 int index, bool authenticated, bool correlated, float soc, Apply apply)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex, true);
        if (!lock) { return false; }
        const auto snapshot = identity();
        if (!snapshot) { return false; }
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
};

template <typename Connect>
TeslaBleRequestIdentity tesla_ble_prepare_request(TeslaBleSession &session, uint64_t epoch,
                                                  TeslaBleIdentity captured, int index, Connect connect)
{
    if (!connect()) { return {}; }
    return session.arm(epoch, captured, index);
}
