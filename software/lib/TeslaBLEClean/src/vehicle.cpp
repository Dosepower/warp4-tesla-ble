// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/vehicle.hpp>
#include <algorithm>
#include <cstring>
namespace tesla_clean {
namespace {
bool validate_status(Bytes encoded,void *) { VcsecStatus status; return decode_vcsec_status(encoded,status); }
bool validate_charge(Bytes encoded,void *) { return charge_state_well_formed(encoded); }
}
Vehicle::Vehicle(IdentityStore &keys,Bytes vin,Bytes address,VehiclePorts ports)
    : keys_(keys),ports_(ports),vcsec_(Domain::Vcsec,vin),info_(Domain::Infotainment,vin)
{
    valid_=vin.valid() && vin.size==17 && address.valid() && address.size==16 && ports.random.fill &&
        ports.now && ports.write && ports.owns && ports.chunk_size>0 && ports.chunk_size<=1026;
    if(valid_) { std::memcpy(vin_.data(),vin.data,17); std::memcpy(address_.data(),address.data,16); }
}
bool Vehicle::advance()
{
    if(serial_==UINT64_MAX) { return false; } ++serial_; return true;
}
bool Vehicle::open(uint64_t generation)
{
    if(!valid_ || exhausted()) { return false; }
    if(connected_) { return generation==generation_; }
    if(ever_opened_ && generation<=last_generation_) { return false; }
    if(!advance()) { return false; }
    generation_=last_generation_=generation; connected_=ever_opened_=true;
    receiver_.reset(generation); return true;
}
void Vehicle::close(uint64_t generation)
{
    if(!connected_ || generation!=generation_) { return; }
    // Detach everything before callbacks. A nested successor may own fresh RX/TX/commands.
    connected_=false; advance(); transmission_=0; deferred_count_=0; deferred_overflow_=false;
    pair_context_=0; pair_reply_pending_=pair_reply_terminal_=false;
    receiver_.reset(generation); vcsec_.reset(); info_.reset();
    std::array<Command,4> retired=std::move(commands_); const size_t count=count_; count_=0; commands_={};
    for(size_t i=0;i<count;++i) {
        VehicleResult result{retired[i].context,retired[i].generation,VehicleResultCode::Disconnected,0,SleepState::Unknown,false};
        retired[i].completion(result);
    }
}
bool Vehicle::enqueue(Kind kind,WakePolicy policy,uint64_t context,VehicleCompletion &&completion,VehicleTiming timing)
{
    if(operation_sequence_==UINT64_MAX) { operation_retired_=true; }
    if(exhausted() || !valid_ || !connected_ || operation_sequence_==UINT64_MAX || context==0 || count_==commands_.size() ||
       timing.total_ms==0 || timing.response_ms==0 || timing.transmit_ms==0 || timing.total_ms>INT32_MAX ||
       timing.response_ms>INT32_MAX || timing.transmit_ms>INT32_MAX ||
       (policy!=WakePolicy::NoWakeSkip && policy!=WakePolicy::WakeIfNeeded)) { return false; }
    for(size_t i=0;i<count_;++i) { if(commands_[i].context==context) { return false; } }
    Command c; c.kind=kind; c.policy=policy; c.context=context; c.generation=generation_;
    c.authentication_context=++operation_sequence_;
    c.started=ports_.now(ports_.context); c.timing=timing; c.completion=std::move(completion);
    c.stage=kind==Kind::Pair?Stage::Pair:Stage::VcsecAuth; commands_[count_++]=std::move(c); return true;
}
bool Vehicle::charge_state_poll(WakePolicy policy,uint64_t context,VehicleCompletion &&cb,VehicleTiming timing)
{ return enqueue(Kind::Poll,policy,context,std::move(cb),timing); }
bool Vehicle::status(uint64_t context,VehicleCompletion &&cb,VehicleTiming timing)
{ return enqueue(Kind::Status,WakePolicy::NoWakeSkip,context,std::move(cb),timing); }
bool Vehicle::wake(bool authorized,uint64_t context,VehicleCompletion &&cb,VehicleTiming timing)
{ return authorized && enqueue(Kind::Wake,WakePolicy::WakeIfNeeded,context,std::move(cb),timing); }
bool Vehicle::pair(bool authorized,uint64_t context,VehicleCompletion &&cb,VehicleTiming timing)
{ return authorized && enqueue(Kind::Pair,WakePolicy::NoWakeSkip,context,std::move(cb),timing); }
bool Vehicle::take_pairing_status(uint64_t context,uint64_t generation,VcsecStatus &status)
{
    if(!context || context!=pair_context_ || !connected_ || generation!=generation_ || !pair_reply_pending_) { return false; }
    status=pair_reply_; pair_reply_pending_=false; return true;
}
void Vehicle::end_pairing_status(uint64_t context)
{
    if(context==pair_context_) { pair_context_=0; pair_reply_pending_=pair_reply_terminal_=false; }
}
void Vehicle::finish(VehicleResultCode code,uint8_t battery,SleepState sleep,bool authenticated)
{
    if(!count_) { return; }
    Command retired=std::move(commands_[0]);
    for(size_t i=1;i<count_;++i) { commands_[i-1]=std::move(commands_[i]); }
    commands_[--count_]=Command{}; advance(); transmission_=0; deferred_count_=0;
    if(vcsec_.guard) { vcsec_.guard->cancel(); } if(info_.guard) { info_.guard->cancel(); }
    VehicleResult result{retired.context,retired.generation,code,battery,sleep,authenticated};
    retired.completion(result);
}
void Vehicle::retry(Stage stage,uint32_t now)
{
    if(!count_) { return; }
    auto &c=commands_[0]; c.stage=stage; c.retry_at=now; c.delayed=true;
}
bool Vehicle::transmit_owns(void *p,uint64_t generation)
{
    auto &tx=*static_cast<Transmission *>(p); auto &v=*tx.vehicle;
    if(v.exhausted() || !v.connected_ || v.generation_!=generation || tx.generation!=generation || v.serial_!=tx.serial ||
       v.transmission_!=tx.serial || v.deferred_overflow_) { return false; }
    const uint32_t now=tx.ports.now(tx.ports.context);
    return now-tx.started<tx.timing.transmit_ms && now-tx.operation_started<tx.timing.total_ms && tx.ports.owns(tx.ports.context,generation);
}
bool Vehicle::transmit_write(void *p,Bytes bytes,uint64_t generation)
{
    auto &tx=*static_cast<Transmission *>(p); return tx.ports.write(tx.ports.context,bytes,generation);
}
bool Vehicle::transmit(Bytes payload,Transmission &tx)
{
    if(serial_!=tx.serial || !connected_) { return false; }
    transmission_=tx.serial; deferred_count_=0; deferred_overflow_=false;
    tx.accepted=send_frame(payload,tx.ports.chunk_size,tx.generation,transmit_write,transmit_owns,&tx);
    if(serial_==tx.serial && transmission_==tx.serial) { transmission_=0; }
    if(!tx.accepted && serial_==tx.serial && connected_ && generation_==tx.generation) {
        // BLE framing has no delimiter that can safely follow an abandoned prefix.
        // Retry only when the backend can prove that its peer stream is clean.
        const bool recovered=tx.ports.recover_frame && tx.ports.recover_frame(tx.ports.context,tx.generation);
        if(serial_==tx.serial && (!recovered || !tx.ports.owns(tx.ports.context,tx.generation))) { close(tx.generation); }
    }
    return tx.accepted;
}
bool Vehicle::transmit_challenge(void *p,const ChallengeView &challenge)
{
    auto &tx=*static_cast<Transmission *>(p); Frame wire;
    return encode_session_request(challenge,{tx.vehicle->address_.data(),16},wire) && tx.vehicle->transmit({wire.data.data(),wire.length},tx);
}
void Vehicle::drain(Transmission &tx)
{
    if(serial_!=tx.serial) { return; }
    if(!tx.accepted) { deferred_count_=0; return; }
    while(deferred_count_ && serial_==tx.serial) {
        Deferred detached=deferred_[0];
        for(size_t i=1;i<deferred_count_;++i) { deferred_[i-1]=deferred_[i]; } --deferred_count_;
        frame(detached.frame,detached.now);
    }
}
bool Vehicle::authenticate(Domain domain,Stage waiting,Stage next,uint32_t now)
{
    auto &c=commands_[0]; auto &ch=channel(domain);
    if(ch.authentication.ready() && !c.refresh) { c.stage=next; return true; }
    if(!advance()) { finish(VehicleResultCode::Failed); return false; }
    ++c.attempts; c.transmitted=now; c.stage=waiting;
    Transmission tx{this,ports_,serial_,generation_,now,c.started,c.timing};
    const OperationTime time{c.authentication_context,c.generation,c.started,c.timing.total_ms,c.timing.response_ms};
    const auto result=ch.authentication.begin(keys_,time,now,ports_.random,{transmit_challenge,&tx});
    if(serial_!=tx.serial) { return false; }
    if(result==ChallengeResult::Sent || result==ChallengeResult::Established) { drain(tx); }
    else if(result==ChallengeResult::KeyDenied) { finish(VehicleResultCode::KeyDenied); }
    else if(result==ChallengeResult::Expired) { finish(VehicleResultCode::Expired); }
    else if(result==ChallengeResult::Exhausted || result==ChallengeResult::Invalid) { finish(VehicleResultCode::Failed); }
    else { retry(domain==Domain::Vcsec?Stage::VcsecAuth:Stage::InfoAuth,now); drain(tx); }
    return false;
}
void Vehicle::encrypted(Domain domain,Bytes payload,Stage waiting,uint32_t now)
{
    auto &c=commands_[0]; auto &ch=channel(domain);
    if(!ch.authentication.ready() || !ch.guard || !advance()) { finish(VehicleResultCode::Failed); return; }
    ++c.attempts;
    SessionInfo session=ch.authentication.info(); session.counter=std::max(session.counter,ch.high_water);
    IdentityLease identity; std::array<uint8_t,65> point{}; Route route; route.domain=domain; route.address=address_;
    EncodedRequest encoded;
    bool built=ch.authentication.identity(identity) && identity.public_point(point) &&
        ports_.random.fill(ports_.random.context,route.uuid.data(),16)==0 && (!uuid_used_ || route.uuid!=last_uuid_);
    if(built) {
        OutgoingMessages messages(*ch.authentication.crypto(),session,{vin_.data(),17},{point.data(),65});
        built=messages.build(payload,route,now-ch.clock_anchor,5,ports_.random,encoded);
    }
    if(!built) { retry(c.stage,now); return; }
    // Reserve every potentially exposed counter, including partial/refused sends. Never roll it back.
    ch.high_water=encoded.counter; last_uuid_=route.uuid; uuid_used_=true;
    Transmission tx{this,ports_,serial_,generation_,now,c.started,c.timing};
    const Stage sending=c.stage;
    // An explicit wake authorization is consumed before possible wire exposure,
    // not after the adapter reports success. A refused write may have reached the car.
    if(waiting==Stage::WaitWake) { c.wake_sent=true; }
    if(!transmit({encoded.bytes.data(),encoded.length},tx)) {
        if(serial_==tx.serial) { retry(waiting==Stage::WaitWake?Stage::Status:sending,now); drain(tx); } return;
    }
    if(serial_!=tx.serial) { return; }
    Request request; request.uuid=route.uuid; request.identifier_length=encoded.identifier_length;
    std::copy_n(encoded.identifier.data(),encoded.identifier_length,request.identifier.data());
    request.context=c.context; request.generation=c.generation; request.started=c.started; request.transmitted=now;
    request.total_ms=c.timing.total_ms; request.response_ms=c.timing.response_ms;
    ch.guard->cancel();
    if(!ch.guard->arm(request)) { finish(VehicleResultCode::Failed); return; }
    c.stage=c.published=waiting; c.has_request=true; c.transmitted=now; c.delayed=false;
    if(waiting==Stage::WaitWake) { c.wake_sent=true; }
    drain(tx);
}
void Vehicle::tick()
{
    if(exhausted() || !connected_ || !count_ || transmission_) { return; }
    const uint32_t now=ports_.now(ports_.context); auto &c=commands_[0];
    if(now-c.started>=c.timing.total_ms) { finish(VehicleResultCode::Expired); return; }
    if(c.delayed) { if(now-c.retry_at<100) { return; } c.delayed=false; }
    if(c.stage==Stage::WaitVcsecAuth || c.stage==Stage::WaitInfoAuth || c.stage==Stage::WaitStatus || c.stage==Stage::WaitCharge || c.stage==Stage::WaitWake) {
        if(now-c.transmitted<c.timing.response_ms) { return; }
        Stage next=Stage::Status;
        if(c.stage==Stage::WaitVcsecAuth) { next=Stage::VcsecAuth; }
        if(c.stage==Stage::WaitInfoAuth) { next=Stage::InfoAuth; }
        if(c.stage==Stage::WaitCharge) { next=Stage::Charge; }
        // A wake is sent once only; after its timeout ask for status, never send another wake.
        retry(next,now); return;
    }
    if(c.attempts>=8) { finish(VehicleResultCode::Failed); return; }
    for(unsigned transitions=0;transitions<3 && count_;++transitions) {
        switch(c.stage) {
        case Stage::VcsecAuth:
            if(authenticate(Domain::Vcsec,Stage::WaitVcsecAuth,c.kind==Kind::Wake && !c.wake_sent?Stage::Wake:Stage::Status,now)) { continue; } return;
        case Stage::InfoAuth:
            if(authenticate(Domain::Infotainment,Stage::WaitInfoAuth,Stage::Charge,now)) { continue; } return;
        case Stage::Status: encrypted(Domain::Vcsec,status_payload(),Stage::WaitStatus,now); return;
        case Stage::Charge: encrypted(Domain::Infotainment,get_charge_state_payload(),Stage::WaitCharge,now); return;
        case Stage::Wake: {
            VcsecPacket wake;
            if(c.wake_sent || !wake_payload(c.policy==WakePolicy::WakeIfNeeded,wake)) { finish(VehicleResultCode::Failed); return; }
            encrypted(Domain::Vcsec,{wake.bytes.data(),wake.length},Stage::WaitWake,now); return;
        }
        case Stage::Pair: {
            IdentityLease identity; VcsecPacket pair;
            if(!keys_.acquire(identity) || !pair_charging_manager(identity,pair) || !advance()) { finish(VehicleResultCode::Failed); return; }
            ++c.attempts; Transmission tx{this,ports_,serial_,generation_,now,c.started,c.timing};
            const bool accepted=transmit({pair.bytes.data(),pair.length},tx);
            if(serial_!=tx.serial) { return; }
            if(accepted) {
                pair_context_=c.context; pair_started_=ports_.now(ports_.context); pair_tx_started_=tx.started;
                pair_reply_pending_=pair_reply_terminal_=false;
                drain(tx); // Includes notifications synchronously deferred during accepted TX.
                if(serial_==tx.serial) { finish(VehicleResultCode::PairRequestSent); }
            }
            else { retry(Stage::Pair,now); drain(tx); } return;
        }
        default: return;
        }
    }
}
void Vehicle::emitted(void *p,const Frame &incoming)
{
    auto &v=*static_cast<Vehicle *>(p); const uint32_t now=v.ports_.now(v.ports_.context);
    if(v.frames_seen_!=UINT32_MAX) { ++v.frames_seen_; }
    if(v.transmission_) {
        if(v.deferred_count_==v.deferred_.size()) {
            if(!v.deferred_overflow_ && v.deferred_overflows_!=UINT32_MAX) { ++v.deferred_overflows_; }
            v.deferred_overflow_=true; return;
        }
        v.deferred_[v.deferred_count_++]={incoming,now}; return;
    }
    v.frame(incoming,now);
}
bool Vehicle::receive(Bytes bytes,uint64_t generation)
{
    if(exhausted() || !connected_ || generation!=generation_) { return false; }
    return receiver_.receive(bytes,ports_.now(ports_.context),generation,emitted,this);
}
void Vehicle::frame(const Frame &incoming,uint32_t now)
{
    if(exhausted() || !connected_) { return; }
    VcsecStatus observed;
    // Window [PRESENT_KEY TX start, acceptance + 60s): a reply deferred while the
    // final request fragment awaited its ATT response carries a receive time
    // before acceptance and must not underflow out of the window.
    if(pair_context_ && now-pair_tx_started_<(pair_started_-pair_tx_started_)+60000u && ports_.owns(ports_.context,generation_) &&
       decode_pairing_reply({incoming.data.data(),incoming.length},{address_.data(),16},observed)) {
        if(pair_replies_seen_!=UINT32_MAX) { ++pair_replies_seen_; }
        // First terminal wins, including when multiple frames arrive in one RX
        // batch. A late WAIT must not hide a specific refusal.
        if(!pair_reply_terminal_) {
            pair_reply_=observed; pair_reply_pending_=true;
            pair_reply_terminal_=observed.operation!=VcsecOperation::Waiting;
        }
        return;
    }
    if(!count_) { return; }
    auto &c=commands_[0]; if(now-c.started>=c.timing.total_ms) { return; }
    SessionWireReply hello;
    if(decode_session_reply({incoming.data.data(),incoming.length},{address_.data(),16},hello)) {
        if(session_replies_seen_!=UINT32_MAX) { ++session_replies_seen_; }
        const Stage expected=hello.domain==Domain::Vcsec?Stage::WaitVcsecAuth:Stage::WaitInfoAuth;
        auto &ch=channel(hello.domain); SessionInfo decoded;
        if(c.stage!=expected) {
            Request request;
            const bool charge=c.published==Stage::WaitCharge;
            // The active guard was published only after full framed TX acceptance.
            // Recheck current time as well as the captured RX time: deferred RX
            // cannot synchronize state after a write callback has spent the budget.
            const uint32_t current=ports_.now(ports_.context);
            if(!c.has_request || hello.domain!=(charge?Domain::Infotainment:Domain::Vcsec) || !ch.guard ||
               !ch.guard->pending_request(request) || now-request.transmitted>=request.response_ms ||
               now-request.started>=request.total_ms) { return; }
            const auto result=ch.authentication.receive_update(hello.view(generation_),request,current);
            if(result==SessionAdmission::Rejected) { return; }
            ch.guard->cancel(); c.has_request=false;
            if(result==SessionAdmission::KeyDenied) { finish(VehicleResultCode::KeyDenied); return; }
            ch.clock_anchor=now;
            ch.high_water=std::max(ch.high_water,ch.authentication.info().counter);
            c.refresh=false;
            // A possibly exposed wake is never resent. Synchronization shares the
            // original eight-attempt/total budget and the ordinary retry backoff.
            retry(charge?Stage::Charge:Stage::Status,current); return;
        }
        const bool clock_update=decode_session_info({hello.encoded.data(),hello.length},decoded) &&
            (!ch.authentication.ready() || decoded.epoch!=ch.authentication.info().epoch || decoded.clock>=ch.authentication.info().clock);
        const auto result=ch.authentication.receive(hello.view(generation_),now,ports_.random);
        if(result==SessionAdmission::KeyDenied) { finish(VehicleResultCode::KeyDenied); return; }
        if(result!=SessionAdmission::Established && result!=SessionAdmission::Updated) { return; }
        if(clock_update) { ch.clock_anchor=now; }
        ch.high_water=std::max(ch.high_water,ch.authentication.info().counter);
        if(!ch.guard) { ch.guard.emplace(*ch.authentication.crypto(),hello.domain,Bytes{vin_.data(),17}); }
        c.refresh=false; c.delayed=false;
        c.stage=hello.domain==Domain::Vcsec?(c.kind==Kind::Wake && !c.wake_sent?Stage::Wake:Stage::Status):Stage::Charge; return;
    }
    Response response;
    if(!c.has_request || !decode_encrypted_response({incoming.data.data(),incoming.length},{address_.data(),16},response)) { return; }
    const bool charge=c.published==Stage::WaitCharge;
    if(response.domain!=(charge?Domain::Infotainment:Domain::Vcsec)) { return; }
    auto &ch=channel(response.domain); std::array<uint8_t,1024> plaintext{}; VerifiedReply verified;
    if(!ch.guard || !ch.guard->accept(response,now,generation_,plaintext,verified,charge?validate_charge:validate_status)) { return; }
    c.has_request=false;
    if(verified.fault) {
        if(verified.fault==5 || verified.fault==6 || verified.fault==15 || verified.fault==17 || verified.fault==26) {
            c.refresh=true; retry(charge?Stage::InfoAuth:Stage::VcsecAuth,now);
        } else if(verified.fault==1 || verified.fault==2 || verified.fault==11) { retry(charge?Stage::Charge:Stage::Status,now); }
        else { finish(VehicleResultCode::Failed); } return;
    }
    if(charge) {
        uint8_t battery=0;
        if(decode_charge_state({plaintext.data(),verified.length},battery)) { finish(VehicleResultCode::Soc,battery,SleepState::Unknown,true); }
        else { finish(VehicleResultCode::Failed); } return;
    }
    VcsecStatus status;
    if(!decode_vcsec_status({plaintext.data(),verified.length},status) || status.kind!=VcsecKind::Vehicle || status.sleep==SleepState::Unknown) {
        finish(VehicleResultCode::Failed); return;
    }
    if(c.kind==Kind::Status) { finish(VehicleResultCode::Status,0,status.sleep,true); return; }
    if(c.kind==Kind::Wake) {
        if(status.sleep==SleepState::Awake) { finish(VehicleResultCode::WakeConfirmed,0,status.sleep,true); }
        else { retry(Stage::Status,now); } return;
    }
    if(status.sleep==SleepState::Awake) { c.stage=Stage::InfoAuth; c.delayed=false; }
    else if(c.policy==WakePolicy::NoWakeSkip) { finish(VehicleResultCode::SkippedAsleep,0,status.sleep,true); }
    else if(!c.wake_sent) { c.stage=Stage::Wake; c.delayed=false; }
    else { retry(Stage::Status,now); }
}
}
