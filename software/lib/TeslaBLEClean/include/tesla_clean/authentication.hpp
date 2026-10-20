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
struct ChallengeTransport { bool (*send)(void *,const ChallengeView &)=nullptr; void *context=nullptr; };
struct SessionReply { Domain domain=Domain::Vcsec; uint64_t generation=0; std::array<uint8_t,16> uuid{}; Bytes encoded,tag; };
enum class ChallengeResult { Sent, Refused, Superseded, Established, KeyDenied, Backoff, Exhausted, Expired, Invalid };
enum class SessionAdmission { Rejected, Deferred, Established, Updated, KeyDenied };
class DomainAuthentication {
    struct Pending {
        OperationTime owner;
        ChallengeView view;
        IdentityLease identity;
        uint32_t transmitted=0,staged_at=0;
        uint64_t serial=0;
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
public:
    bool exhausted() const { return serial_ == UINT64_MAX; }
    DomainAuthentication(Domain domain,Bytes vin);
    ChallengeResult begin(IdentityStore &keys,const OperationTime &owner,uint32_t now,Random random,ChallengeTransport transport);
    SessionAdmission receive(const SessionReply &reply,uint32_t now,Random random);
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
