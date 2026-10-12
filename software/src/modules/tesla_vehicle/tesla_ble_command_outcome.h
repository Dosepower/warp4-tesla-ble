#pragma once
#include <cstdint>
#include <cstring>
#include "tesla_ble_clean_platform.h"
#include "tesla_ble_result.h"
#include <optional>

// Publication order is an end-to-end 64-bit revision, not a signed low-word
// comparison that can misclassify a delayed successor across UINT32_MAX.
inline bool tesla_ble_publication_newer(uint64_t order, uint64_t applied)
{
    return order > applied;
}

// One worker producer, main consumer. Coalesces authoritative command state,
// not lossy queue notifications. Main never waits for the publication lock.
class TeslaBleCommandOutcome {
public:
    bool synchronization_initialized() const { return mutex.initialized(); }
    enum class Kind { KeyLoaded, KeyRemoved, PairStarted, PairApproved, Error };
    struct Outcome { Kind kind = Kind::KeyLoaded; bool key_present = false; bool key_known = true; char error[96] = {}; uint64_t order = 0; TeslaBleRequestIdentity request; };
    void publish_approval(TeslaBleRequestIdentity request)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock) { return; }
        value = {};
        value.kind = Kind::PairApproved;
        value.key_present = true;
        value.request = request;
        value.order = ++sequence;
    }
    uint64_t next_order()
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        return lock ? ++sequence : 0;
    }
    void publish(Kind kind, bool key_present, const char *error = nullptr, bool key_known = true)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock) { return; }
        value.kind = kind;
        value.key_present = key_present;
        value.key_known = key_known;
        std::strncpy(value.error, error ? error : "", sizeof(value.error) - 1);
        value.error[sizeof(value.error) - 1] = '\0';
        value.order = ++sequence;
    }
    std::optional<Outcome> take()
    {
        tesla_warp::CheckedMutex::Guard lock(mutex, true);
        if (!lock || acknowledged == value.order) { return std::nullopt; }
        auto result = value;
        acknowledged = value.order;
        return result;
    }
private:
    tesla_warp::CheckedMutex mutex;
    Outcome value;
    uint64_t sequence = 0, acknowledged = 0;
};
