// SPDX-License-Identifier: Apache-2.0
// External contract: an intentionally absent completion does not prevent admission.
#include "controller_fixture.hpp"
#include <cstdio>
using namespace controller_fixture;
static bool enqueue(Harness &h,unsigned kind,uint64_t context,VehicleTiming timing={}) {
    VehicleCompletion empty;
    switch(kind) {
    case 0: return h.vehicle.status(context,std::move(empty),timing);
    case 1: return h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,context,std::move(empty),timing);
    case 2: return h.vehicle.wake(true,context,std::move(empty),timing);
    default: return h.vehicle.pair(true,context,std::move(empty),timing);
    }
}
int main() {
    for(unsigned kind=0;kind<4;++kind) {
        Harness h; assert(h.vehicle.open(h.link));
        for(unsigned i=1;i<=4;++i) { assert(enqueue(h,kind,i)); }
        assert(h.vehicle.queued()==4 && !enqueue(h,kind,5) && h.sent.empty());
        h.vehicle.close(h.link); assert(h.vehicle.queued()==0 && h.sent.empty());
        ++h.link; assert(h.vehicle.open(h.link));
        assert(enqueue(h,kind,6,{100,50,50})); h.clock+=100; h.vehicle.tick();
        assert(h.vehicle.queued()==0 && h.sent.empty());
        assert(enqueue(h,kind,7)); h.vehicle.tick();
        if(kind==3) { assert(h.last().pair && h.vehicle.queued()==0); }
        else {
            assert(h.last().hello); h.establish();
            h.inject(h.response(h.last(),{awake,sizeof(awake)},1)); h.vehicle.tick();
            if(kind==1) {
                assert(h.last().hello && h.last().domain==Domain::Infotainment); h.establish();
                h.inject(h.response(h.last(),{soc,sizeof(soc)},1));
            }
            assert(h.vehicle.queued()==0);
        }
        assert(!enqueue(h,kind,0));
    }
    // Optional absence is NOT permission to ignore failed callback preparation.
    // Production WARP checks assign's result before invoking an enqueue operation.
    Harness h; assert(h.vehicle.open(h.link)); VehicleCompletion cb;
    Allocation denied; denied.allocate=[](void *,size_t)->void * { return nullptr; };
    const bool ready=cb.assign([](const VehicleResult &) { assert(false); },denied);
    assert(!ready && !(ready && h.vehicle.status(1,std::move(cb))) && h.vehicle.queued()==0);
    std::puts("Clean optional completion: all four operations admit intentional absence, bounded overflow/disconnect/expiry/reconnect/authenticated success; failed preparation remains explicitly gated");
}
