// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/identity.hpp>
#include <tesla_clean/response.hpp>
#include <tesla_clean/session_info.hpp>
namespace tesla_clean {
struct OperationTime { uint64_t context=0,generation=0; uint32_t started=0,total_ms=0,response_ms=0; };
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
// Unanswered challenge: retransmit the identical request (same UUID) at most this
// many times while it stays fresh (pinned tesla-ble v6.2.0 vehicle.cpp:320-342
// resends unchanged and renews only at CLOCK_SYNC_MAX_LATENCY). A reply to any
// copy then remains admissible until the challenge's own freshness/total deadline
// instead of being invalidated by an artificial UUID change.
inline constexpr unsigned session_info_max_replays=3;
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
    // Always creates a fresh challenge (new UUID); counts toward the 3-challenge cap.
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
