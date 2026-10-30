// SPDX-License-Identifier: Apache-2.0
// Event trace ring beyond the saturating 16-bit trace_total, through the real
// clean Vehicle path (public API only: frames in, terminal result out,
// format_vehicle_trace). Synthetic fixture key/VIN; deterministic model.
// A production SoC poll first receives `count` unsolicited plaintext frames
// (no signature_data) while waiting for the VCSEC session reply; each 63
// consecutive frames coalesce into one Unrelated trace word, so 66000 * 63 frames
// record more than 65535 events. The operation then completes normally. The
// latest vehicle_trace_capacity events must be identical to a reference run with
// the same tail but far fewer (< 65535) events: the ring keeps wrapping after the
// diagnostic count saturates and the formatter starts at the oldest retained slot.
// Version-neutral (no ring internals): RED against a ring indexed by trace_total.
#include "controller_fixture.hpp"
#include <cstdio>
#include <string>
using namespace controller_fixture;
namespace {
void expect(bool ok,const char *what) { if(!ok) { std::printf("FAIL: %s\n",what); std::fflush(stdout); std::abort(); } }
// FromVCSECMessage{vehicleStatus{vehicleSleepStatus=AWAKE}}: no signature_data.
Frame broadcast()
{
    ProtoWriter<8> from; ProtoWriter<64> out;
    assert(from.integer(1,2) && out.block(7,from.bytes()) && out.block(10,{awake,sizeof(awake)}));
    Frame f; auto wire=out.bytes(); f.length=wire.size; std::memcpy(f.data.data(),wire.data,wire.size); return f;
}
struct Outcome { VehicleResultCode code; unsigned total; std::string events; };
Outcome run(uint32_t words)
{
    Harness h; std::vector<VehicleResult> results; h.recoverable=false;
    expect(h.vehicle.open(h.link),"open");
    expect(h.vehicle.charge_state_poll(WakePolicy::WakeIfNeeded,1,h.callback(results),production_soc_timing),"start");
    h.vehicle.tick(); expect(h.sent.size()==1 && h.sent[0].hello,"VCSEC session request");
    const Frame noise=broadcast();
    for(uint32_t i=0;i<words*63u+5u;++i) { h.inject(noise); }
    expect(results.empty(),"still running after the unsolicited frames");
    size_t seen=0;
    for(unsigned step=0;step<5000 && results.empty();++step) {
        while(seen<h.sent.size() && results.empty()) {
            const auto &s=h.sent[seen++];
            if(s.hello) { h.inject(h.hello(s)); }
            else if(s.domain==Domain::Infotainment) { h.inject(h.response(s,{soc,sizeof(soc)},unsigned(seen))); }
            else { h.inject(h.response(s,{awake,sizeof(awake)},unsigned(seen))); }
        }
        if(!results.empty()) { break; }
        h.vehicle.tick(); h.clock+=20;
    }
    expect(results.size()==1,"one terminal result");
    std::array<char,600> text{};
    expect(format_vehicle_trace(results[0].diagnostics,text.data(),text.size()),"trace fits");
    const std::string all=text.data(); const auto bar=all.find('|');
    return {results[0].code,unsigned(results[0].diagnostics.trace_total),all.substr(bar+1)};
}
}
int main()
{
    const Outcome reference=run(200), saturated=run(66000);
    std::printf("reference: code %d n=%u events %s\n",int(reference.code),reference.total,reference.events.c_str());
    std::printf("saturated: code %d n=%u events %s\n",int(saturated.code),saturated.total,saturated.events.c_str());
    expect(reference.code==VehicleResultCode::Soc && saturated.code==VehicleResultCode::Soc,"both SoC");
    expect(reference.total>vehicle_trace_capacity && reference.total<UINT16_MAX,"reference wraps without saturating");
    expect(saturated.total==UINT16_MAX,"diagnostic count saturates (> 65535 events recorded)");
    size_t words=1; for(char c:saturated.events) { if(c==' ') { ++words; } }
    expect(words==vehicle_trace_capacity,"full ring reported");
    expect(saturated.events==reference.events,"latest 32 events identical after saturation (ring keeps wrapping, oldest first)");
    std::printf("PASS: >65535 events keep the latest %zu in order\n",vehicle_trace_capacity);
    return 0;
}
