// SPDX-License-Identifier: Apache-2.0
#include "controller_fixture.hpp"
#include <cstdio>
using namespace controller_fixture;
int main() {
    unsigned cases=0;
    for(auto domain:{Domain::Vcsec,Domain::Infotainment}) {
        Harness h; assert(h.vehicle.open(h.link)); std::vector<VehicleResult> results;
        assert(h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,1,h.callback(results))); h.vehicle.tick(); h.establish();
        if(domain==Domain::Infotainment) {
            h.inject(h.response(h.last(),{awake,sizeof(awake)},1)); h.vehicle.tick(); h.establish();
        }
        const auto request=h.last(); assert(request.domain==domain && !request.hello);
        for(const auto &bad: {std::vector<uint8_t>{255},std::vector<uint8_t>{10,1,255}}) {
            h.inject(h.response(request,{bad.data(),bad.size()},100));
            assert(results.empty() && h.vehicle.queued()==1); // No replay/publication on malformed inner decode.
        }
        const auto good=h.response(request,domain==Domain::Vcsec?Bytes{awake,sizeof(awake)}:Bytes{soc,sizeof(soc)},100);
        h.inject(good); h.inject(good);
        if(domain==Domain::Vcsec) {
            h.vehicle.tick(); h.establish(); h.inject(h.response(h.last(),{soc,sizeof(soc)},100));
        }
        assert(results.size()==1 && results[0].authenticated && results[0].battery==72);
        ++cases;
    }
    std::printf("Independent malformed-inner admission: %u domains retain request/replay on authenticated malformed outer/nested plaintext, then accept valid same-counter response exactly once\n",cases);
}
