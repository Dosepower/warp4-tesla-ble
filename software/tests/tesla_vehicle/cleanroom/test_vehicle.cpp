// SPDX-License-Identifier: Apache-2.0
#include "controller_fixture.hpp"
#include <cstdio>
using namespace controller_fixture;
int main()
{
    for(uint64_t generation:{uint64_t{5},uint64_t{UINT32_MAX},(uint64_t{1}<<32)+5,UINT64_MAX}) {
        Harness h; h.link=generation; std::vector<VehicleResult> results;
        assert(h.vehicle.open(h.link));
        assert(h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,77,h.callback(results)));
        h.vehicle.tick(); assert(h.last().hello && h.last().domain==Domain::Vcsec);
        const auto challenge=h.last();
        h.inject(h.hello(challenge,false,7,1234,true)); assert(results.empty());
        h.inject(h.hello(challenge),h.link^(uint64_t{1}<<32),7); assert(results.empty());
        h.establish(); auto status=h.last(); assert(!status.hello && status.domain==Domain::Vcsec && status.plaintext==std::vector<uint8_t>({0x0a,0}));
        h.inject(h.response(status,{awake,sizeof(awake)},1,0,true)); h.vehicle.tick(); assert(h.last().uuid==status.uuid && results.empty());
        h.inject(h.response(status,{awake,sizeof(awake)},1)); h.vehicle.tick(); assert(h.last().hello && h.last().domain==Domain::Infotainment);
        h.establish(); auto query=h.last(); assert(query.domain==Domain::Infotainment && query.counter==8 && query.plaintext==std::vector<uint8_t>({0x12,4,0x0a,2,0x12,0}));
        auto reply=h.response(query,{soc,sizeof(soc)}); h.inject(reply); h.inject(reply); h.vehicle.tick();
        assert(results.size()==1 && results[0].code==VehicleResultCode::Soc && results[0].battery==72 && results[0].authenticated && results[0].context==77 && results[0].generation==generation && h.vehicle.queued()==0);
        assert(h.vehicle.high_water(Domain::Vcsec)==8 && h.vehicle.high_water(Domain::Infotainment)==8);
    }
    // Routine no-wake poll does not even begin Infotainment when asleep.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,1,h.callback(results))); h.vehicle.tick(); h.establish();
        auto status=h.last(); h.inject(h.response(status,{asleep,sizeof(asleep)})); h.vehicle.tick();
        assert(results.size()==1 && results[0].code==VehicleResultCode::SkippedAsleep && results[0].authenticated && h.sent.size()==2);
        assert(!h.vehicle.wake(false,2,h.callback(results)) && !h.vehicle.pair(false,3,h.callback(results)));
    }
    // Finite fixed-capacity FIFO and total time include queueing; disconnect retires once.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        for(unsigned i=1;i<=4;++i) { assert(h.vehicle.status(i,h.callback(results),{1000,200,100})); }
        assert(!h.vehicle.status(5,h.callback(results)) && h.vehicle.queued()==4);
        h.clock+=1000; for(unsigned i=0;i<4;++i) { h.vehicle.tick(); }
        assert(results.size()==4 && h.writes==0 && h.vehicle.queued()==0);
        for(auto &r:results) { assert(r.code==VehicleResultCode::Expired); }
        results.clear(); assert(h.vehicle.status(6,h.callback(results))); h.vehicle.tick();
        h.vehicle.close(h.link); h.vehicle.close(h.link); assert(results.size()==1 && results[0].code==VehicleResultCode::Disconnected && !h.vehicle.connected());
        assert(!h.vehicle.open(h.link)); h.link+=(uint64_t{1}<<32); assert(h.vehicle.open(h.link));
    }
    std::puts("Clean public Vehicle: fragmented both-domain SessionInfo/status/GetChargeState to authenticated SoC, full-width ownership, bad HMAC/GCM refusal, no-wake skip, fixed queue/deadline and disconnect once passed");
}
