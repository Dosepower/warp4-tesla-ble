// SPDX-License-Identifier: Apache-2.0
// Response-window anchoring for multi-fragment ATT write-with-response requests.
// Official reference (teslamotors/vehicle-command a4b43c1e):
//   internal/dispatcher/dispatcher.go tryStartSession:96-125 waits RetryInterval
//   only after d.Send()/conn.Send() returned; checkForSessionUpdate:182-224
//   discards SessionInfo older than AllowedLatency from request creation;
//   pkg/connector/ble/ble.go:28-29,59-61,94-96 RetryInterval=1s, maxLatency=4s.
// Production WARP timing for pair verification polls: {5000,1000,10000}; 20-byte chunks.
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
    // 1) Valid SessionInfo arriving 400 ms after the last fragment's ATT ack
    //    (but >1000 ms after TX start) must be admitted, then the encrypted
    //    status reply arriving 400 ms after its own ATT completion as well.
    {
        Harness h; std::vector<VehicleResult> results; write_delay=150; h.hook=slow_write;
        assert(h.vehicle.open(h.link));
        assert(h.vehicle.status(11,h.callback(results),verify));
        const uint32_t hello_start=h.clock; const unsigned w0=h.writes;
        h.vehicle.tick(); assert(h.sent.size()==1 && h.last().hello && h.last().domain==Domain::Vcsec);
        const uint32_t hello_tx=h.clock-hello_start; std::printf("session request: %u writes, TX %u ms\n",h.writes-w0,hello_tx);
        assert(hello_tx>=600);
        const auto challenge=h.last();
        h.clock+=400; h.hook=nullptr; h.inject(h.hello(challenge)); h.hook=slow_write;
        std::printf("SessionInfo RX at +%u ms after TX start, +400 ms after TX completion\n",h.clock-hello_start);
        const unsigned before=h.sent.size(); const uint32_t status_start=h.clock; const unsigned w1=h.writes;
        h.vehicle.tick();
        if(h.sent.size()==before || h.last().hello) {
            std::puts("RED: valid authenticated SessionInfo after TX completion was not admitted (window anchored before multi-write TX)");
            return 1;
        }
        const auto status=h.last(); assert(status.domain==Domain::Vcsec && status.plaintext==std::vector<uint8_t>({0x0a,0}));
        const uint32_t status_tx=h.clock-status_start; std::printf("status request: %u writes, TX %u ms\n",h.writes-w1,status_tx);
        h.clock+=400; h.hook=nullptr; h.inject(h.response(status,{awake,sizeof(awake)},1)); h.vehicle.tick();
        if(results.size()!=1 || results[0].code!=VehicleResultCode::Status || !results[0].authenticated) {
            std::puts("RED: authenticated encrypted status after its TX completion was not admitted");
            return 1;
        }
        assert(results[0].context==11 && results[0].sleep==SleepState::Awake);
    }
    // 2) Freshness controls stay strict. A reply is no longer discarded merely
    //    because one response budget elapsed (the identical challenge is replayed,
    //    see test_challenge_replay), but a reply >= 4 s after challenge creation
    //    is refused, the renewed challenge carries a new UUID and the predecessor
    //    UUID is refused afterwards. No KeyDenied/success is invented.
    //    (Longer total than production so the renewed challenge fits the budget.)
    //    Single-phase (phase_ms==0, pairing verify): legacy renewal after the 100 ms
    //    retry delay. Phased (SoC): renewed at once at the transport interval.
    for(const bool phased:{false,true}) {
        const VehicleTiming timing=phased?VehicleTiming{9000,1000,10000,session_auth_phase_ms}:VehicleTiming{9000,1000,10000};
        Harness h; std::vector<VehicleResult> results; write_delay=150; h.hook=slow_write;
        assert(h.vehicle.open(h.link)); assert(h.vehicle.status(12,h.callback(results),timing));
        const uint32_t created=h.clock; h.vehicle.tick(); const auto first=h.last(); assert(first.hello && h.clock==created+900);
        h.clock+=1000; h.hook=slow_write; h.vehicle.tick(); // response budget spent: identical replay
        assert(h.sent.size()==2 && h.last().hello && h.last().uuid==first.uuid && h.clock==created+2800);
        h.clock=created+4000; h.hook=nullptr; h.inject(h.hello(first)); h.vehicle.tick();
        if(!phased) {
            assert(results.empty() && h.sent.size()==2); // stale (maxLatency); renewal backoff armed
            h.clock+=100; h.hook=slow_write; h.vehicle.tick();
        } else {
            // Stale (maxLatency) reply refused; the challenge is renewed at once at the
            // transport interval (reference vehicle.cpp:325-334), never with that reply.
            assert(results.empty() && h.sent.size()==3);
        }
        const auto second=h.last(); assert(second.hello && second.uuid!=first.uuid);
        h.clock+=10; h.hook=nullptr; h.inject(h.hello(first)); h.vehicle.tick();
        assert(results.empty() && h.last().hello && h.last().uuid==second.uuid);
        // Bad HMAC on the current UUID within the window is still refused.
        h.inject(h.hello(second,false,7,1234,true)); h.vehicle.tick(); assert(results.empty() && h.last().hello);
        // Denied SessionInfo on the current UUID is reported as KeyDenied, not approval.
        h.inject(h.hello(second,true)); h.vehicle.tick();
        assert(results.size()==1 && results[0].code==VehicleResultCode::KeyDenied && !results[0].authenticated);
    }
    // 3) Official BLE maxLatency (4 s from request creation) still bounds
    //    SessionInfo even when TX itself was slow.
    {
        Harness h; std::vector<VehicleResult> results; write_delay=600; h.hook=slow_write;
        assert(h.vehicle.open(h.link)); assert(h.vehicle.status(13,h.callback(results),{9000,1000,10000}));
        const uint32_t start=h.clock; h.vehicle.tick(); const auto c=h.last(); assert(c.hello);
        assert(h.clock-start>=3600);
        h.clock=start+4000; h.hook=nullptr; h.inject(h.hello(c)); const unsigned n=h.sent.size(); h.vehicle.tick();
        assert(results.empty() && (h.sent.size()==n || h.last().hello));
    }
    // 4) Zero-latency writes: unchanged behaviour (regression of existing timing).
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.status(14,h.callback(results),verify)); h.vehicle.tick(); const auto c=h.last();
        h.clock+=999; h.inject(h.hello(c)); h.vehicle.tick(); auto s=h.last(); assert(!s.hello);
        h.clock+=1000; h.inject(h.response(s,{awake,sizeof(awake)},1)); h.vehicle.tick();
        assert(results.empty());
    }
    std::puts("Response windows anchored at full ATT-acknowledged TX completion (official dispatcher RetryInterval after Send), 4 s SessionInfo maxLatency, stale/late/bad-HMAC/denied controls passed");
}
