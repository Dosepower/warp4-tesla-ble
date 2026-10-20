// SPDX-License-Identifier: Apache-2.0
// Independent public-port R7/R8 equivalents; no legacy fixture or private access.
#include "controller_fixture.hpp"
#include <cstdio>
using namespace controller_fixture;
static void reach(Harness &h,Domain target,std::vector<VehicleResult> &results,uint64_t context) {
    assert(h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,context,h.callback(results)));
    h.vehicle.tick();
    if(target==Domain::Infotainment) {
        h.establish(); assert(!h.last().hello);
        h.inject(h.response(h.last(),{awake,sizeof(awake)})); h.vehicle.tick();
    }
    assert(h.last().hello && h.last().domain==target);
}
static void denial(Domain domain) {
    Harness h; assert(h.vehicle.open(h.link)); std::vector<VehicleResult> old,fresh;
    reach(h,domain,old,1); const Sent challenge=h.last();
    h.inject(h.hello(challenge,true,7,1234,true)); assert(old.empty());
    const Frame denied=h.hello(challenge,true); h.inject(denied);
    assert(old.size()==1 && old[0].code==VehicleResultCode::KeyDenied);
    assert(h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,2,h.callback(fresh)));
    h.inject(denied); assert(old.size()==1 && fresh.empty());
    h.vehicle.close(h.link); assert(fresh.size()==1 && fresh[0].code==VehicleResultCode::Disconnected);
    const auto retired=h.link; h.link+=uint64_t{1}<<32; assert(h.vehicle.open(h.link));
    assert(!h.vehicle.receive({denied.data.data(),denied.length},retired));
}
static void refusal(bool persistent) {
    Harness h; assert(h.vehicle.open(h.link)); std::vector<VehicleResult> results;
    h.accepted=false;
    assert(h.vehicle.status(1,h.callback(results),{5000,200,100})); h.vehicle.tick();
    const unsigned first=h.writes; assert(first>0 && results.empty());
    h.clock+=99; h.vehicle.tick(); assert(h.writes==first && results.empty());
    ++h.clock; if(!persistent) { h.accepted=true; }
    h.vehicle.tick(); assert(h.writes>first);
    if(!persistent) {
        h.establish(); h.inject(h.response(h.last(),{awake,sizeof(awake)}));
        assert(results.size()==1 && results[0].authenticated);
    } else {
        for(unsigned n=0;n<20 && results.empty();++n) { h.clock+=100; h.vehicle.tick(); }
        assert(results.size()==1 && results[0].code==VehicleResultCode::Failed && h.writes<=8);
    }
    assert(results.size()==1);
}
static void nested(unsigned tails) {
    Harness h; assert(h.vehicle.open(h.link)); const uint64_t retired=h.link;
    unsigned completed=0,nested_completed=0; std::vector<VehicleResult> successor;
    for(unsigned n=0;n<tails;++n) {
        VehicleCompletion cb; assert(cb.assign([&,n](const VehicleResult &r) {
            assert(r.code==VehicleResultCode::Disconnected && r.generation==retired); ++completed;
            if(n!=0) { return; }
            h.link+=uint64_t{1}<<32; assert(h.vehicle.open(h.link));
            VehicleCompletion nested; assert(nested.assign([&](const VehicleResult &r) {
                assert(r.code==VehicleResultCode::Disconnected); ++nested_completed;
                h.link+=uint64_t{1}<<32; assert(h.vehicle.open(h.link));
                assert(h.vehicle.status(500,h.callback(successor))); h.vehicle.tick();
                Frame reply=h.hello(h.last()); const uint8_t prefix[]={uint8_t(reply.length>>8)};
                assert(h.vehicle.receive({prefix,1},h.link));
            }));
            assert(h.vehicle.status(100,std::move(nested))); h.vehicle.close(h.link);
        }));
        assert(h.vehicle.status(n+1,std::move(cb)));
    }
    h.vehicle.close(retired);
    assert(completed==tails && nested_completed==1 && h.vehicle.queued()==1 && successor.empty());
    // Old retirement unwind must preserve the new generation's partial RX.
    Frame reply=h.hello(h.last()); std::vector<uint8_t> rest{uint8_t(reply.length)};
    rest.insert(rest.end(),reply.data.begin(),reply.data.begin()+reply.length);
    assert(h.vehicle.receive({rest.data(),rest.size()},h.link)); h.vehicle.tick();
    assert(!h.last().hello); h.inject(h.response(h.last(),{awake,sizeof(awake)}));
    assert(successor.size()==1 && successor[0].authenticated && successor[0].generation==h.link);
}
int main() {
    denial(Domain::Vcsec); denial(Domain::Infotainment);
    refusal(false); refusal(true);
    for(unsigned tails : {1u,4u}) { nested(tails); }
    std::puts("Independent R7/R8: both-domain denial/stale admission; refusal/backoff/exhaustion; nested generation retirement at all relevant 1/4-slot queue boundaries");
}
