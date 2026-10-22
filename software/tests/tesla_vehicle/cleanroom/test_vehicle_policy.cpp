// SPDX-License-Identifier: Apache-2.0
#include "controller_fixture.hpp"
#include <cstdio>
using namespace controller_fixture;
static void reach_status(Harness &h,std::vector<VehicleResult> &results,WakePolicy policy=WakePolicy::WakeIfNeeded)
{
    assert(h.vehicle.open(h.link)); assert(h.vehicle.charge_state_poll(policy,99,h.callback(results)));
    h.vehicle.tick(); h.establish(); assert(!h.last().hello && h.last().domain==Domain::Vcsec);
}
int main()
{
    // A fully exposed wake can act despite a final transport refusal. Do not retransmit it.
    {
        Harness h; std::vector<VehicleResult> results; reach_status(h,results);
        h.inject(h.response(h.last(),{asleep,sizeof(asleep)}));
        h.on_frame=[](Harness &p) { assert(p.last().plaintext==std::vector<uint8_t>({0x10,30})); p.accepted=false; };
        h.vehicle.tick(); assert(h.last().plaintext==std::vector<uint8_t>({0x10,30}));
        h.accepted=true; h.clock+=100; h.vehicle.tick();
        assert(h.last().plaintext==std::vector<uint8_t>({0x0a,0}));
        h.inject(h.response(h.last(),{awake,sizeof(awake)},2)); h.vehicle.tick(); h.establish();
        h.inject(h.response(h.last(),{soc,sizeof(soc)})); assert(results.size()==1 && results[0].code==VehicleResultCode::Soc);
        unsigned wakes=0; for(auto &s:h.sent) { if(s.plaintext==std::vector<uint8_t>({0x10,30})) { ++wakes; } } assert(wakes==1);
    }
    // Reusing a public completion context does not reuse the private authentication budget.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        for(unsigned repeat=0;repeat<2;++repeat) {
            assert(h.vehicle.status(7,h.callback(results))); h.vehicle.tick(); assert(h.last().hello);
            auto pending=h.last(); h.inject(h.hello(pending,true)); assert(results.size()==repeat+1 && results.back().code==VehicleResultCode::KeyDenied);
            h.clock+=200;
        }
    }
    // Manual wake is explicit, exactly one send; completion requires authenticated awake state.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.wake(true,1,h.callback(results))); h.vehicle.tick(); h.establish();
        auto wake=h.last(); assert(wake.plaintext==std::vector<uint8_t>({0x10,30}));
        h.inject(h.response(wake,{awake,sizeof(awake)},1,0,true)); assert(results.empty());
        h.inject(h.response(wake,{awake,sizeof(awake)})); assert(results.size()==1 && results[0].code==VehicleResultCode::WakeConfirmed && results[0].authenticated);
    }
    // Authenticated wake resynchronization must recover via status, not another wake.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.wake(true,1,h.callback(results))); h.vehicle.tick(); h.establish();
        auto wake=h.last(); h.inject(h.response(wake,{awake,sizeof(awake)},1,17));
        h.clock+=100; h.vehicle.tick(); assert(h.last().hello); h.inject(h.hello(h.last())); h.vehicle.tick();
        assert(results.empty() && h.last().plaintext==std::vector<uint8_t>({0x0a,0}));
        h.inject(h.response(h.last(),{awake,sizeof(awake)},2)); assert(results.size()==1 && results[0].code==VehicleResultCode::WakeConfirmed);
        unsigned wakes=0; for(auto &s:h.sent) { if(s.plaintext==std::vector<uint8_t>({0x10,30})) { ++wakes; } } assert(wakes==1);
    }
    // Pair only reports transmission/physical approval needed, never key admission or SoC.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.pair(true,1,h.callback(results))); h.vehicle.tick();
        assert(h.last().pair && results.size()==1 && results[0].code==VehicleResultCode::PairRequestSent && !results[0].authenticated);
    }
    std::puts("Clean Vehicle policy: potentially exposed/refused wake never retried, independent operation budgets, explicit manual wake and pairing transmission-not-admission passed");
}
