// SPDX-License-Identifier: Apache-2.0
#include "controller_fixture.hpp"
#include <functional>
#include <cstdio>
using namespace controller_fixture;
struct RotationDisk : KeyStorage {
    SaveResult outcome; std::function<void()> hook; std::vector<uint8_t> committed_bytes;
    SaveResult save(Bytes pem) override {
        if(hook) { auto work=std::move(hook); work(); }
        if(committed(outcome)) { committed_bytes.assign(pem.data,pem.data+pem.size); }
        return outcome;
    }
};
int main() {
    unsigned cases=0;
    for(auto domain:{Domain::Vcsec,Domain::Infotainment})
        for(uint64_t generation:{uint64_t{UINT32_MAX},uint64_t{UINT32_MAX}+1,UINT64_MAX})
        for(bool complete_inside:{false,true})
        for(auto result:{SaveResult::NotCommitted,SaveResult::NotCommittedCleanupPending,SaveResult::Committed,SaveResult::CommittedCleanupPending,SaveResult::RestartRequired}) {
            Harness h; h.link=generation; assert(h.vehicle.open(generation)); std::vector<VehicleResult> done;
            IdentityLease old; assert(h.keys.acquire(old)); std::array<uint8_t,65> old_point; assert(old.public_point(old_point));
            assert(h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,1,h.callback(done))); h.vehicle.tick();
            if(domain==Domain::Infotainment) {
                h.establish(); h.inject(h.response(h.last(),{awake,sizeof(awake)},11)); h.vehicle.tick();
            }
            assert(h.last().hello && h.last().domain==domain);
            const auto pending=h.last(); const auto accepted=h.hello(pending);
            const uint8_t prefix=accepted.length>>8;
            assert(h.vehicle.receive({&prefix,1},generation));
            RotationDisk disk; disk.outcome=result;
            disk.hook=[&]() {
                IdentityLease active; assert(h.keys.acquire(active) && active.same(old));
                if(complete_inside) {
                    std::vector<uint8_t> wire{uint8_t(accepted.length)};
                    wire.insert(wire.end(),accepted.data.begin(),accepted.data.begin()+accepted.length);
                    assert(h.vehicle.receive({wire.data(),wire.size()},generation)); h.vehicle.tick();
                    const auto query=h.last(); assert(!query.hello);
                    const auto signer=field(field(field({query.frame.data.data(),query.frame.length},13),1),1);
                    assert(signer.size==old_point.size() && !std::memcmp(signer.data,old_point.data(),signer.size));
                    h.inject(h.response(query,domain==Domain::Vcsec?Bytes{awake,sizeof(awake)}:Bytes{soc,sizeof(soc)},100));
                    if(domain==Domain::Vcsec) { h.vehicle.tick(); h.establish(); h.inject(h.response(h.last(),{soc,sizeof(soc)},100)); }
                    assert(done.size()==1 && done[0].authenticated && done[0].generation==generation);
                }
            };
            assert(h.keys.rotate(disk,h.rng.get()).committed==KeyStorage::committed(result));
            IdentityLease current; assert(h.keys.acquire(current)); assert(current.same(old)!=KeyStorage::committed(result));
            if(!complete_inside) {
                std::vector<uint8_t> rest{uint8_t(accepted.length)};
                rest.insert(rest.end(),accepted.data.begin(),accepted.data.begin()+accepted.length);
                assert(h.vehicle.receive({rest.data(),rest.size()},generation)); h.vehicle.tick();
                h.inject(h.response(h.last(),domain==Domain::Vcsec?Bytes{awake,sizeof(awake)}:Bytes{soc,sizeof(soc)},100));
                if(domain==Domain::Vcsec) { h.vehicle.tick();
                    // The next domain must use the durable current key; do not
                    // pretend the fixture's scalar-1 response authenticates it.
                    if(KeyStorage::committed(result)) { h.vehicle.close(generation); }
                    else { h.establish(); h.inject(h.response(h.last(),{soc,sizeof(soc)},100)); }
                }
            }
            assert(done.size()==1 && h.vehicle.queued()==0); h.inject(accepted); assert(done.size()==1);
            h.vehicle.close(generation); assert(done.size()==1);
            if(KeyStorage::committed(result)) {
                P256Key persisted; assert(persisted.import_pem({disk.committed_bytes.data(),disk.committed_bytes.size()},h.rng.get()));
                std::array<uint8_t,65> point; assert(persisted.public_point(point)); std::array<uint8_t,65> ram; assert(current.public_point(ram) && point==ram && point!=old_point);
            }
            ++cases;
        }
    std::printf("Independent public key/session composition: %u both-domain low-word-wrap/terminal-generation save outcomes; pending partial RX and leased outgoing identity, in-save HMAC/GCM completion, durable disk/RAM agreement and once-only retirement passed\n",cases);
}
