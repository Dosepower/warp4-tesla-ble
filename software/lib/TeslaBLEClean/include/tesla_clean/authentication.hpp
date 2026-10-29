// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/identity.hpp>
#include <tesla_clean/response.hpp>
#include <tesla_clean/session_info.hpp>
namespace tesla_clean {
// started/total_ms: the operation's absolute deadline (never extended).
// phase_started/phase_ms: the current authentication phase's own step budget
// (pinned tesla-ble v6.2.0 set_command_phase_ restarts the step clock per phase,
// vehicle.cpp:640-649, 1391-1400). phase_ms==0: one phase spanning the operation.
// A phase never ends after the absolute deadline and only ever moves forward.
struct OperationTime {
    uint64_t context=0,generation=0; uint32_t started=0,total_ms=0,response_ms=0,phase_started=0,phase_ms=0;
    uint32_t phase_offset() const { return phase_ms?phase_started-started:0; }
    uint32_t phase_budget() const { return phase_ms?phase_ms:total_ms; }
    // True while `at` lies inside both the phase budget and the absolute deadline.
    bool phase_open(uint32_t at) const {
        return at-started<total_ms && (phase_ms==0 || (phase_offset()<total_ms && at-phase_started<phase_ms));
    }
};
struct ChallengeView {
    Domain domain=Domain::Vcsec;
    uint64_t context=0,generation=0;
    std::array<uint8_t,16> uuid{};
    std::array<uint8_t,65> public_point{};
};
// Optional now(): clock read after send() accepted the complete framed request.
// The response window starts at that ATT-acknowledged completion, as the official
// dispatcher.go tryStartSession waits RetryInterval only after Send returned.
struct ChallengeTransport { bool (*send)(void *,const ChallengeView &)=nullptr; void *context=nullptr; uint32_t (*now)(void *)=nullptr; };
// Official pkg/connector/ble/ble.go maxLatency (AllowedLatency): SessionInfo older
// than this from request creation is discarded (dispatcher.go checkForSessionUpdate).
inline constexpr uint32_t session_info_max_latency_ms=4000;
// Pinned reference tesla-ble v6.2.0 (a00f3872) include/vehicle.h:363-381 and
// src/vehicle.cpp:319-344: an unanswered session request is resent UNCHANGED every
// TRANSPORT_RETRY_INTERVAL (1 s) and rebuilt with a new UUID once it is
// CLOCK_SYNC_MAX_LATENCY (4 s) old, without extending the AUTH_RESPONSE_TIMEOUT
// (25 s) wait. A reply to any copy stays admissible while its challenge is fresh.
inline constexpr uint32_t session_info_resend_interval_ms=1000; // TRANSPORT_RETRY_INTERVAL
// MAX_AUTH_RESENDS: identical copies of ONE challenge (freshness binds first: <=3 at 1 s).
inline constexpr unsigned session_info_max_replays=10;
// AUTH_RESPONSE_TIMEOUT: production step budget of one authentication phase.
inline constexpr uint32_t session_auth_phase_ms=25000;
// New challenges (UUIDs) per authentication phase: ceil(25 s / 4 s) + 1. Unlike the
// reference (whose 10 resends span only ~10 s of its 25 s wait, leaving no fresh
// challenge after ~12 s), fresh coverage continues through the whole phase, so a
// late-waking infotainment answering at e.g. 20 s is admitted. Hard bound per phase:
// <= 8 UUIDs and <= 1 + 25 s / 1 s = 26 copies; never beyond the absolute deadline.
inline constexpr unsigned session_info_max_challenges=8;
// Single-phase operations (OperationTime::phase_ms==0: pairing, approval
// verification polls, legacy callers) keep their original caps unchanged.
inline constexpr unsigned legacy_session_info_max_replays=3;
inline constexpr unsigned legacy_session_info_max_challenges=3;
struct SessionReply { Domain domain=Domain::Vcsec; uint64_t generation=0; std::array<uint8_t,16> uuid{}; Bytes encoded,tag; };
enum class ChallengeResult { Sent, Refused, Superseded, Established, KeyDenied, Backoff, Exhausted, Expired, Invalid };
enum class SessionAdmission { Rejected, Deferred, Established, Updated, KeyDenied };
class DomainAuthentication {
    struct Pending {
        OperationTime owner;
        ChallengeView view;
        IdentityLease identity;
        // transmitted = challenge creation, before the first copy's TX start; it is
        // the fixed freshness anchor and never moves on replay. transmit_elapsed is
        // the duration of the latest copy's complete ATT-acknowledged framed TX;
        // answer_until (offset from transmitted) = latest copy completion + response_ms.
        uint32_t transmitted=0,staged_at=0,transmit_elapsed=0,answer_until=0;
        uint64_t serial=0;
        unsigned replays=0;
        bool active=false,sending=false,staged=false;
        std::array<uint8_t,512> encoded{};
        size_t length=0;
        std::array<uint8_t,32> tag{};
    };
    Domain domain_;
    std::array<uint8_t,17> vin_{};
    bool valid_=false,ready_=false,session_=false;
    SessionInfo info_{};
    SessionCrypto crypto_;
    IdentityLease established_;
    Pending pending_;
    OperationTime budget_;
    unsigned attempts_=0;
    uint32_t last_attempt_=0;
    uint64_t serial_=0;
    std::array<uint8_t,16> last_uuid_{};
    bool used_uuid_=false;
    // Do not readmit an epoch retired on this live session. Refuse further epoch
    // changes on capacity exhaustion instead of silently forgetting old epochs.
    std::array<std::array<uint8_t,16>,8> retired_epochs_{};
    size_t retired_count_=0;
    bool advance();
    bool verify_update(const SessionReply &reply,SessionInfo &candidate) const;
    bool fresh_epoch(const SessionInfo &candidate) const;
    void apply_update(SessionInfo candidate);
    ChallengeResult sent(uint32_t tx_started,ChallengeTransport transport,Random random);
public:
    bool exhausted() const { return serial_ == UINT64_MAX; }
    DomainAuthentication(Domain domain,Bytes vin);
    // Always creates a fresh challenge (new UUID); counts toward the per-phase challenge cap.
    // Exhausted: the phase budget (time or challenges) is spent; Expired: absolute deadline.
    ChallengeResult begin(IdentityStore &keys,const OperationTime &owner,uint32_t now,Random random,ChallengeTransport transport);
    // True only for this exact owner's idle pending challenge with replay budget
    // left and enough freshness for another copy of the latest TX duration.
    bool replayable(const OperationTime &owner,uint32_t now) const;
    // Retransmits the identical pending challenge. Never changes its UUID, identity,
    // freshness anchor or the owner's total deadline; otherwise like begin().
    ChallengeResult replay(const OperationTime &owner,uint32_t now,Random random,ChallengeTransport transport);
    // received: capture time of the complete frame; processed: current time when a
    // deferred frame is dispatched. Both must be within total deadline/freshness.
    SessionAdmission receive(const SessionReply &reply,uint32_t received,uint32_t processed,Random random);
    SessionAdmission receive(const SessionReply &reply,uint32_t now,Random random) { return receive(reply,now,now,random); }
    // Caller supplies a snapshot of the currently active, fully accepted command.
    // This path never starts a handshake, rekeys, reallocates identity or resets budgets.
    SessionAdmission receive_update(const SessionReply &reply,const Request &request,uint32_t now);
    bool pending() const { return pending_.active; }
    bool ready() const { return ready_; }
    bool identity(IdentityLease &output) const { return established_.clone(output); }
    const SessionInfo &info() const { return info_; }
    const SessionCrypto *crypto() const { return ready_?&crypto_:nullptr; }
    void reset();
};
}
