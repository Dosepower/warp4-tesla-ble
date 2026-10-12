// SPDX-License-Identifier: Apache-2.0
#include "controller_fixture.hpp"
#include <cstdio>
using namespace controller_fixture;
int main()
{
    unsigned schedules=0;
    for(uint64_t generation:{uint64_t{5},uint64_t{UINT32_MAX},(uint64_t{1}<<32)+5,UINT64_MAX-2}) for(bool denial:{false,true}) {
        Harness h; h.link=generation; assert(h.vehicle.open(h.link)); assert(h.vehicle.open(h.link));
        std::array<unsigned,4> retired{}; std::vector<VehicleResult> results; Frame fresh_reply;
        auto reconnect=[&]() {
            h.pending.clear(); h.link=generation+1; assert(h.vehicle.open(h.link));
            assert(h.vehicle.status(99,h.callback(results))); h.vehicle.tick(); fresh_reply=h.hello(h.last());
            const uint8_t prefix=static_cast<uint8_t>(fresh_reply.length>>8);
            assert(h.vehicle.receive({&prefix,1},h.link));
            // Stale close cannot erase this successor's prefix, including low-word-equal tokens.
            h.vehicle.close(generation); h.vehicle.close(h.link^(uint64_t{1}<<32));
        };
        for(unsigned i=0;i<4;++i) {
            VehicleCompletion cb; assert(cb.assign([&,i](const VehicleResult &r) {
                assert(++retired[i]==1 && r.context==i+1 && r.generation==generation);
                if(i==0) {
                    assert(r.code==(denial?VehicleResultCode::KeyDenied:VehicleResultCode::Disconnected));
                    if(denial) { h.vehicle.close(generation); }
                    else { reconnect(); }
                } else {
                    assert(r.code==VehicleResultCode::Disconnected);
                    if(denial && i==1) { reconnect(); }
                }
                h.vehicle.tick();
            }));
            assert(h.vehicle.status(i+1,std::move(cb)));
        }
        h.vehicle.tick(); auto old=h.last(); auto old_reply=h.hello(old,denial);
        if(denial) { h.inject(h.hello(old,true,7,1234,true)); assert(retired[0]==0); h.inject(old_reply); }
        else { const uint8_t malformed[]={0xff,0xff,0xff}; h.vehicle.receive({malformed,3},generation); h.vehicle.close(generation); }
        for(auto count:retired) { assert(count==1); }
        assert(h.vehicle.queued()==1 && h.vehicle.connected() && h.vehicle.generation()==generation+1);
        std::vector<uint8_t> suffix{static_cast<uint8_t>(fresh_reply.length)};
        suffix.insert(suffix.end(),fresh_reply.data.begin(),fresh_reply.data.begin()+fresh_reply.length);
        assert(!h.vehicle.receive({suffix.data(),suffix.size()},h.link^(uint64_t{1}<<32)));
        assert(h.vehicle.receive({suffix.data(),suffix.size()},h.link)); h.vehicle.tick();
        h.inject(old_reply,generation,7); assert(results.empty());
        h.inject(h.response(h.last(),{awake,sizeof(awake)})); h.vehicle.tick();
        assert(results.size()==1 && results[0].context==99 && results[0].authenticated);
        for(auto count:retired) { assert(count==1); } ++schedules;
    }
    // Disconnect while inside a chunk submission; old continuation cannot submit to its successor.
    for(bool returned:{false,true}) {
        Harness h; std::vector<VehicleResult> old,fresh; assert(h.vehicle.open(h.link));
        VehicleCompletion cb; assert(cb.assign([&](const VehicleResult &r) {
            old.push_back(r); assert(r.code==VehicleResultCode::Disconnected);
            h.pending.clear(); h.link+=(uint64_t{1}<<32); assert(h.vehicle.open(h.link));
            assert(h.vehicle.status(2,h.callback(fresh))); h.vehicle.tick();
        }));
        assert(h.vehicle.status(1,std::move(cb)));
        h.accept_final=returned;
        h.hook=[](Harness &p) { auto old=p.link; p.vehicle.close(old); p.accepted=p.accept_final; };
        h.vehicle.tick(); assert(old.size()==1 && fresh.empty() && h.sent.size()==1 && h.last().hello);
        const unsigned after=h.writes; h.accepted=true; h.inject(h.hello(h.last())); h.vehicle.tick();
        assert(h.writes>after); h.inject(h.response(h.last(),{awake,sizeof(awake)})); assert(fresh.size()==1 && fresh[0].context==2);
    }
    // Bounded staged RX overflow cannot authenticate before TX acceptance.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        h.on_frame=[](Harness &p) { auto reply=p.hello(p.last()); for(unsigned i=0;i<5;++i) { p.inject(reply); } };
        assert(h.vehicle.status(1,h.callback(results))); h.vehicle.tick(); assert(results.empty() && h.recoveries==1);
        h.clock+=100; h.vehicle.tick(); assert(h.last().hello); h.establish(); h.inject(h.response(h.last(),{awake,sizeof(awake)})); assert(results.size()==1);
    }
    // Without proven peer framing recovery, refusal retires the whole old link once.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link)); h.accepted=false; h.recoverable=false;
        for(unsigned i=1;i<=4;++i) { assert(h.vehicle.status(i,h.callback(results))); }
        h.vehicle.tick(); assert(!h.vehicle.connected() && h.writes==1 && results.size()==4 && h.vehicle.queued()==0);
        for(auto &r:results) { assert(r.code==VehicleResultCode::Disconnected); }
        h.vehicle.tick(); assert(h.writes==1 && results.size()==4);
    }
    // UINT64_MAX is usable once; it must never wrap into a reusable owner.
    {
        Harness h; assert(h.vehicle.open(UINT64_MAX)); h.vehicle.close(UINT64_MAX);
        assert(!h.vehicle.open(0) && !h.vehicle.open(UINT64_MAX));
    }
    std::printf("Clean Vehicle R8/full-width related regressions: %u detached old-tail/denial schedules, fresh partial RX preservation, stale cleanup/refusal/overflow ownership and nonwrapping link exhaustion passed\n",schedules);
}
