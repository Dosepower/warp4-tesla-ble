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
    // Admission survives lossy/coalesced diagnostics. Only terminal main ACK
    // releases ownership, using the existing runtime mutex (no new allocation).
    enum class PairPhase { Idle, Queued, Working, Sent, Approval, Cancelling, Failed };
    struct Pair {
        uint64_t command = 0;
        uint32_t authority_started = 0, authority_config = 0, authority_binding = 0;
        PairPhase phase = PairPhase::Idle;
        TeslaBleIdentity vehicle;
        TeslaBleRequestIdentity approval;
        bool sent = false;
        bool completed = false; // Snapshot only: terminal was consumed/acknowledged.
        bool key_present = false, key_known = false;
        bool cleanup_required = false;
        uint64_t order = 0;
        char error[96] = {};
    };
    // Explicit admission pins fresh radio evidence to THIS command only. It is
    // not stored authorization and cannot be inherited by a later attempt.
    static constexpr uint32_t PAIR_AUTHORITY_MS = 120000;
    uint64_t accept_pair(TeslaBleIdentity identity, uint32_t now = 0,
                         uint32_t config = 0, uint32_t binding = 0)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex, true);
        if (lock && pair_sequence == UINT64_MAX) { exhausted = true; }
        if (!lock || deleting || cleanup_pending || exhausted || pair.command) { return 0; }
        pair = {};
        pair.command = ++pair_sequence;
        pair.phase = PairPhase::Queued;
        pair.vehicle = identity;
        pair.authority_started = now;
        pair.authority_config = config;
        pair.authority_binding = binding;
        return pair.command;
    }
    std::optional<Pair> claim_pair()
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock || deleting || cleanup_pending || exhausted || pair.phase != PairPhase::Queued) { return std::nullopt; }
        pair.phase = PairPhase::Working;
        return pair;
    }
    bool pair_current(uint64_t command)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        return lock && !deleting && !exhausted && command && command == pair.command &&
            (pair.phase == PairPhase::Working || pair.phase == PairPhase::Sent);
    }
    // Pin consent to the accepted SLAC generation, including a reconnect of
    bool pair_binding_current(uint32_t now, TeslaBleIdentity identity, uint32_t config, uint32_t binding)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex, true);
        return lock && !deleting && !cleanup_pending && !exhausted && pair.command &&
            (pair.phase == PairPhase::Working || pair.phase == PairPhase::Sent) &&
            pair.vehicle == identity && binding && pair.authority_binding == binding &&
            pair.authority_config == config && now - pair.authority_started < PAIR_AUTHORITY_MS;
    }
    // Pin consent to the accepted SLAC generation, including a reconnect of
    // the same profile/MAC. Never let a fresh session inherit an older action.
    bool binding_identity_current(TeslaBleIdentity identity)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex, true);
        return lock && !deleting && !exhausted && (!pair.command ||
            (pair.vehicle == identity && pair.phase != PairPhase::Failed && pair.phase != PairPhase::Cancelling));
    }
    void pair_sent(uint64_t command)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (lock && !deleting && command == pair.command && pair.phase == PairPhase::Working) {
            pair.phase = PairPhase::Sent;
            pair.sent = true;
        }
    }
    void pair_approved(uint64_t command, TeslaBleRequestIdentity request)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (lock && !deleting && command == pair.command && pair.phase == PairPhase::Sent) {
            pair.phase = PairPhase::Approval;
            pair.approval = request;
            pair.key_present = pair.key_known = true;
            pair.order = advance_order();
        }
    }
    uint64_t retire_pair()
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock || !pair.command || pair.phase == PairPhase::Failed) { return 0; }
        pair.phase = PairPhase::Cancelling;
        return pair.command;
    }
    void begin_cleanup() {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (lock) { cleanup_pending = true; }
    }
    void end_cleanup() {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (lock && cleanup_depth && --cleanup_depth == 0) { cleanup_pending = false; }
    }
    void begin_worker_cleanup() {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (lock) {
            cleanup_pending = true;
            if (cleanup_depth == UINT32_MAX) { exhausted = true; }
            else { ++cleanup_depth; }
        }
    }
    bool publication_exhausted() {
        tesla_warp::CheckedMutex::Guard lock(mutex, true);
        return lock && exhausted;
    }
    template<class Cleanup> uint64_t cancel_pair(uint64_t command, const char *message, bool key_present, bool key_known, bool cleanup_required, Cleanup cleanup)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock || !command || pair.command != command || pair.phase == PairPhase::Failed) { return 0; }
        // Worker-only lock order: outcome -> session for scoped disarm, never
        // session -> outcome. No SDK/storage work under either lock. MAIN uses
        // only zero-wait outcome/session operations and never waits behind this.
        // Terminal visibility is last: main cannot admit a successor before it.
        cleanup();
        pair.phase = PairPhase::Failed;
        pair.key_present = key_present;
        pair.key_known = key_known;
        pair.cleanup_required = cleanup_required;
        pair.order = advance_order();
        std::strncpy(pair.error, message, sizeof(pair.error) - 1);
        pair.error[sizeof(pair.error) - 1] = '\0';
        return pair.order;
    }
    template<class Approve> std::optional<Pair> take_pair_state(Approve approve)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex, true);
        if (!lock) { return std::nullopt; }
        auto result = pair;
        // Keep ownership if final session consumption fails (including transient
        // contention). Queued disconnect must retire THIS command, not a newly
        // admitted successor. Lock order is outcome -> session; no SDK/storage.
        if (!cleanup_pending && !exhausted && (pair.phase == PairPhase::Failed ||
            (pair.phase == PairPhase::Approval && !deleting && approve(pair.approval)))) {
            result.completed = true;
            pair = {};
        }
        return result;
    }
    bool accept_forget()
    {
        tesla_warp::CheckedMutex::Guard lock(mutex, true);
        if (!lock || deleting) { return false; }
        deleting = true;
        return true;
    }
    bool forgetting()
    {
        // Worker-only. Main uses stop_state()/forward_state(), never this wait.
        tesla_warp::CheckedMutex::Guard lock(mutex);
        return !lock || deleting || exhausted;
    }
    // Main is always zero-wait. Closure is intentional retirement, not retry.
    enum class Forward { Available, Contended, Retiring, Exhausted };
    Forward forward_state()
    {
        // Accepted deletion/retirement is visible even while a worker owns
        // outcome and waits for session. It must never spend a contention retry.
        if (exhausted.load()) { return Forward::Exhausted; }
        if (deleting.load() || cleanup_pending.load()) { return Forward::Retiring; }
        tesla_warp::CheckedMutex::Guard lock(mutex, true);
        if (!lock) { return Forward::Contended; }
        if (exhausted) { return Forward::Exhausted; }
        if (deleting || cleanup_pending) { return Forward::Retiring; }
        return Forward::Available;
    }
    bool forward_available() { return forward_state() == Forward::Available; }
    struct StopState { bool deletion_incomplete = false, exhausted = false, deletion_complete = false; };
    std::optional<StopState> stop_state()
    {
        tesla_warp::CheckedMutex::Guard lock(mutex, true);
        if (!lock) { return std::nullopt; }
        return StopState{deleting && !deletion_done, exhausted, deletion_done};
    }
    void finish_forget()
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (lock) { deletion_done = true; }
    }
    bool take_deleted()
    {
        tesla_warp::CheckedMutex::Guard lock(mutex, true);
        if (!lock || !deletion_done) { return false; }
        deleting = deletion_done = false; pair = {};
        return true;
    }
    enum class Kind { KeyLoaded, KeyRemoved, PairStarted, PairApproved, Error };
    struct Outcome { Kind kind = Kind::KeyLoaded; bool key_present = false; bool key_known = true; bool cleanup_required = false; char error[96] = {}; uint64_t order = 0; TeslaBleRequestIdentity request; };
    void publish_approval(TeslaBleRequestIdentity request)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock) { return; }
        const auto order = advance_order();
        if (!order) { return; }
        value = {};
        value.kind = Kind::PairApproved;
        value.key_present = true;
        value.request = request;
        value.order = order;
    }
    uint64_t next_order()
    {
        // Worker-only. Main terminal supervision must not allocate revisions.
        tesla_warp::CheckedMutex::Guard lock(mutex);
        return lock ? advance_order() : 0;
    }
    uint64_t publish(Kind kind, bool key_present, const char *error = nullptr, bool key_known = true, bool cleanup_required = false)
    {
        tesla_warp::CheckedMutex::Guard lock(mutex);
        if (!lock) { return 0; }
        const auto order = advance_order();
        if (!order) { return 0; } // Preserve the final sequenced record intact.
        value.kind = kind;
        value.key_present = key_present;
        value.key_known = key_known;
        value.cleanup_required = cleanup_required;
        std::strncpy(value.error, error ? error : "", sizeof(value.error) - 1);
        value.error[sizeof(value.error) - 1] = '\0';
        value.order = order;
        return value.order;
    }
    std::optional<Outcome> take()
    {
        tesla_warp::CheckedMutex::Guard lock(mutex, true);
        if (!lock || exhausted || acknowledged == value.order) { return std::nullopt; }
        auto result = value;
        acknowledged = value.order;
        return result;
    }
private:
    // Only under mutex. Zero is never a publication revision. Quarantine before
    // overflow; admission and consumption remain closed until restart.
    uint64_t advance_order() {
        if (sequence == UINT64_MAX) { exhausted = true; return 0; }
        return ++sequence;
    }
    tesla_warp::CheckedMutex mutex;
    Outcome value;
    Pair pair;
    std::atomic<bool> deleting{false}, cleanup_pending{false}, exhausted{false};
    bool deletion_done = false; // Under outcome mutex, not a forward-admission flag.
    uint32_t cleanup_depth = 0; // Nested worker unwinds cannot reopen admission.
    uint64_t sequence = 0, acknowledged = 0;
    uint64_t pair_sequence = 0; // Command identity is independent of event order.
};
