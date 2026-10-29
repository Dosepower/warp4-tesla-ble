// SPDX-License-Identifier: Apache-2.0
// Production SoC poll timing through the real clean Vehicle path: per-phase step
// budgets after a wake versus the pinned reference tesla-ble v6.2.0 (a00f3872)
// include/vehicle.h:160-163,363-381 and src/vehicle.cpp:319-344,1391-1400.
// Synthetic fixture key/VIN; deterministic waking model, not a vehicle recording.
// Build with -DTIMING_BASELINE to run the same scenarios with the previous
// production timing {40000,5000,10000} (baseline RED evidence).
#include "controller_fixture.hpp"
#include <cstdio>
#include <functional>
#include <set>
using namespace controller_fixture;
namespace {
const uint8_t wake_plain[]={0x10,30};
#ifdef TIMING_BASELINE
constexpr VehicleTiming soc_timing{40000,5000,10000};
constexpr uint32_t phase_ms=40000;
constexpr unsigned max_uuids=3; // previous per-operation challenge cap
#else
constexpr VehicleTiming soc_timing=production_soc_timing;
constexpr uint32_t phase_ms=session_auth_phase_ms;
constexpr unsigned max_uuids=session_info_max_challenges;
#endif
struct Copy { uint32_t at; std::array<uint8_t,16> uuid; };
struct Run {
    Harness h; std::vector<VehicleResult> results;
    unsigned wakes=0,counter=0; uint32_t wake_at=0,awake_at=0,start=0;
    std::vector<Copy> info; // every infotainment session request copy on the wire
    std::function<bool()> awake=[]{return false;};
    std::function<bool(const Sent &)> on_sent=[](const Sent &){return true;}; // false: no reply
    size_t seen=0;
    static bool is_wake(const Sent &s) { return !s.hello && s.domain==Domain::Vcsec && s.plaintext.size()==2 && !std::memcmp(s.plaintext.data(),wake_plain,2); }
    void pump(uint32_t until) {
        while(h.clock<until && results.empty()) {
            h.vehicle.tick();
            while(seen<h.sent.size()) {
                const Sent s=h.sent[seen++];
                if(s.hello && s.domain==Domain::Infotainment) { info.push_back({h.clock,s.uuid}); }
                if(!on_sent(s)) { continue; }
                if(s.hello) { h.inject(h.hello(s)); continue; }
                if(s.domain==Domain::Infotainment) { h.inject(h.response(s,{soc,sizeof(soc)},++counter)); continue; }
                if(is_wake(s)) { ++wakes; wake_at=h.clock; continue; }
                const bool a=awake(); if(a && !awake_at) { awake_at=h.clock; }
                h.inject(h.response(s,{a?controller_fixture::awake:asleep,sizeof(asleep)},++counter));
            }
            h.clock+=20;
        }
    }
    uint32_t elapsed() const { return h.clock-start; }
};
void expect(bool ok,const char *what) { if(!ok) { std::printf("FAIL: %s\n",what); std::fflush(stdout); std::abort(); } }
// VCSEC awake `wake_ms` after the single wake; the infotainment ignores every session
// request during its first `ignore_ms` after the first one (vehicle.h:160-163 model).
void waking(Run &r,uint32_t wake_ms,uint32_t ignore_ms)
{
    r.awake=[&r,wake_ms]{return r.wake_at && r.h.clock-r.wake_at>=wake_ms;};
    r.on_sent=[&r,ignore_ms](const Sent &s) {
        if(s.hello && s.domain==Domain::Infotainment) { return r.h.clock-r.info.front().at>=ignore_ms; }
        return true;
    };
}
bool start(Run &r) { if(!r.h.vehicle.open(r.h.link)) { return false; } r.start=r.h.clock; return r.h.vehicle.charge_state_poll(WakePolicy::WakeIfNeeded,1,r.h.callback(r.results),soc_timing); }
const char *code(const Run &r) { static char b[48]; std::snprintf(b,sizeof b,"%d/%s",r.results.empty()?-1:static_cast<int>(r.results[0].code),
    r.results.empty()?"-":vehicle_failure_name(r.results[0].diagnostics.failure)); return b; }
}
int main(int argc,char **argv)
{
    const char *only=argc>1?argv[1]:nullptr;
    auto run=[only](const char *n){ return !only || !std::strcmp(only,n); };
    // A) Slow wake (12 s) + infotainment ignoring requests for 18 s (> old 15.6 s
    //    synthetic abort): the infotainment phase has its own 25 s budget from
    //    authenticated awake, so the SoC arrives in the same operation, one wake.
    if(run("A")) {
        Run r; waking(r,12000,18000); assert(start(r)); r.pump(r.start+120000);
        std::printf("A: result %s after %u ms, wake->awake %u ms, info copies %zu\n",code(r),r.elapsed(),r.awake_at-r.wake_at,r.info.size());
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::Soc && r.results[0].battery==72,"A: >15.6 s infotainment delay within 25 s yields SoC");
        expect(r.results[0].wake_exposed && r.results[0].awake_after_wake && r.wakes==1 && r.results[0].diagnostics.info_answered,"A: one wake, authenticated awake, info answered");
        expect(r.elapsed()<=soc_timing.total_ms,"A: inside the absolute bound");
    }
    // B) Late answer near the end of the infotainment phase (23 s): still a fresh
    //    challenge on the air, admitted.
    if(run("B")) {
        Run r; waking(r,3000,23000); assert(start(r)); r.pump(r.start+120000);
        std::printf("B: result %s after %u ms\n",code(r),r.elapsed());
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::Soc,"B: answer at 23 s of the 25 s infotainment phase admitted");
    }
    // C) Infotainment never answers: identical copies every 1 s, a new challenge once
    //    4 s old, bounded copies/UUIDs, and the phase ends at awake + 25 s with
    //    challenge_cap (never an unbounded auth loop, never a second wake).
    if(run("C")) {
        Run r; waking(r,2000,UINT32_MAX); assert(start(r)); r.pump(r.start+200000);
        std::set<std::array<uint8_t,16>> uuids; for(auto &c:r.info) { uuids.insert(c.uuid); }
        const uint32_t info_span=r.info.empty()?0:r.h.clock-r.info.front().at;
        std::printf("C: result %s after %u ms, info copies %zu uuids %zu, phase span %u ms\n",code(r),r.elapsed(),r.info.size(),uuids.size(),info_span);
        expect(r.results.size()==1 && r.results[0].code!=VehicleResultCode::Soc && r.results[0].awake_after_wake && r.wakes==1,"C: no SoC, awake evidence, one wake");
        expect(r.results[0].diagnostics.failure==VehicleFailure::ChallengeCap && r.results[0].diagnostics.stage==VehicleStage::WaitInfoAuth,"C: ends by the infotainment phase budget");
        expect(info_span>=phase_ms-1100 && info_span<=phase_ms+100,"C: infotainment phase lasts its own 25 s budget");
        expect(r.info.size()>=20 && r.info.size()<=1+phase_ms/1000,"C: copies at the 1 s transport interval, bounded");
        expect(uuids.size()>=5 && uuids.size()<=max_uuids,"C: renewed challenges, bounded");
        for(size_t i=1;i<r.info.size();++i) {
            expect(r.info[i].at-r.info[i-1].at>=1000,"C: copies never closer than 1 s");
            if(r.info[i].uuid==r.info[i-1].uuid) {
                size_t first=i; while(first>0 && r.info[first-1].uuid==r.info[i].uuid) { --first; }
                expect(r.info[i].at-r.info[first].at<session_info_max_latency_ms,"C: identical copies only while fresh (4 s)");
            }
        }
        expect(r.elapsed()<=soc_timing.total_ms,"C: inside the absolute bound");
    }
    // D) Never awake: one wake, authenticated status probes every 1 s, the awake wait
    //    ends after its own 25 s budget (wake_poll_cap), no second wake, no info request.
    if(run("D")) {
        Run r; assert(start(r)); r.pump(r.start+200000);
        std::printf("D: result %s after %u ms (wake +%u ms)\n",code(r),r.elapsed(),r.h.clock-r.wake_at);
        expect(r.results.size()==1 && r.wakes==1 && r.results[0].wake_exposed && !r.results[0].awake_after_wake && r.info.empty(),"D: one wake, no awake evidence");
        expect(r.results[0].diagnostics.failure==VehicleFailure::WakePollCap && r.h.clock-r.wake_at<=phase_ms+100,"D: awake wait bounded by its phase");
    }
    // E) Stale challenge reply: a valid reply to the FIRST infotainment challenge
    //    delivered after it was renewed (> 4 s) is never admitted; the current one is.
    if(run("E")) {
        Run r; std::vector<Sent> hellos; bool replayed=false;
        r.awake=[&r]{return r.wake_at && r.h.clock-r.wake_at>=1000;};
        r.on_sent=[&](const Sent &s) {
            if(!(s.hello && s.domain==Domain::Infotainment)) { return true; }
            hellos.push_back(s);
            if(r.h.clock-r.info.front().at<6000) { return false; }
            if(!replayed) {
                replayed=true; r.h.inject(r.h.hello(hellos.front())); r.h.vehicle.tick();
                expect(r.results.empty() && hellos.front().uuid!=s.uuid,"E: renewed challenge, stale reply not terminal");
                for(size_t i=r.seen;i<r.h.sent.size();++i) { expect(r.h.sent[i].hello || r.h.sent[i].domain!=Domain::Infotainment,"E: stale reply admitted no charge request"); }
            }
            return true;
        };
        assert(start(r)); r.pump(r.start+120000);
        std::printf("E: result %s\n",code(r));
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::Soc && r.results[0].diagnostics.rejected_replies>=1,"E: stale UUID rejected, fresh one admitted");
    }
    // F) Foreign link generation and cancellation: frames for another generation are
    //    refused; a close during the infotainment phase ends it as Disconnected.
    if(run("F")) {
        Run r; waking(r,1000,UINT32_MAX); assert(start(r));
        r.pump(r.start+8000); expect(r.results.empty() && !r.info.empty(),"F: in infotainment phase");
        expect(!r.h.vehicle.receive({r.h.sent.back().frame.data.data(),r.h.sent.back().frame.length},r.h.link+1),"F: foreign generation refused");
        r.h.vehicle.close(r.h.link);
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::Disconnected && r.results[0].awake_after_wake,"F: cancel ends bounded, keeps evidence");
        std::printf("F: foreign generation refused in infotainment phase; close -> result %s, awake evidence kept\n",code(r));
    }
