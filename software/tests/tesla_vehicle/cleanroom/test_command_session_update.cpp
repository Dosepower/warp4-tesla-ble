// SPDX-License-Identifier: Apache-2.0
#define main preserved_astra_confirmation_main
#include "astra/protocol_probe_confirm.cpp"
#undef main

static Frame session_frame(Harness &h,const Sent &s,const SessionInfo &data,bool bad=false)
{
    ProtoWriter<512> body; ProtoWriter<24> to,from; ProtoWriter<40> mac;
    ProtoWriter<48> signature; ProtoWriter<1024> out;
    assert(body.integer(1,data.counter) && body.block(2,{data.remote_public.data(),65}) &&
        body.block(3,{data.epoch.data(),16}) && body.fixed32(4,data.clock) &&
        body.integer(5,data.status) && body.integer(6,data.handle));
    auto tag=session_tag(body.bytes(),{s.uuid.data(),16}); if(bad) { tag[0]^=1; }
    assert(to.block(2,{h.address.data(),16}) && from.integer(1,static_cast<uint32_t>(s.domain)) &&
        mac.block(1,{tag.data(),32}) && signature.block(6,mac.bytes()));
    assert(out.block(6,to.bytes()) && out.block(7,from.bytes()) && out.block(15,body.bytes()) &&
        out.block(13,signature.bytes()) && out.block(50,{s.uuid.data(),16}));
    Frame f; auto bytes=out.bytes(); f.length=bytes.size; std::memcpy(f.data.data(),bytes.data,bytes.size); return f;
}
static SessionInfo initial_info()
{
    SessionInfo data; assert(decode_session_info({vectors::encoded_info,sizeof(vectors::encoded_info)},data)); return data;
}
static bool same(const SessionInfo &a,const SessionInfo &b)
{
    return a.remote_public==b.remote_public && a.epoch==b.epoch && a.counter==b.counter &&
        a.clock==b.clock && a.status==b.status && a.handle==b.handle;
}
struct AuthLink {
    ChallengeView challenge;
    static bool send(void *p,const ChallengeView &c) { static_cast<AuthLink *>(p)->challenge=c; return true; }
};
static void direct_admission(Domain domain)
{
    Harness h; DomainAuthentication auth(domain,{vectors::vin,17}); AuthLink link;
    const uint64_t generation=0x100000005ULL;
    OperationTime op{1,generation,100,10000,6000};
    assert(auth.begin(h.keys,op,100,h.rng.get(),{AuthLink::send,&link})==ChallengeResult::Sent);
    Sent s; s.uuid=link.challenge.uuid; s.domain=domain;
    auto wire=h.hello(s); SessionWireReply decoded;
    assert(decode_session_reply({wire.data.data(),wire.length},{h.address.data(),16},decoded));
    assert(auth.receive(decoded.view(generation),101,h.rng.get())==SessionAdmission::Established);
    const auto original=auth.info(); const auto *crypto=auth.crypto();
    IdentityLease identity; assert(auth.identity(identity));
    Request request; request.uuid=s.uuid; request.uuid[0]^=0x40; request.context=2;
    request.generation=generation; request.started=100; request.transmitted=200;
    request.total_ms=10000; request.response_ms=6000; s.uuid=request.uuid;
    auto fresh=original; fresh.counter=100; fresh.clock=2000;
    for(unsigned mode=0;mode<10;++mode) {
        auto candidate=fresh; auto sent=s; auto r=request; uint64_t g=generation; uint32_t now=201;
        if(mode==1) { sent.uuid[0]^=1; }
        if(mode==2) { sent.domain=domain==Domain::Vcsec?Domain::Infotainment:Domain::Vcsec; }
        if(mode==3) { g=5; }
        if(mode==4) { now=5200; } // hard five-second recent-UUID horizon
        if(mode==5) { r.response_ms=1; }
        if(mode==6) { r.total_ms=101; }
        if(mode==7) { candidate.clock=original.clock-1; candidate.counter=1; }
        if(mode==8) { candidate.remote_public[64]^=1; } // valid old-peer MAC, foreign key
        wire=session_frame(h,sent,candidate,mode==0);
        assert(decode_session_reply({wire.data.data(),wire.length},{h.address.data(),16},decoded));
        auto view=decoded.view(g); if(mode==9) { view.encoded.size=0; }
        assert(auth.receive_update(view,r,now)==SessionAdmission::Rejected);
        assert(same(auth.info(),original) && auth.crypto()==crypto && auth.ready() && !auth.pending());
        IdentityLease retained; assert(auth.identity(retained) && retained.same(identity));
    }
    wire=session_frame(h,s,fresh); assert(decode_session_reply({wire.data.data(),wire.length},{h.address.data(),16},decoded));
    assert(auth.receive_update(decoded.view(generation),request,201)==SessionAdmission::Updated);
    assert(auth.info().counter==100 && auth.info().clock==2000 && auth.crypto()==crypto);
    // Same-epoch lower peer counter cannot roll back the local counter. The
    // official reference permits its newer clock; counter publication is clamped.
    fresh.counter=2; fresh.clock=2001; wire=session_frame(h,s,fresh);
    assert(decode_session_reply({wire.data.data(),wire.length},{h.address.data(),16},decoded));
    assert(auth.receive_update(decoded.view(generation),request,202)==SessionAdmission::Updated && auth.info().counter==100);
    // A new opaque epoch can carry a lower clock, but high-water never decreases.
    fresh.epoch[0]^=1; fresh.clock=1; wire=session_frame(h,s,fresh);
    assert(decode_session_reply({wire.data.data(),wire.length},{h.address.data(),16},decoded));
    assert(auth.receive_update(decoded.view(generation),request,203)==SessionAdmission::Updated);
    const auto changed=auth.info(); assert(changed.clock==1 && changed.counter==100);
    fresh.epoch=original.epoch; fresh.counter=101; fresh.clock=3000; wire=session_frame(h,s,fresh);
    assert(decode_session_reply({wire.data.data(),wire.length},{h.address.data(),16},decoded));
    assert(auth.receive_update(decoded.view(generation),request,204)==SessionAdmission::Rejected && same(auth.info(),changed));
    // Fixed epoch-history exhaustion refuses without evicting older epochs.
    for(unsigned i=2;i<=8;++i) {
        fresh.epoch=original.epoch; fresh.epoch[0]^=static_cast<uint8_t>(i); fresh.clock=i;
        wire=session_frame(h,s,fresh); assert(decode_session_reply({wire.data.data(),wire.length},{h.address.data(),16},decoded));
        assert(auth.receive_update(decoded.view(generation),request,205+i)==SessionAdmission::Updated);
    }
    const auto full=auth.info(); fresh.epoch[0]^=0x80; wire=session_frame(h,s,fresh);
    assert(decode_session_reply({wire.data.data(),wire.length},{h.address.data(),16},decoded));
    assert(auth.receive_update(decoded.view(generation),request,220)==SessionAdmission::Rejected && same(auth.info(),full));
    auth.reset(); assert(auth.receive_update(decoded.view(generation),request,221)==SessionAdmission::Rejected && !auth.ready());
}
static void public_negatives(bool info)
{
    for(unsigned mode=0;mode<7;++mode) {
        Harness h; std::vector<VehicleResult> out; assert(h.vehicle.open(h.link)); start(h,out,info,1);
        const auto sent=h.last(); const auto high=h.vehicle.high_water(sent.domain);
        const auto other=h.vehicle.high_water(info?Domain::Vcsec:Domain::Infotainment);
        auto data=initial_info(); data.counter=100; data.clock=2000; auto bad=sent;
        if(mode==1) { bad.uuid[0]^=1; }
        if(mode==2) { bad.domain=info?Domain::Vcsec:Domain::Infotainment; }
        if(mode==3) { data.clock=1; data.counter=1; }
        if(mode==4) { data.remote_public[64]^=1; }
        if(mode==5) { h.clock+=500; } // exact response deadline
        const auto frame=session_frame(h,bad,data,mode==0);
        if(mode==6) { h.inject(frame,h.link+(uint64_t{1}<<32),7); }
        else { h.inject(frame); }
        assert(out.empty() && h.vehicle.high_water(sent.domain)==high &&
            h.vehicle.high_water(info?Domain::Vcsec:Domain::Infotainment)==other);
        if(mode==5) { h.vehicle.tick(); h.clock+=100; h.vehicle.tick(); }
        h.inject(reply(h,h.last(),info?Bytes{soc,sizeof(soc)}:Bytes{awake,sizeof(awake)},1));
        assert(out.size()==1 && out[0].authenticated);
    }
}
static void lifecycle(bool info)
{
    // Updates arriving inside TX are staged until acceptance; refused complete
    // sends and delayed callbacks cannot publish synchronization state.
    for(unsigned mode=0;mode<4;++mode) {
        Harness h; std::vector<VehicleResult> out; assert(h.vehicle.open(h.link)); start(h,out,info,1);
        const auto previous=h.last(); const auto high=h.vehicle.high_water(previous.domain);
        h.clock+=500; h.vehicle.tick(); h.clock+=100;
        h.accept_final=mode!=1;
        h.on_frame= mode==2 ? +[](Harness &p) {
            p.inject(synchronization_error(p,p.last())); p.clock+=500;
        } : mode==3 ? +[](Harness &p) {
            auto stale=synchronization_error(p,p.last()); auto old=p.link;
            p.vehicle.close(old); p.link+=(uint64_t{1}<<32); assert(p.vehicle.open(p.link));
            p.inject(stale,old,7);
        } : +[](Harness &p) {
            auto domain=p.last().domain; auto before=p.vehicle.high_water(domain);
            p.inject(synchronization_error(p,p.last())); p.vehicle.tick();
            assert(p.vehicle.high_water(domain)==before); p.accepted=p.accept_final;
        };
        h.vehicle.tick();
        if(mode==0) {
            assert(out.empty() && h.vehicle.high_water(previous.domain)==100);
            auto duplicate=synchronization_error(h,h.last()); h.inject(duplicate);
            h.clock+=99; h.vehicle.tick(); assert(h.last().counter==high+1);
            h.clock+=1; h.vehicle.tick(); assert(h.last().counter==101);
            h.inject(reply(h,previous,info?Bytes{soc,sizeof(soc)}:Bytes{awake,sizeof(awake)},1)); assert(out.empty());
            h.inject(reply(h,h.last(),info?Bytes{soc,sizeof(soc)}:Bytes{awake,sizeof(awake)},1)); assert(out.size()==1 && out[0].authenticated);
        } else if(mode==3) {
            assert(out.size()==1 && out[0].code==VehicleResultCode::Disconnected);
            assert(h.vehicle.high_water(previous.domain)==high+1); start(h,out,info,2);
            h.inject(reply(h,h.last(),info?Bytes{soc,sizeof(soc)}:Bytes{awake,sizeof(awake)},1));
            assert(out.size()==2 && out.back().generation==h.link && out.back().authenticated);
        } else { assert(out.empty() && h.vehicle.high_water(previous.domain)==high+1); }
    }
    // Repeated valid updates still consume the original operation's attempt cap.
    Harness h; std::vector<VehicleResult> out; assert(h.vehicle.open(h.link)); start(h,out,info,1);
    for(unsigned i=0;i<10 && out.empty();++i) {
        auto data=initial_info(); data.counter=100+i; data.clock=2000+i;
        h.inject(session_frame(h,h.last(),data)); h.clock+=100; h.vehicle.tick();
    }
    assert(out.size()==1 && out[0].code==VehicleResultCode::Failed && h.sent.size()<=8);
}
static void wake_and_reentry()
{
    Harness h; std::vector<VehicleResult> out; assert(h.vehicle.open(h.link));
    assert(h.vehicle.wake(true,1,h.callback(out),{10000,500,500})); h.vehicle.tick(); h.establish();
    auto wake=h.last(); assert(wake.plaintext==std::vector<uint8_t>({0x10,30}));
    h.inject(synchronization_error(h,wake)); h.clock+=100; h.vehicle.tick();
    assert(h.last().counter==101 && h.last().plaintext==std::vector<uint8_t>(status_payload().data,status_payload().data+status_payload().size));
    h.inject(reply(h,h.last(),{awake,sizeof(awake)},1)); assert(out.size()==1 && out[0].code==VehicleResultCode::WakeConfirmed);
    // Completion reentry preserves successor correlation and reused counter 1.
    VehicleCompletion cb; assert(cb.assign([&](const VehicleResult &r) {
        out.push_back(r); assert(h.vehicle.status(3,h.callback(out))); h.vehicle.tick();
    }));
    assert(h.vehicle.status(2,std::move(cb))); h.vehicle.tick(); auto old=h.last();
    h.inject(reply(h,old,{awake,sizeof(awake)},1)); auto successor=h.last();
    assert(successor.uuid!=old.uuid); h.inject(reply(h,old,{awake,sizeof(awake)},1)); assert(out.size()==2);
    h.inject(reply(h,successor,{awake,sizeof(awake)},1)); assert(out.size()==3 && out.back().context==3);
    // Terminal full-width generation stays terminal; low-word aliases do not own it.
    h.vehicle.close(h.link); h.link=UINT64_MAX; assert(h.vehicle.open(h.link));
    assert(!h.vehicle.receive({},UINT32_MAX)); h.vehicle.close(h.link); assert(!h.vehicle.open(0));
}
int main()
{
    direct_admission(Domain::Vcsec); direct_admission(Domain::Infotainment);
    for(bool info:{false,true}) { public_negatives(info); lifecycle(info); }
    wake_and_reentry();
    std::puts("Command SessionInfo: both-domain full-state negatives, epoch nonrollback/history exhaustion, five-second recency, TX staging/refusal/delay, disconnect/64-bit alias, bounded recovery, once-only wake and nested reused-response-counter completion passed");
    return 0;
}
