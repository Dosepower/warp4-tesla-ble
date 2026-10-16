// SPDX-License-Identifier: Apache-2.0
// Independent observable schedules through the public clean controller only.
#include "controller_fixture.hpp"
#include <cstdio>
using namespace controller_fixture;
static uint32_t reply_counter=100;
static bool enqueue(Harness &h,Domain d,uint64_t id,VehicleCompletion &&cb) {
    if(d==Domain::Vcsec) { return h.vehicle.status(id,std::move(cb),{20000,200,100}); }
    return h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,id,std::move(cb),{20000,200,100});
}
static void finish(Harness &h,unsigned &done,unsigned wanted) {
    for(unsigned n=0;n<24 && done<wanted;++n) {
        h.vehicle.tick(); auto s=h.last();
        if(s.hello) { h.inject(h.hello(s)); }
        else { h.inject(h.response(s,s.domain==Domain::Vcsec?Bytes{awake,sizeof(awake)}:Bytes{soc,sizeof(soc)},++reply_counter)); }
    }
    assert(done==wanted);
}
static void prerequisite(Harness &h,Domain d) {
    if(d==Domain::Infotainment) {
        h.vehicle.tick(); h.establish(); h.inject(h.response(h.last(),{awake,sizeof(awake)},++reply_counter));
    }
}
static void refusal(Domain d,bool recovery,bool persistent) {
    Harness h; assert(h.vehicle.open(h.link)); unsigned first=0,next=0;
    VehicleCompletion a,b;
    assert(a.assign([&](const VehicleResult &r) { ++first; assert(r.authenticated!=persistent); }));
    assert(b.assign([&](const VehicleResult &r) { ++next; assert(r.authenticated); }));
    assert(enqueue(h,d,1,std::move(a)) && enqueue(h,d,2,std::move(b)));
    prerequisite(h,d);
    if(recovery) {
        h.vehicle.tick(); h.establish(); auto original=h.last(); assert(original.domain==d && !original.hello);
        h.inject(h.response(original,d==Domain::Vcsec?Bytes{awake,sizeof(awake)}:Bytes{soc,sizeof(soc)},++reply_counter,15));
        h.inject(h.response(original,d==Domain::Vcsec?Bytes{awake,sizeof(awake)}:Bytes{soc,sizeof(soc)},++reply_counter,15));
        h.clock+=100;
    }
    const auto water=h.vehicle.high_water(d); h.accepted=false; h.vehicle.tick();
    assert(first==0 && next==0 && h.vehicle.queued()==2 && h.vehicle.high_water(d)==water);
    const auto writes=h.writes; h.clock+=99;
    for(unsigned n=0;n<32;++n) { h.vehicle.tick(); assert(h.writes==writes && first==0); }
    ++h.clock;
    if(persistent) {
        for(unsigned n=0;n<12 && !first;++n) { h.vehicle.tick(); h.clock+=100; }
        assert(first==1 && next==0 && h.vehicle.queued()==1 && h.vehicle.high_water(d)==water);
    }
    h.accepted=true;
    if(!persistent) { finish(h,first,1); }
    finish(h,next,1); h.vehicle.tick(); assert(first==1 && next==1 && h.vehicle.queued()==0);
}
static void denial(Domain d,unsigned mode) {
    Harness h; assert(h.vehicle.open(h.link)); unsigned first=0,next=0,old_next=0;
    VehicleCompletion a,b;
    assert(a.assign([&](const VehicleResult &r) {
        assert(r.code==VehicleResultCode::KeyDenied); ++first;
        if(mode==2) {
            h.vehicle.close(h.link); ++h.link; assert(h.vehicle.open(h.link));
            VehicleCompletion fresh; assert(fresh.assign([&](const VehicleResult &v) { assert(v.authenticated); ++next; }));
            assert(enqueue(h,d,3,std::move(fresh)));
        }
        if(mode) { h.vehicle.tick(); }
    }));
    assert(b.assign([&](const VehicleResult &r) {
        if(mode==2) { assert(r.code==VehicleResultCode::Disconnected); ++old_next; }
        else { assert(r.authenticated); ++next; }
    }));
    assert(enqueue(h,d,1,std::move(a)) && enqueue(h,d,2,std::move(b)));
    prerequisite(h,d); h.vehicle.tick(); const auto challenge=h.last(); assert(challenge.hello && challenge.domain==d);
    h.inject(h.hello(challenge,true,7,1234,true)); assert(!first && !next && h.vehicle.queued()==2);
    const auto old=h.hello(challenge,true); h.inject(old); assert(first==1 && !next);
    h.inject(old); h.inject(old); assert(!next);
    finish(h,next,1); assert(first==1 && old_next==unsigned(mode==2));
}
static void nested(Domain d,bool denied,bool inside,unsigned slots) {
    Harness h; assert(h.vehicle.open(h.link)); const auto old_generation=h.link;
    unsigned old=0,middle=0,inner=0,final=0; bool entered=false;
    auto successor=[&]() {
        assert(!entered); entered=true; ++h.link; assert(h.vehicle.open(h.link));
        VehicleCompletion mid; assert(mid.assign([&](const VehicleResult &r) {
            assert(r.code==VehicleResultCode::Disconnected); ++middle;
            ++h.link; assert(h.vehicle.open(h.link));
            VehicleCompletion complete; assert(complete.assign([&](const VehicleResult &v) { assert(v.authenticated); ++inner; }));
            assert(enqueue(h,d,100,std::move(complete))); finish(h,inner,1);
            VehicleCompletion fresh; assert(fresh.assign([&](const VehicleResult &v) { assert(v.authenticated); ++final; }));
            assert(enqueue(h,d,101,std::move(fresh))); h.vehicle.tick();
        }));
        assert(enqueue(h,d,99,std::move(mid))); h.vehicle.tick(); h.vehicle.close(h.link);
    };
    for(unsigned i=0;i<slots;++i) {
        VehicleCompletion cb; assert(cb.assign([&,i](const VehicleResult &r) {
            assert(r.generation==old_generation); assert(r.code==(denied && i==0?VehicleResultCode::KeyDenied:VehicleResultCode::Disconnected)); ++old;
            if(i==0 && denied) { h.vehicle.close(h.link); }
            if(inside && ((denied && i==1) || (!denied && i==0))) { successor(); }
            h.vehicle.tick();
        }));
        assert(enqueue(h,d,i+1,std::move(cb)));
    }
    prerequisite(h,d); h.vehicle.tick(); const auto challenge=h.last(); assert(challenge.hello);
    if(denied) { h.inject(h.hello(challenge,true,7,1234,true)); assert(!old); h.inject(h.hello(challenge,true)); }
    else { h.vehicle.close(h.link); }
    if(!inside) { successor(); }
    assert(old==slots && middle==1 && inner==1 && final==0 && h.vehicle.queued()==1);
    h.inject(h.hello(challenge,denied)); assert(final==0);
    finish(h,final,1); assert(old==slots && middle==1 && inner==1 && final==1);
}
int main() {
    unsigned r7=0,r8=0;
    for(auto d:{Domain::Vcsec,Domain::Infotainment}) {
        for(bool recovery:{false,true}) for(bool persistent:{false,true}) { refusal(d,recovery,persistent); ++r7; }
        for(unsigned mode=0;mode<3;++mode) { denial(d,mode); ++r7; }
        for(bool denied:{false,true}) for(bool inside:{false,true}) for(unsigned slots:{2u,4u}) { nested(d,denied,inside,slots); ++r8; }
    }
    std::printf("Public combined R7: %u both-domain initial/recovery one-shot/persistent refusal and denial/queued-successor schedules; R8: %u nested drains with inner authenticated success and final successor passed\n",r7,r8);
}