#ifndef TIMING_BASELINE
    // H) A late session fault refresh (fault 15 on the charge reply) opens a new
    //    infotainment session phase, but that phase cannot extend the absolute
    //    deadline: slow VCSEC session (23 s), slow wake (23 s) and slow infotainment
    //    (23 s) push the refresh past total - phase; the operation then ends Expired
    //    exactly at the absolute bound, not at refresh + 25 s.
    if(run("H")) {
        Run r; waking(r,23000,23000); uint32_t refresh_at=0; bool faulted=false;
        auto info_gate=r.on_sent;
        r.on_sent=[&](const Sent &s) {
            if(s.hello && s.domain==Domain::Vcsec) { return r.h.clock-r.start>=23000; }
            if(s.hello && s.domain==Domain::Infotainment) { return !faulted && info_gate(s); }
            if(s.domain==Domain::Infotainment && !faulted) {
                faulted=true; refresh_at=r.h.clock; r.h.inject(r.h.response(s,{soc,sizeof(soc)},++r.counter,15)); return false;
            }
            return true;
        };
        assert(start(r)); r.pump(r.start+200000);
        std::printf("H: result %s after %u ms, refresh at %u ms\n",code(r),r.elapsed(),refresh_at-r.start);
        expect(faulted && refresh_at-r.start+phase_ms>soc_timing.total_ms,"H: refresh phase would end beyond the absolute bound");
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::Expired && r.wakes==1,"H: ends Expired, one wake");
        expect(r.elapsed()>=soc_timing.total_ms && r.elapsed()<=soc_timing.total_ms+20,"H: ends at the absolute bound, not extended by the refresh phase");
    }
    // I) Awake-wait phase enforced at RX admission, not only in tick(): production
    //    (tesla_vehicle.cpp worker) dispatches RX before the next tick. A status probe
    //    sent before the 25 s awake phase ends whose authentic Awake answer arrives at
    //    wake + 25001 ms (inside the 5 s response and 90 s absolute windows, before any
    //    tick) must not open a fresh infotainment phase. Variants: immediate RX and RX
    //    deferred during the probe's own TX (captured inside, dispatched after the phase
    //    end); in-window positive controls at wake + 24999 ms; forged tag, foreign link
    //    generation and an older probe's UUID stay refused; legacy phase_ms==0 unchanged.
    if(run("I")) {
        enum class Rx { Immediate, Deferred, Forged, Foreign, OldUuid };
        struct Case { const char *name; Rx rx; uint32_t answer; bool legacy; bool soc; };
        const Case cases[]={
            {"late-immediate",Rx::Immediate,phase_ms+1,false,false},
            {"late-deferred",Rx::Deferred,phase_ms+1,false,false},
            {"edge-immediate",Rx::Immediate,phase_ms-1,false,true},
            {"edge-deferred",Rx::Deferred,phase_ms-1,false,true},
            {"edge-forged",Rx::Forged,phase_ms-1,false,false},
            {"edge-foreign-generation",Rx::Foreign,phase_ms-1,false,false},
            {"edge-old-uuid",Rx::OldUuid,phase_ms-1,false,false},
            {"legacy-phase0-late",Rx::Immediate,phase_ms+1,true,true},
        };
        static Run *active=nullptr; static uint32_t deferred_answer=0,deferred_sent_at=0; static Sent deferred_probe;
        const char *only_case=argc>2?argv[2]:nullptr; // optional single variant (RED evidence per case)
        for(const auto &k:cases) {
            if(only_case && std::strcmp(only_case,k.name)) { continue; }
            Run r; active=&r; bool held=false,armed=false; Sent probe,previous; uint32_t probe_at=0;
            r.on_sent=[&](const Sent &s) {
                if(s.hello || s.domain!=Domain::Vcsec || Run::is_wake(s) || !r.wake_at) { return true; }
                if(held) { return false; }
                if(r.h.clock-r.wake_at>=phase_ms-1000) { held=true; probe=s; probe_at=r.h.clock; return false; }
                previous=s; return true;
            };
            VehicleTiming timing=soc_timing; if(k.legacy) { timing.phase_ms=0; }
            assert(r.h.vehicle.open(r.h.link)); r.start=r.h.clock;
            assert(r.h.vehicle.charge_state_poll(WakePolicy::WakeIfNeeded,1,r.h.callback(r.results),timing));
            while(!held && r.results.empty() && r.h.clock-r.start<200000) {
                if(k.rx==Rx::Deferred && !armed && r.wake_at && r.h.clock-r.wake_at>=phase_ms-1000) {
                    // Next frame on the wire is the boundary probe: answer it while its
                    // TX is still in flight (deferred RX), then move the clock on.
                    armed=true; deferred_answer=r.wake_at+k.answer;
                    r.h.on_frame=[](Harness &h) {
                        deferred_probe=h.sent.back(); deferred_sent_at=h.clock;
                        h.inject(h.response(deferred_probe,{controller_fixture::awake,sizeof(controller_fixture::awake)},++active->counter));
                        h.clock=deferred_answer;
                    };
                }
                r.pump(r.h.clock+20);
            }
            if(k.rx==Rx::Deferred) { probe_at=deferred_sent_at; }
            expect(held && r.results.empty() && r.wakes==1 && r.info.empty(),"I: boundary probe held inside the awake phase");
            expect(probe_at-r.wake_at<phase_ms,"I: probe sent before the phase end");
            const size_t sent_before=r.h.sent.size();
            if(k.rx!=Rx::Deferred) {
                if(r.h.clock<r.wake_at+k.answer) { r.h.clock=r.wake_at+k.answer; }
                expect(r.h.clock==r.wake_at+k.answer,"I: answer at the intended boundary time");
                const Sent &target=k.rx==Rx::OldUuid?previous:probe;
                const Frame f=r.h.response(target,{controller_fixture::awake,sizeof(controller_fixture::awake)},++r.counter,0,k.rx==Rx::Forged);
                if(k.rx==Rx::Foreign) { r.h.inject(f,r.h.link+1,7); } else { r.h.inject(f); }
            } else {
                expect(deferred_probe.uuid==probe.uuid && r.h.clock-r.wake_at>=k.answer,"I: deferred answer captured during the probe TX");
            }
            // Before any tick: the late answer must not have advanced to the infotainment phase.
            const bool late=!k.legacy && k.answer>=phase_ms;
            if(late) {
                r.h.vehicle.tick();
                for(size_t i=sent_before;i<r.h.sent.size();++i) { expect(r.h.sent[i].domain!=Domain::Infotainment,"I: no infotainment request after a late awake answer"); }
            }
            r.pump(r.h.clock+120000);
            std::printf("I %s: probe at wake+%u ms, answer at wake+%u ms -> result %s after %u ms, info copies %zu\n",
                k.name,probe_at-r.wake_at,k.answer,code(r),r.elapsed(),r.info.size());
            expect(r.results.size()==1 && r.wakes==1,"I: one result, one wake");
            if(k.soc) {
                expect(r.results[0].code==VehicleResultCode::Soc && r.results[0].awake_after_wake && !r.info.empty(),"I: in-window authentic awake answer admitted -> SoC");
            } else {
                expect(r.results[0].code!=VehicleResultCode::Soc && !r.results[0].awake_after_wake && r.info.empty(),"I: no infotainment phase, no SoC");
                expect(r.results[0].diagnostics.failure==VehicleFailure::WakePollCap,"I: ends by the awake-wait phase budget");
                expect(r.h.clock-r.wake_at<=phase_ms+100 || !late,"I: ends at the awake phase end");
            }
            expect(r.elapsed()<=soc_timing.total_ms+20,"I: inside the absolute bound");
            active=nullptr;
        }
    }
    // G) Phase bookkeeping cannot restart the absolute deadline or move backwards.
    if(run("G")) {
        OperationTime t{9,5,1000,90000,5000,1000,25000};
        expect(t.phase_open(25999) && !t.phase_open(26000),"G: phase end");
        t.phase_started=80000; expect(t.phase_open(90999) && !t.phase_open(91000),"G: phase never beyond the absolute deadline");
        t.phase_started=0xFFFFFF00u; expect(!t.phase_open(500),"G: wrapped/foreign phase start closed");
    }
#endif
    std::puts("Session phase timing: per-phase budgets after wake, 1 s resend, 4 s renewal, bounded copies, absolute bound");
}
