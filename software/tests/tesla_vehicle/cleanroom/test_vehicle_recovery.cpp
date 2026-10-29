// SPDX-License-Identifier: Apache-2.0
#include "controller_fixture.hpp"
#include <cstdio>
using namespace controller_fixture;
// Phased (SoC-style) budgets; the default VehicleTiming is single-phase (legacy pairing/verify path).
static constexpr VehicleTiming phased{5000,500,500,session_auth_phase_ms};
static void status(Harness &h,std::vector<VehicleResult> &results,uint64_t context=1,VehicleTiming timing={})
{
    assert(h.vehicle.status(context,h.callback(results),timing)); h.vehicle.tick();
    if(h.last().hello) { h.establish(); }
    assert(!h.last().hello);
}
int main()
{
    // A refused fragment must not poison the next frame on this transport stream.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        h.accepted=false; assert(h.vehicle.status(1,h.callback(results))); h.vehicle.tick();
        assert(h.recoveries==1 && h.pending.empty());
        h.accepted=true; h.clock+=100; h.vehicle.tick(); h.establish();
        h.inject(h.response(h.last(),{awake,sizeof(awake)})); assert(results.size()==1 && results[0].code==VehicleResultCode::Status);
    }
    // Outgoing high-water must survive both signed refresh and full-width reentry.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link)); status(h,results);
        auto old=h.last(); assert(old.counter==8);
        h.inject(h.response(old,{awake,sizeof(awake)},1,15)); h.clock+=100; h.vehicle.tick();
        auto recovery=h.last(); assert(recovery.hello);
        h.inject(h.hello(recovery,false,2,1235,true)); assert(h.vehicle.high_water(Domain::Vcsec)==8 && results.empty());
        h.inject(h.hello(recovery,false,2,1235)); h.vehicle.tick(); auto fresh=h.last(); assert(fresh.counter==9);
        h.inject(h.response(old,{awake,sizeof(awake)},2)); assert(results.empty());
        h.inject(h.response(fresh,{awake,sizeof(awake)},2)); assert(results.size()==1 && results[0].authenticated);
        h.vehicle.close(h.link); h.link+=(uint64_t{1}<<32); assert(h.vehicle.open(h.link));
        status(h,results,2); assert(h.last().counter==10);
    }
    // Synchronous responses are withheld until the ENTIRE framed send is accepted.
    for(bool accepted:{false,true}) {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        h.accept_final=accepted;
        h.on_frame=[](Harness &p) { p.inject(p.hello(p.last())); assert(p.sent.size()==1); p.vehicle.tick(); assert(p.sent.size()==1); p.accepted=p.accept_final; };
        assert(h.vehicle.status(1,h.callback(results))); h.vehicle.tick(); assert(results.empty());
        h.accepted=true;
        if(!accepted) { h.clock+=100; h.vehicle.tick(); assert(h.last().hello); h.establish(); }
        else { h.vehicle.tick(); assert(!h.last().hello); }
        h.on_frame=[](Harness &p) { p.inject(p.response(p.last(),{awake,sizeof(awake)})); p.vehicle.tick(); };
        // The current status already exists; trigger a timed retry, preserving the operation deadline.
        h.clock+=500; h.vehicle.tick(); h.clock+=100; h.vehicle.tick();
        assert(results.size()==1 && results[0].code==VehicleResultCode::Status && results[0].authenticated);
    }
    // Refused complete encrypted sends consume counters, not request publication or callbacks.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link)); status(h,results);
        h.clock+=500; h.vehicle.tick(); h.clock+=100;
        h.on_frame=[](Harness &p) { p.inject(p.response(p.last(),{awake,sizeof(awake)},1)); p.accepted=false; };
        h.vehicle.tick(); const auto refused=h.last(); assert(refused.counter==9 && h.vehicle.high_water(Domain::Vcsec)==9 && results.empty());
        h.accepted=true; h.clock+=100; h.vehicle.tick(); auto accepted=h.last(); assert(accepted.counter==10);
        h.inject(h.response(refused,{awake,sizeof(awake)},1)); assert(results.empty());
        h.inject(h.response(accepted,{awake,sizeof(awake)},1)); assert(results.size()==1 && results[0].code==VehicleResultCode::Status);
    }
    // Exact transmit/total deadlines checked before and after each fragment.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.status(1,h.callback(results),{300,100,25}));
        h.hook=[](Harness &p) { p.clock+=25; }; h.vehicle.tick(); assert(h.writes==1 && results.empty());
        h.clock=400; h.vehicle.tick(); assert(h.writes==1 && results.size()==1 && results[0].code==VehicleResultCode::Expired);
    }
    // Persistent authentication refusal and mixed authenticated busy/recovery are bounded.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link)); h.accepted=false;
        assert(h.vehicle.status(1,h.callback(results),{5000,200,100}));
        for(unsigned i=0;i<12 && results.empty();++i) { h.pending.clear(); h.vehicle.tick(); h.clock+=100; }
        assert(results.size()==1 && results[0].code==VehicleResultCode::Failed && h.writes==3);
    }
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link)); status(h,results);
        for(unsigned i=0;i<7;++i) {
            auto sent=h.last(); h.inject(h.response(sent,{awake,sizeof(awake)},i+1,1)); h.clock+=100; h.vehicle.tick();
        }
        assert(results.size()==1 && results[0].code==VehicleResultCode::Failed && h.sent.size()==8);
        h.vehicle.tick(); assert(results.size()==1);
    }
    // ---- PHASED (SoC) operations: reference backoff 250 ms x2..8 s, session copies
    // outside the attempt cap. The single-phase blocks above stay on the legacy path.
    for(bool accepted:{false,true}) {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        h.accept_final=accepted;
        h.on_frame=[](Harness &p) { p.inject(p.hello(p.last())); assert(p.sent.size()==1); p.vehicle.tick(); assert(p.sent.size()==1); p.accepted=p.accept_final; };
        assert(h.vehicle.status(1,h.callback(results),phased)); h.vehicle.tick(); assert(results.empty());
        h.accepted=true;
        if(!accepted) { h.clock+=100; h.vehicle.tick(); assert(h.last().hello); h.establish(); }
        else { h.vehicle.tick(); assert(!h.last().hello); }
        h.on_frame=[](Harness &p) { p.inject(p.response(p.last(),{awake,sizeof(awake)})); p.vehicle.tick(); };
        // Response budget (500 ms) spent, then the first request backoff (250 ms).
        h.clock+=500; h.vehicle.tick(); h.clock+=Vehicle::initial_retry_delay_ms; h.vehicle.tick();
        assert(results.size()==1 && results[0].code==VehicleResultCode::Status && results[0].authenticated);
    }
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link)); status(h,results,1,phased);
        // Response timeout -> first request backoff (250 ms); refused send -> second (500 ms).
        h.clock+=500; h.vehicle.tick(); h.clock+=Vehicle::initial_retry_delay_ms;
        h.on_frame=[](Harness &p) { p.inject(p.response(p.last(),{awake,sizeof(awake)},1)); p.accepted=false; };
        h.vehicle.tick(); const auto refused=h.last(); assert(refused.counter==9 && h.vehicle.high_water(Domain::Vcsec)==9 && results.empty());
        h.accepted=true; h.clock+=2*Vehicle::initial_retry_delay_ms-1; h.vehicle.tick(); assert(h.last().counter==9);
        h.clock+=1; h.vehicle.tick(); auto accepted=h.last(); assert(accepted.counter==10);
        h.inject(h.response(refused,{awake,sizeof(awake)},1)); assert(results.empty());
        h.inject(h.response(accepted,{awake,sizeof(awake)},1)); assert(results.size()==1 && results[0].code==VehicleResultCode::Status);
    }
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link)); h.accepted=false;
        assert(h.vehicle.status(1,h.callback(results),{5000,200,100,session_auth_phase_ms}));
        for(unsigned i=0;i<20 && results.empty();++i) { h.pending.clear(); h.vehicle.tick(); h.clock+=100; }
        assert(results.size()==1 && results[0].code==VehicleResultCode::Failed && h.writes<=Vehicle::max_attempts);
    }
    {
        // Busy replies: request retries follow the reference backoff 250 ms x2, capped at
        // 8 s (never earlier), and end at the attempt cap (session request not counted).
        // Total 60 s so that the attempt cap, not the deadline, is exercised.
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.status(1,h.callback(results),{60000,500,500,60000})); h.vehicle.tick();
        if(h.last().hello) { h.establish(); } assert(!h.last().hello);
        for(unsigned i=0;i<Vehicle::max_attempts && results.empty();++i) {
            auto sent=h.last(); const size_t before=h.sent.size(); h.inject(h.response(sent,{awake,sizeof(awake)},i+1,1));
            const uint32_t delay=std::min<uint32_t>(Vehicle::initial_retry_delay_ms<<std::min(i,5u),Vehicle::max_retry_delay_ms);
            h.clock+=delay-1; h.vehicle.tick(); assert(h.sent.size()==before && results.empty());
            h.clock+=1; h.vehicle.tick();
        }
        assert(results.size()==1 && results[0].code==VehicleResultCode::Failed && h.sent.size()==1+Vehicle::max_attempts);
        h.vehicle.tick(); assert(results.size()==1);
    }
    std::puts("Clean Vehicle R7-related regression: high-water across refresh/reconnect, staged synchronous acceptance, refused publication, finite mixed budgets/backoff, stale UUID and per-fragment/total deadlines passed");
}
