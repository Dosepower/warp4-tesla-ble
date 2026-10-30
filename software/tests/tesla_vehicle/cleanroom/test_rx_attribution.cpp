// SPDX-License-Identifier: Apache-2.0
// Production SoC poll through the real clean Vehicle path with unsolicited and
// late frames. Synthetic fixture key/VIN; deterministic models, not a vehicle
// recording. A reproduced software mechanism is not proof of a field cause.
//
// A: unsolicited plaintext VCSEC status broadcasts (no signature_data; the pinned
//    reference tesla-ble v6.2.0 src/vehicle.cpp:1383-1390 documents bursts of
//    5 in 0.6 s from a waking car, 1191-1197 admits them as broadcasts) arrive
//    while one framed request is being written. They used to fill the 4-slot
//    deferral queue; the overflow aborted the started frame and the production
//    backend (no peer-stream recovery) retired the link: Disconnected.
// B: the same count of reply-shaped (signature-bearing) frames still overflows
//    the unchanged bounded queue: no admission/queue widening.
// Without -DRX_BASELINE additionally:
// C: every identical session request copy is answered 1.5 s late; success, and every
//    rejected reply is classified late by state/window only (no armed session request
//    of that domain, or its window ended): rejected_replies == late_session_replies
//    <= response_timeouts (compare the latest field success 4/4), bounded trace. The
//    classification is unauthenticated; it does not establish that a refused frame
//    is a duplicate answer to an earlier copy (here the model makes it one).
// D: an authenticated VCSEC CommandStatus OK answering the wake request is
//    refused by the status validator (trace: ResponseIgnored/Payload at wake_wait).
// Build with -DRX_BASELINE against the saved pre-change library (RED evidence).
#include "controller_fixture.hpp"
#include <cstdio>
#include <functional>
using namespace controller_fixture;
namespace {
void expect(bool ok,const char *what) { if(!ok) { std::printf("FAIL: %s\n",what); std::fflush(stdout); std::abort(); } }
const uint8_t wake_plain[]={0x10,30};
// FromVCSECMessage{vehicleStatus{vehicleSleepStatus=AWAKE}} routed from VCSEC, no
// to_destination, no request_uuid, no signature_data.
Frame broadcast()
{
    ProtoWriter<8> from; ProtoWriter<64> out;
    assert(from.integer(1,2) && out.block(7,from.bytes()) && out.block(10,{awake,sizeof(awake)}));
    Frame f; auto wire=out.bytes(); f.length=wire.size; std::memcpy(f.data.data(),wire.data,wire.size); return f;
}
struct Due { uint32_t at; Frame frame; };
struct Run {
    Harness h; std::vector<VehicleResult> results; std::vector<Due> due;
    unsigned counter=0,wakes=0; size_t seen=0; uint32_t start=0;
    uint32_t hello_delay=0;           // session reply latency after each copy
    bool asleep_first=false;          // VCSEC asleep until the wake was sent
    bool ack_wake=false;              // answer the wake with an authenticated CommandStatus OK
    static bool is_wake(const Sent &s) { return !s.hello && s.domain==Domain::Vcsec && s.plaintext.size()==2 && !std::memcmp(s.plaintext.data(),wake_plain,2); }
    void reply(const Sent &s) {
        if(s.hello) { due.push_back({h.clock+hello_delay,h.hello(s)}); return; }
        if(s.domain==Domain::Infotainment) { h.inject(h.response(s,{soc,sizeof(soc)},++counter)); return; }
        if(is_wake(s)) {
            ++wakes;
            if(ack_wake) { static const uint8_t ok[]={0x22,0}; h.inject(h.response(s,{ok,sizeof(ok)},++counter)); }
            return;
        }
        const bool a=!asleep_first || wakes;
        h.inject(h.response(s,{a?awake:asleep,sizeof(asleep)},++counter));
    }
    void pump(uint32_t until) {
        while(h.clock<until && results.empty()) {
            for(size_t i=0;i<due.size();) {
                if(int32_t(h.clock-due[i].at)>=0) { const Frame f=due[i].frame; due.erase(due.begin()+long(i)); h.inject(f); } else { ++i; }
            }
            if(!results.empty()) { break; }
            h.vehicle.tick();
            while(seen<h.sent.size() && results.empty()) { reply(h.sent[seen++]); }
            h.clock+=20;
        }
    }
};
bool start(Run &r)
{
    r.h.recoverable=false; // production transport_recover_frame never claims a clean peer stream
    if(!r.h.vehicle.open(r.h.link)) { return false; } r.start=r.h.clock;
    return r.h.vehicle.charge_state_poll(WakePolicy::WakeIfNeeded,1,r.h.callback(r.results),production_soc_timing);
}
// Inject `count` frames from inside the first chunk write of the next framed request.
Run *burst_run=nullptr; unsigned burst_count=0; bool burst_shaped=false;
void burst(Harness &h)
{
    expect(h.vehicle.transmitting(),"burst lands inside an accepted frame transmission");
    for(unsigned i=0;i<burst_count;++i) {
        if(burst_shaped) { h.inject(h.hello(h.sent.front())); } else { h.inject(broadcast()); }
    }
}
const VehicleResult &scenario_burst(Run &r,unsigned count,bool shaped)
{
    expect(start(r),"start");
    r.h.vehicle.tick(); expect(r.h.sent.size()==1 && r.h.sent[0].hello,"VCSEC session request");
    r.seen=1; r.h.inject(r.h.hello(r.h.sent[0]));
    burst_run=&r; burst_count=count; burst_shaped=shaped; r.h.hook=burst; // next write: the status request's first chunk
    r.pump(r.h.clock+90000);
    expect(r.results.size()==1,"one terminal result");
    return r.results[0];
}
}
int main()
{
    {
        Run r; const auto &x=scenario_burst(r,5,false);
        std::printf("A: 5 unsolicited broadcasts during status TX -> code %d failure %s after %lu ms, frames %u\n",
            int(x.code),vehicle_failure_name(x.diagnostics.failure),static_cast<unsigned long>(x.diagnostics.elapsed_ms),unsigned(x.diagnostics.frames));
        expect(x.code==VehicleResultCode::Soc && x.battery==72 && x.authenticated,"A: SoC despite unsolicited burst");
        expect(r.h.vehicle.connected() && r.h.vehicle.deferred_overflows()==0,"A: link kept, no deferral overflow");
#ifndef RX_BASELINE
        expect(x.diagnostics.unrelated_frames==5 && !x.diagnostics.deferred_overflow,"A: broadcasts attributed");
#endif
    }
    {
        Run r; const auto &x=scenario_burst(r,5,true);
        std::printf("B: 5 reply-shaped frames during status TX -> code %d failure %s, overflows %u\n",
            int(x.code),vehicle_failure_name(x.diagnostics.failure),unsigned(r.h.vehicle.deferred_overflows()));
        expect(x.code==VehicleResultCode::Disconnected && r.h.vehicle.deferred_overflows()==1,"B: bounded queue unchanged");
#ifndef RX_BASELINE
        expect(x.diagnostics.deferred_overflow,"B: overflow attributed");
#endif
    }
#ifndef RX_BASELINE
    {
        Run r; r.hello_delay=1500; expect(start(r),"C start"); r.pump(r.h.clock+90000);
        expect(r.results.size()==1,"C result"); const auto &d=r.results[0].diagnostics;
        std::array<char,512> text{}; expect(format_vehicle_trace(d,text.data(),text.size()),"C trace fits");
        std::printf("C: replies 1500 ms after each copy -> code %d after %lu ms attempts %u timeouts %u rejected %u late %u frames %u\n   trace %s\n",
            int(r.results[0].code),static_cast<unsigned long>(d.elapsed_ms),unsigned(d.attempts),unsigned(d.response_timeouts),
            unsigned(d.rejected_replies),unsigned(d.late_session_replies),unsigned(d.frames),text.data());
        expect(r.results[0].code==VehicleResultCode::Soc,"C: SoC");
        // One copy timeout per phase; the model's delayed VCSEC reply arrives during the
        // infotainment phase and is refused by state (classified late, unauthenticated);
        // the delayed infotainment reply arrives only after the result (not counted).
        expect(d.response_timeouts==2 && d.rejected_replies==1 && d.late_session_replies==1,
            "C: every rejection classified late by state/window (not an authenticated duplicate)");
        expect(d.attempts==2 && d.vcsec_answered && d.info_answered,"C: status + charge only");
    }
    {
        Run r; r.asleep_first=true; r.ack_wake=true; expect(start(r),"D start"); r.pump(r.h.clock+90000);
        expect(r.results.size()==1 && r.results[0].code==VehicleResultCode::Soc,"D: SoC after wake");
        const auto &d=r.results[0].diagnostics; bool payload_refused=false; uint32_t wake_tx=0,first_probe=0;
        for(size_t i=0;i<std::min<size_t>(d.trace_total,vehicle_trace_capacity);++i) {
            const uint32_t w=d.trace[i]; const auto ev=static_cast<VehicleTraceEvent>((w>>10)&31); const auto st=static_cast<VehicleStage>((w>>6)&15);
            if(ev==VehicleTraceEvent::ResponseIgnored && st==VehicleStage::WaitWake && (w&63)==uint32_t(ResponseRejection::Payload)) { payload_refused=true; }
            if(ev==VehicleTraceEvent::Request && st==VehicleStage::WaitWake) { wake_tx=w>>15; }
            if(ev==VehicleTraceEvent::Request && st==VehicleStage::WaitStatus && wake_tx && !first_probe) { first_probe=w>>15; }
        }
        std::array<char,512> text{}; format_vehicle_trace(d,text.data(),text.size());
        std::printf("D: authenticated wake CommandStatus OK -> refused by status validator %d; first awake probe %lu ms after wake\n   trace %s\n",
            int(payload_refused),static_cast<unsigned long>(first_probe-wake_tx),text.data());
        expect(payload_refused && first_probe-wake_tx>=production_soc_timing.response_ms,"D: wake reply not admitted, probe after response budget");
    }
#endif
    std::printf("RX attribution: unsolicited frames no longer overflow deferral; bounded queue, admission and wake semantics unchanged\n");
}
