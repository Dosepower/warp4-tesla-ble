// SPDX-License-Identifier: Apache-2.0
// Clean controller wake contract: exposure reporting and post-wake status pacing.
// Synthetic fixture key/VIN; deterministic sleep model, not a vehicle recording.
#include "controller_fixture.hpp"
#include <cstdio>
#include <functional>
using namespace controller_fixture;
namespace {
const uint8_t wake_plain[]={0x10,30};
struct Run {
    Harness h; std::vector<VehicleResult> results;
    unsigned wakes=0,statuses=0,post_wake=0,counter=0; uint32_t wake_at=0; std::vector<uint32_t> post_wake_at;
    std::function<bool()> awake=[]{return false;};
    std::function<bool(const Sent &)> on_sent=[](const Sent &){return true;}; // false: no reply
    size_t seen=0;
    static bool is_wake(const Sent &s) { return !s.hello && s.domain==Domain::Vcsec && s.plaintext.size()==2 && !std::memcmp(s.plaintext.data(),wake_plain,2); }
    void pump(uint32_t until) {
        while(h.clock<until && results.empty()) {
            h.vehicle.tick();
            while(seen<h.sent.size()) {
                const Sent s=h.sent[seen++];
                if(!on_sent(s)) { continue; }
                if(s.hello) { h.inject(h.hello(s)); continue; }
                if(s.domain==Domain::Infotainment) { h.inject(h.response(s,{soc,sizeof(soc)},++counter)); continue; }
                if(is_wake(s)) { ++wakes; wake_at=h.clock; continue; } // no decodable vehicle-status reply
                ++statuses; if(wake_at) { ++post_wake; post_wake_at.push_back(h.clock); }
                h.inject(h.response(s,{awake()?controller_fixture::awake:asleep,sizeof(asleep)},++counter));
            }
            h.clock+=20;
        }
    }
};
void expect(bool ok,const char *what) { if(!ok) { std::printf("FAIL: %s\n",what); std::fflush(stdout); std::abort(); } }
}
int main()
{
    const VehicleTiming soc_timing{40000,5000,10000}; // unchanged WARP manual SoC timing
    { // Awake vehicle: no wake on the wire.
        Run r; r.awake=[]{return true;}; assert(r.h.vehicle.open(r.h.link));
        assert(r.h.vehicle.charge_state_poll(WakePolicy::WakeIfNeeded,1,r.h.callback(r.results),soc_timing)); r.pump(r.h.clock+40000);
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::Soc && !r.results[0].wake_exposed && r.wakes==0,"awake: Soc without wake exposure");
    }
    { // Asleep, no-wake policy: skipped, nothing exposed.
        Run r; assert(r.h.vehicle.open(r.h.link));
        assert(r.h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,1,r.h.callback(r.results),soc_timing)); r.pump(r.h.clock+40000);
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::SkippedAsleep && !r.results[0].wake_exposed && r.wakes==0,"no-wake skip");
    }
    { // Slow wake: VCSEC awake 9 s after the wake; paced probes, one wake, SoC.
        Run r; r.awake=[&r]{return r.wake_at && r.h.clock-r.wake_at>=9000;}; assert(r.h.vehicle.open(r.h.link));
        const uint32_t start=r.h.clock;
        assert(r.h.vehicle.charge_state_poll(WakePolicy::WakeIfNeeded,1,r.h.callback(r.results),soc_timing)); r.pump(start+41000);
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::Soc && r.results[0].battery==72 && r.results[0].wake_exposed,"slow wake delivers SoC");
        expect(r.wakes==1,"exactly one wake");
        for(size_t i=1;i<r.post_wake_at.size();++i) { expect(r.post_wake_at[i]-r.post_wake_at[i-1]>=Vehicle::wake_status_interval_ms,"post-wake probe pacing"); }
        std::printf("slow wake: post-wake probes=%u soc after %u ms\n",r.post_wake,r.h.clock-start);
    }
    { // Never awake: one wake, bounded probes, Expired exactly at the unchanged total deadline.
        Run r; assert(r.h.vehicle.open(r.h.link)); const uint32_t start=r.h.clock;
        assert(r.h.vehicle.charge_state_poll(WakePolicy::WakeIfNeeded,1,r.h.callback(r.results),soc_timing)); r.pump(start+60000);
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::Expired && r.results[0].wake_exposed,"never awake: Expired, exposed");
        expect(r.wakes==1 && r.post_wake<=Vehicle::max_wake_status_polls && r.h.clock-start<=40000+40,"one wake, bounded, at deadline");
        std::printf("never awake: post-wake probes=%u ended after %u ms\n",r.post_wake,r.h.clock-start);
    }
    // Waking infotainment: VCSEC awake 2 s after the wake, but the infotainment ignores
    // session requests for a while after its first one (synthetic model of the documented
    // behaviour, yoziru TeslaBLE v6.2.0 include/vehicle.h:158-163; real durations unknown).
    auto info_boot=[&](uint32_t ignore_ms,Run &r,unsigned &ignored) {
        uint32_t info_first=0;
        r.awake=[&r]{return r.wake_at && r.h.clock-r.wake_at>=2000;};
        r.on_sent=[&r,&ignored,info_first,ignore_ms](const Sent &s) mutable {
            if(s.hello && s.domain==Domain::Infotainment) {
                if(!info_first) { info_first=r.h.clock; }
                if(r.h.clock-info_first<ignore_ms) { ++ignored; return false; }
            }
            return true;
        };
        assert(r.h.vehicle.open(r.h.link)); const uint32_t start=r.h.clock;
        assert(r.h.vehicle.charge_state_poll(WakePolicy::WakeIfNeeded,1,r.h.callback(r.results),soc_timing)); r.pump(start+60000);
        std::printf("info boot %u ms: ignored infotainment session requests=%u result=%d after %u ms\n",ignore_ms,ignored,
                    r.results.empty()?-1:static_cast<int>(r.results[0].code),r.h.clock-start);
        return r.h.clock-start;
    };
    { // Short infotainment delay (8 s): SoC in the same operation, awake evidence reported.
        Run r; unsigned ignored=0; const uint32_t took=info_boot(8000,r,ignored);
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::Soc && r.results[0].battery==72 && r.results[0].wake_exposed &&
               r.results[0].awake_after_wake && r.wakes==1 && ignored>=1 && took<=40000+40,"short infotainment delay: SoC, woken evidence, one wake");
    }
    { // Long infotainment delay (20 s): the unchanged per-operation session-challenge cap
      // (3 per domain) ends the operation without SoC, but with authenticated awake
      // evidence and exactly one wake. Recovery is a separate later operation without a
      // wake (firmware post-wake follow-up), never a second wake or a raised auth budget.
        Run r; unsigned ignored=0; const uint32_t took=info_boot(20000,r,ignored);
        expect(r.results.size()==1 && r.results[0].code!=VehicleResultCode::Soc && r.results[0].wake_exposed && r.results[0].awake_after_wake &&
               r.wakes==1 && ignored==3 && took<=40000+40,"long infotainment delay: no SoC, woken evidence, one wake, bounded");
    }
    { // Never awake: wake exposed, but no awake evidence (honest "wake sent", not "woken").
        Run r; assert(r.h.vehicle.open(r.h.link));
        assert(r.h.vehicle.charge_state_poll(WakePolicy::WakeIfNeeded,1,r.h.callback(r.results),soc_timing)); r.pump(r.h.clock+60000);
        expect(r.results.size()==1 && r.results[0].wake_exposed && !r.results[0].awake_after_wake,"never awake: no awake evidence");
    }
    { // VCSEC awake, infotainment never answers within the deadline: woken (evidence), no SoC, Expired.
        Run r; r.awake=[&r]{return r.wake_at && r.h.clock-r.wake_at>=1000;};
        r.on_sent=[](const Sent &s){ return !(s.hello && s.domain==Domain::Infotainment); };
        assert(r.h.vehicle.open(r.h.link)); const uint32_t start=r.h.clock;
        assert(r.h.vehicle.charge_state_poll(WakePolicy::WakeIfNeeded,1,r.h.callback(r.results),soc_timing)); r.pump(start+60000);
        expect(r.results.size()==1 && r.results[0].code!=VehicleResultCode::Soc && r.results[0].wake_exposed && r.results[0].awake_after_wake &&
               r.wakes==1 && r.h.clock-start<=40000+40,"silent infotainment: woken evidence, no SoC, bounded by the unchanged deadline");
    }
    { // Independent probe cap even with a much longer total budget.
        Run r; assert(r.h.vehicle.open(r.h.link));
        assert(r.h.vehicle.charge_state_poll(WakePolicy::WakeIfNeeded,1,r.h.callback(r.results),{200000,5000,10000})); r.pump(r.h.clock+200000);
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::Failed && r.results[0].wake_exposed && r.wakes==1 &&
               r.post_wake==Vehicle::max_wake_status_polls,"probe cap");
    }
    { // Link closed while waiting for the wake: exposed.
        Run r; assert(r.h.vehicle.open(r.h.link));
        r.on_sent=[&r](const Sent &s){ if(Run::is_wake(s)) { ++r.wakes; r.h.vehicle.close(r.h.link); return false; } return true; };
        assert(r.h.vehicle.charge_state_poll(WakePolicy::WakeIfNeeded,1,r.h.callback(r.results),soc_timing)); r.pump(r.h.clock+40000);
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::Disconnected && r.results[0].wake_exposed && r.wakes==1,"close in WaitWake: exposed");
    }
    { // Link closed before any wake frame: definitely not exposed.
        Run r; assert(r.h.vehicle.open(r.h.link));
        r.on_sent=[&r](const Sent &s){ if(!s.hello && s.domain==Domain::Vcsec) { r.h.vehicle.close(r.h.link); return false; } return true; };
        assert(r.h.vehicle.charge_state_poll(WakePolicy::WakeIfNeeded,1,r.h.callback(r.results),soc_timing)); r.pump(r.h.clock+40000);
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::Disconnected && !r.results[0].wake_exposed && r.wakes==0,"close before wake: not exposed");
    }
    { // Wake frame refused after its first fragment: exposed, link retired, never resent.
        Run r; assert(r.h.vehicle.open(r.h.link)); r.h.recoverable=false;
        r.on_sent=[&r](const Sent &s){
            // The next write after the first (asleep) status is wake fragment 1; refuse all later writes.
            if(!s.hello && s.domain==Domain::Vcsec && !Run::is_wake(s) && !r.statuses) { r.h.hook=[](Harness &h){ h.accepted=false; }; }
            return true;
        };
        assert(r.h.vehicle.charge_state_poll(WakePolicy::WakeIfNeeded,1,r.h.callback(r.results),soc_timing)); r.pump(r.h.clock+40000);
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::Disconnected && r.results[0].wake_exposed,"partial wake: exposed");
        expect(r.wakes==0 && r.statuses==1 && !r.h.vehicle.connected() && r.h.recoveries==1,"partial wake frame never completed, link retired");
    }
    std::puts("Clean Vehicle wake contract: exposure reported for sent/partial/closed wakes only, paced bounded post-wake probes, single wake, unchanged deadlines passed");
}
