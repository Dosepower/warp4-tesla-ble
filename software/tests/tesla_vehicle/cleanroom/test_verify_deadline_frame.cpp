// SPDX-License-Identifier: Apache-2.0
// Field incident 2.13.5+6ae10f8c: pair verification poll {5000,1000,10000}
// with slow ATT write-with-response acknowledgements and no SessionInfo reply.
// The operation's TOTAL deadline must not abandon an already started BLE frame
// mid-stream (that left a framing prefix at the peer, forced recover_frame ->
// backend link retirement = WARP frame_refused/-1, disconnect 0x216 local).
// Official reference teslamotors/vehicle-command a4b43c1e:
//   pkg/connector/ble/ble.go:108-127 Connection.Send ignores its context and
//   writes every block of the length-prefixed frame; only a write error aborts.
// Strictness kept: total deadline still prevents NEW transmissions and late
// acceptance; the per-transmission transmit_ms budget still aborts a frame.
#include "controller_fixture.hpp"
#include <cstdio>
using namespace controller_fixture;
namespace {
uint32_t write_delay=0;
void slow_write(Harness &h) { h.clock+=write_delay; h.hook=slow_write; }
constexpr VehicleTiming verify{5000,1000,10000};
}
int main()
{
    // 1) Production verification timing, 200 ms per ATT write (114 B request =
    //    6 writes = 1.2 s), vehicle silent: third SessionInfoRequest straddles
    //    the 5000 ms operation deadline.
    {
        Harness h; std::vector<VehicleResult> results; write_delay=200; h.recoverable=false; h.hook=slow_write;
        assert(h.vehicle.open(h.link)); const uint32_t start=h.clock;
        assert(h.vehicle.status(21,h.callback(results),verify));
        for(unsigned i=0;i<1000 && results.empty();++i) { h.vehicle.tick(); h.clock+=10; }
        std::printf("frames=%zu writes=%u recoveries=%u partial_bytes=%zu elapsed=%u result=%d\n",h.sent.size(),h.writes,h.recoveries,
            h.pending.size(),h.clock-start,results.empty()?-1:int(results[0].code));
        if(h.recoveries || !h.pending.empty() || results.size()!=1 || results[0].code==VehicleResultCode::Disconnected || !h.vehicle.connected()) {
            std::puts("RED: operation total deadline abandoned a started frame mid-stream -> recover_frame/link retirement (frame_refused/-1)");
            return 1;
        }
        assert(results[0].code==VehicleResultCode::Expired && !results[0].authenticated && results[0].context==21);
        assert(h.sent.size()==3); for(const auto &s:h.sent) { assert(s.hello); }
        // A valid SessionInfo for the frame that completed after the deadline is
        // not admitted; no success/denial is invented after expiry.
        h.hook=nullptr; h.inject(h.hello(h.last())); h.vehicle.tick(); assert(results.size()==1);
        // Same link remains usable for the next poll (no reconnect needed).
        std::vector<VehicleResult> next; assert(h.vehicle.status(22,h.callback(next),verify)); h.vehicle.tick();
        assert(h.last().hello && next.empty());
        h.clock+=100; h.inject(h.hello(h.last())); h.vehicle.tick(); const auto status=h.last(); assert(!status.hello);
        h.clock+=100; h.inject(h.response(status,{awake,sizeof(awake)},1)); h.vehicle.tick();
        assert(next.size()==1 && next[0].code==VehicleResultCode::Status && next[0].authenticated);
    }
    // 2) No new frame starts after the total deadline (unchanged strictness).
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.status(23,h.callback(results),verify)); h.clock+=5000; const unsigned w=h.writes; h.vehicle.tick();
        assert(h.writes==w && results.size()==1 && results[0].code==VehicleResultCode::Expired);
    }
    // 3) The per-transmission budget still aborts a stalled frame (fail-safe).
    {
        Harness h; std::vector<VehicleResult> results; h.recoverable=false; assert(h.vehicle.open(h.link));
        assert(h.vehicle.status(24,h.callback(results),{5000,1000,300})); write_delay=200; h.hook=slow_write;
        h.vehicle.tick(); assert(h.recoveries==1 && results.size()==1 && results[0].code==VehicleResultCode::Disconnected && !h.vehicle.connected());
    }
    std::puts("Started frames complete across the operation deadline (official ble.go Send); no new TX/late acceptance after expiry; transmit budget fail-safe and next-poll authenticated status passed");
}
