// SPDX-License-Identifier: Apache-2.0
// Protocol: official dispatcher/session.go:113-129, signer.go:96-129.
// Ownership/retry/transport-staging are independently authored local policy.
#include <tesla_clean/authentication.hpp>
#include <mbedtls/platform_util.h>
extern "C" {
#include <mbedtls/constant_time.h>
}
#include <algorithm>
#include <cstring>
namespace tesla_clean {
DomainAuthentication::DomainAuthentication(Domain domain,Bytes vin) : domain_(domain)
{
    valid_=(domain==Domain::Vcsec || domain==Domain::Infotainment) && vin.valid() && vin.size==vin_.size();
    if(valid_) { std::memcpy(vin_.data(),vin.data,vin_.size()); }
}
bool DomainAuthentication::advance()
{
    if(serial_==UINT64_MAX) { return false; }
    ++serial_; return true;
}
void DomainAuthentication::reset()
{
    advance(); pending_=Pending{}; established_.reset(); info_={}; ready_=session_=false;
    budget_={}; attempts_=0; last_attempt_=0; retired_epochs_={}; retired_count_=0;
    // Reconstruct the owned crypto member to erase its secret and readiness.
    crypto_.~SessionCrypto(); new(&crypto_) SessionCrypto();
    // UUID reuse history and nonwrapping local serial intentionally survive reset.
}
ChallengeResult DomainAuthentication::begin(IdentityStore &keys,const OperationTime &owner,uint32_t now,Random random,ChallengeTransport transport)
{
    if(!valid_ || !random.fill || !transport.send || owner.context==0 || owner.total_ms==0 || owner.response_ms==0 ||
       owner.total_ms>INT32_MAX || owner.response_ms>INT32_MAX) { return ChallengeResult::Invalid; }
    if(now-owner.started>=owner.total_ms) { return ChallengeResult::Expired; }
    if(owner.phase_ms>INT32_MAX || (owner.phase_ms && owner.phase_offset()>=owner.total_ms)) { return ChallengeResult::Invalid; }
    if(budget_.context!=owner.context || budget_.generation!=owner.generation) {
        budget_=owner; attempts_=0;
    } else if(budget_.started!=owner.started || budget_.total_ms!=owner.total_ms || budget_.response_ms!=owner.response_ms ||
              budget_.phase_ms!=owner.phase_ms) {
        return ChallengeResult::Invalid; // caller cannot restart a running operation's total deadline
    } else if(budget_.phase_started!=owner.phase_started) {
        // A new phase of the same operation gets a fresh challenge budget; a phase
        // can only move forward and never past the absolute deadline.
        if(owner.phase_offset()<=budget_.phase_offset()) { return ChallengeResult::Invalid; }
        budget_=owner; attempts_=0;
    }
    if(!owner.phase_open(now) || attempts_>=(owner.phase_ms?session_info_max_challenges:legacy_session_info_max_challenges)) { return ChallengeResult::Exhausted; }
    if(attempts_ && now-last_attempt_<100) { return ChallengeResult::Backoff; }
    ++attempts_; last_attempt_=now;
    Pending proposal; proposal.owner=owner; proposal.transmitted=now;
    proposal.view.domain=domain_; proposal.view.context=owner.context; proposal.view.generation=owner.generation;
    if(!(session_?established_.clone(proposal.identity):keys.acquire(proposal.identity)) ||
       !proposal.identity.public_point(proposal.view.public_point) ||
       random.fill(random.context,proposal.view.uuid.data(),proposal.view.uuid.size())!=0 ||
       (used_uuid_ && proposal.view.uuid==last_uuid_) ||
       (pending_.active && proposal.view.uuid==pending_.view.uuid) || !advance()) { return ChallengeResult::Refused; }
    proposal.serial=serial_; proposal.active=proposal.sending=true;
    const uint64_t serial=proposal.serial;
    // Detached predecessor allows rollback on refusal without clobbering a nested successor.
    Pending previous=std::move(pending_); pending_=std::move(proposal);
    const ChallengeView view=pending_.view;
    last_uuid_=view.uuid; used_uuid_=true;
    const bool accepted=transport.send(transport.context,view);
    if(serial_!=serial || !pending_.active || pending_.serial!=serial) { return ChallengeResult::Superseded; }
    if(!accepted) { pending_=std::move(previous); return ChallengeResult::Refused; }
    return sent(now,transport,random);
}
bool DomainAuthentication::replayable(const OperationTime &owner,uint32_t now) const
{
    const uint32_t age=now-pending_.transmitted;
    // Same owner/budget only: a replay can never restart or extend a deadline.
    return valid_ && pending_.active && !pending_.sending && owner.context!=0 &&
        pending_.owner.context==owner.context && pending_.owner.generation==owner.generation &&
        pending_.owner.started==owner.started && pending_.owner.total_ms==owner.total_ms &&
        pending_.owner.response_ms==owner.response_ms && pending_.owner.phase_started==owner.phase_started &&
        pending_.owner.phase_ms==owner.phase_ms && owner.phase_open(now) &&
        pending_.replays<(owner.phase_ms?session_info_max_replays:legacy_session_info_max_replays) && age<session_info_max_latency_ms &&
        // A copy that cannot complete while still fresh is useless; renew instead.
        session_info_max_latency_ms-age>pending_.transmit_elapsed;
}
ChallengeResult DomainAuthentication::replay(const OperationTime &owner,uint32_t now,Random random,ChallengeTransport transport)
{
    if(!transport.send || !replayable(owner,now)) { return ChallengeResult::Invalid; }
    if(now-last_attempt_<100) { return ChallengeResult::Backoff; }
    last_attempt_=now; ++pending_.replays;
    const uint64_t serial=pending_.serial;
    // Identical request: same UUID, routing, ephemeral identity public point.
    const ChallengeView view=pending_.view;
    pending_.sending=true;
    const bool accepted=transport.send(transport.context,view);
    if(serial_!=serial || !pending_.active || pending_.serial!=serial) { return ChallengeResult::Superseded; }
    if(!accepted) {
        // Earlier complete copies remain answerable; a reply captured during the
        // refused copy is dropped rather than trusted.
        pending_.sending=pending_.staged=false; return ChallengeResult::Refused;
    }
    return sent(now,transport,random);
}
ChallengeResult DomainAuthentication::sent(uint32_t tx_started,ChallengeTransport transport,Random random)
{
    pending_.sending=false;
    // Response budget starts when the complete framed copy was accepted (official
    // dispatcher.go tryStartSession: RetryInterval after Send returned).
    const uint32_t current=transport.now?transport.now(transport.context):tx_started;
    const uint32_t elapsed=current-tx_started,since=current-pending_.transmitted;
    pending_.transmit_elapsed=elapsed<=INT32_MAX?elapsed:0;
    // Each completed identical copy opens its own response budget; never shrink.
    if(since<=INT32_MAX) { pending_.answer_until=std::max(pending_.answer_until,since+pending_.owner.response_ms); }
    if(pending_.staged) {
        // Move staged bytes out before receive clears pending ownership.
        const uint32_t received_at=pending_.staged_at;
        auto bytes=pending_.encoded; auto tag=pending_.tag; const size_t length=pending_.length;
        SessionReply reply{domain_,pending_.owner.generation,pending_.view.uuid,{bytes.data(),length},{tag.data(),tag.size()}};
        pending_.staged=false;
        // Recheck deadline/freshness at dispatch time: no late authentication.
        const auto result=receive(reply,received_at,current,random);
        if(result==SessionAdmission::Established || result==SessionAdmission::Updated) { return ChallengeResult::Established; }
        if(result==SessionAdmission::KeyDenied) { return ChallengeResult::KeyDenied; }
    }
    return ChallengeResult::Sent;
}
SessionAdmission DomainAuthentication::receive(const SessionReply &reply,uint32_t now,uint32_t processed,Random random)
{
    // Three separate bounds, all checked: the response budget after the latest
    // completed identical copy (capture time; while a copy is still sending the
    // reply is only staged and rechecked after completion), the challenge's own
    // freshness fixed at creation and the owner's total deadline (both at capture
    // AND dispatch time). A renewed challenge has a new UUID, so predecessor
    // replies cannot match.
    // Same admission conditions as one combined test; split only to record
    // which gate refused (all are side-effect free).
    rejection_=SessionRejection::None;
    const auto reject=[this](SessionRejection why) { rejection_=why; return SessionAdmission::Rejected; };
    if(!valid_ || !pending_.active) { return reject(SessionRejection::NoPending); }
    if(reply.domain!=domain_ || reply.generation!=pending_.owner.generation) { return reject(SessionRejection::Route); }
    if(reply.uuid!=pending_.view.uuid) { return reject(SessionRejection::Uuid); }
    if(now-pending_.owner.started>=pending_.owner.total_ms || processed-pending_.owner.started>=pending_.owner.total_ms) {
        return reject(SessionRejection::Deadline);
    }
    if(now-pending_.transmitted>=session_info_max_latency_ms || processed-pending_.transmitted>=session_info_max_latency_ms) {
        return reject(SessionRejection::Stale);
    }
    // Nothing captured after the phase's own step budget authenticates (late frame).
    if(!pending_.owner.phase_open(now) || !pending_.owner.phase_open(processed)) { return reject(SessionRejection::Phase); }
    if(!pending_.sending && now-pending_.transmitted>=pending_.answer_until) { return reject(SessionRejection::Window); }
    if(!reply.encoded.valid() || reply.encoded.size==0 || reply.encoded.size>pending_.encoded.size() ||
       !reply.tag.valid() || reply.tag.size!=pending_.tag.size()) { return reject(SessionRejection::Malformed); }
    if(pending_.sending) {
        if(pending_.staged) { return reject(SessionRejection::Staged); }
        std::memcpy(pending_.encoded.data(),reply.encoded.data,reply.encoded.size);
        std::memcpy(pending_.tag.data(),reply.tag.data,reply.tag.size);
        pending_.length=reply.encoded.size; pending_.staged_at=now; pending_.staged=true;
        return SessionAdmission::Deferred;
    }
    SessionInfo candidate; std::array<uint8_t,16> secret{};
    const bool update=session_ && pending_.identity.same(established_);
    SessionInfoResult result=SessionInfoResult::Rejected;
    if(update) {
        // Resynchronization and denial must be vouched for by the established peer,
        // not by a new public key carried by an otherwise untrusted response.
        if(!verify_update(reply,candidate)) { rejection_=SessionRejection::Authentication; }
        else if(!fresh_epoch(candidate)) { rejection_=SessionRejection::Epoch; }
        else { result=candidate.status==0?SessionInfoResult::Established:SessionInfoResult::KeyDenied; }
    } else {
        result=authenticate_session_info(*pending_.identity.key(),random,{vin_.data(),vin_.size()},
            {reply.uuid.data(),reply.uuid.size()},reply.encoded,reply.tag,candidate,secret);
    }
    if(result==SessionInfoResult::Rejected) {
        if(rejection_==SessionRejection::None) { rejection_=SessionRejection::Authentication; }
        return SessionAdmission::Rejected;
    }
    if(result==SessionInfoResult::KeyDenied) {
        pending_=Pending{}; ready_=false; return SessionAdmission::KeyDenied;
    }
    if(update) {
        // Tesla update semantics never roll the outgoing counter backwards.
        if(candidate.epoch!=info_.epoch || candidate.clock>=info_.clock) {
            apply_update(candidate);
        }
    } else {
        crypto_.set_key({secret.data(),secret.size()});
        info_=candidate; established_=std::move(pending_.identity); session_=true;
    }
    mbedtls_platform_zeroize(secret.data(),secret.size());
    pending_=Pending{}; ready_=true;
    return update?SessionAdmission::Updated:SessionAdmission::Established;
}
bool DomainAuthentication::verify_update(const SessionReply &reply,SessionInfo &candidate) const
{
    std::array<uint8_t,32> expected{};
    const bool authentic=reply.encoded.valid() && reply.encoded.size && reply.encoded.size<=512 &&
        reply.tag.valid() && reply.tag.size==expected.size() &&
        crypto_.session_info_tag({vin_.data(),vin_.size()},{reply.uuid.data(),reply.uuid.size()},reply.encoded,expected) &&
        mbedtls_ct_memcmp(expected.data(),reply.tag.data,expected.size())==0;
    mbedtls_platform_zeroize(expected.data(),expected.size());
    return authentic && decode_session_info(reply.encoded,candidate) && candidate.remote_public==info_.remote_public;
}
bool DomainAuthentication::fresh_epoch(const SessionInfo &candidate) const
{
    if(candidate.epoch==info_.epoch) { return true; }
    if(retired_count_==retired_epochs_.size()) { return false; }
    for(size_t i=0;i<retired_count_;++i) { if(candidate.epoch==retired_epochs_[i]) { return false; } }
    return true;
}
void DomainAuthentication::apply_update(SessionInfo candidate)
{
    if(candidate.epoch!=info_.epoch) { retired_epochs_[retired_count_++]=info_.epoch; }
    // The official signer preserves its counter even on epoch changes. The
    // Vehicle separately preserves every potentially exposed outgoing counter.
    candidate.counter=std::max(candidate.counter,info_.counter); info_=candidate;
}
SessionAdmission DomainAuthentication::receive_update(const SessionReply &reply,const Request &request,uint32_t now)
{
    rejection_=SessionRejection::None;
    const auto reject=[this](SessionRejection why) { rejection_=why; return SessionAdmission::Rejected; };
    if(!valid_ || !ready_ || !session_ || pending_.active || request.context==0) { return reject(SessionRejection::NotReady); }
    if(reply.domain!=domain_ || reply.generation!=request.generation) { return reject(SessionRejection::Route); }
    if(reply.uuid!=request.uuid) { return reject(SessionRejection::Uuid); }
    if(!request.total_ms || !request.response_ms || request.total_ms>INT32_MAX || request.response_ms>INT32_MAX ||
       request.transmit_elapsed>INT32_MAX) { return reject(SessionRejection::Malformed); }
    if(request.transmitted-request.started>=request.total_ms || now-request.started>=request.total_ms) {
        return reject(SessionRejection::Deadline);
    }
    if(now-request.transmitted>=request.transmit_elapsed+request.response_ms || now-request.transmitted>=5000) {
        return reject(SessionRejection::RequestWindow);
    }
    SessionInfo candidate;
    if(!verify_update(reply,candidate)) { return reject(SessionRejection::Authentication); }
    if(!fresh_epoch(candidate) || (candidate.epoch==info_.epoch && candidate.clock<info_.clock)) { return reject(SessionRejection::Epoch); }
    if(candidate.status!=0) { ready_=false; return SessionAdmission::KeyDenied; }
    // All admission and fallible work precedes the allocation-free publication.
    apply_update(candidate); return SessionAdmission::Updated;
}
}
