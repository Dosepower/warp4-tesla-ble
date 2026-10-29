// SPDX-License-Identifier: Apache-2.0
// Structured terminal classification of SoC commands (live finding 2.13.5+6ae25bce:
// a periodic poll on a still-connected link ended with the generic "authenticated SoC
// request failed"; the actual bound/stage was not observable). Diagnostics only:
// nonsecret counts/stage/elapsed, never authority. The silent-vehicle scenarios
// below are SYNTHETIC mechanisms, not the proven field cause.
#include "controller_fixture.hpp"
#include <cstdio>
using namespace controller_fixture;
namespace {
constexpr VehicleTiming soc_timing=production_soc_timing; // production SoC request timing (tesla_vehicle.cpp)
void run_until_result(Harness &h,std::vector<VehicleResult> &results,uint32_t limit)
{
    const uint32_t end=h.clock+limit;
    while(results.empty() && h.clock<end) { h.vehicle.tick(); h.clock+=10; }
}
void print(const char *name,const VehicleDiagnostics &d)
{
    std::printf("%s: failure=%s stage=%s elapsed=%u attempts=%u challenges=%u timeouts=%u frames=%u rejected=%u ignored=%u fault=%u reused=%d/%d answered=%d/%d\n",
        name,vehicle_failure_name(d.failure),vehicle_stage_name(d.stage),d.elapsed_ms,d.attempts,d.challenges,d.response_timeouts,d.frames,
        d.rejected_replies,d.ignored_responses,d.fault,d.vcsec_reused,d.info_reused,d.vcsec_answered,d.info_answered);
    std::fflush(stdout);
}
}
int main()
{
    // A) Fresh link, VCSEC session established, encrypted status never answered.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,41,h.callback(results),soc_timing));
        h.vehicle.tick(); h.establish(); assert(!h.last().hello);
        run_until_result(h,results,soc_timing.total_ms+1000);
        assert(results.size()==1 && results[0].code!=VehicleResultCode::Soc && !results[0].wake_exposed);
        const auto &d=results[0].diagnostics; print("A silent status",d);
        assert(d.failure==VehicleFailure::AttemptCap || d.failure==VehicleFailure::Deadline);
        assert(d.stage==VehicleStage::WaitStatus || d.stage==VehicleStage::Status);
        assert(!d.vcsec_reused && !d.info_reused && d.vcsec_answered && !d.info_answered);
        assert(d.challenges==1 && d.response_timeouts>0 && d.frames==1 && d.elapsed_ms<=soc_timing.total_ms && d.fault==0);
    }
    // B) VCSEC fine, Infotainment never answers session requests: its own phase budget (challenge_cap).
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,42,h.callback(results),soc_timing));
        h.vehicle.tick(); h.establish(); auto status=h.last();
        h.inject(h.response(status,{awake,sizeof(awake)},1)); h.vehicle.tick();
        assert(h.last().hello && h.last().domain==Domain::Infotainment);
        run_until_result(h,results,soc_timing.total_ms+1000);
        assert(results.size()==1 && results[0].code==VehicleResultCode::Failed);
        const auto &d=results[0].diagnostics; print("B silent infotainment session",d);
        assert(d.failure==VehicleFailure::ChallengeCap && (d.stage==VehicleStage::InfoAuth || d.stage==VehicleStage::WaitInfoAuth));
        // 1 VCSEC + renewed Infotainment challenges (every 4 s within its 25 s phase), bounded.
        assert(d.vcsec_answered && !d.info_answered && d.challenges>=1+5 && d.challenges<=1+session_info_max_challenges);
        assert(d.elapsed_ms<soc_timing.total_ms); // ends on its own challenge bound, before the operation deadline
    }
    // C) Second command on the same link reuses both sessions and succeeds unchanged.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,43,h.callback(results),soc_timing));
        h.vehicle.tick(); h.establish(); auto status=h.last(); h.inject(h.response(status,{awake,sizeof(awake)},1)); h.vehicle.tick();
        h.establish(); auto query=h.last(); h.inject(h.response(query,{soc,sizeof(soc)},1)); h.vehicle.tick();
        assert(results.size()==1 && results[0].code==VehicleResultCode::Soc && results[0].diagnostics.failure==VehicleFailure::None);
        assert(!results[0].diagnostics.vcsec_reused && results[0].diagnostics.challenges==2);
        results.clear(); h.clock+=300000;
        assert(h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,44,h.callback(results),soc_timing));
        h.vehicle.tick(); status=h.last(); assert(!status.hello);
        h.inject(h.response(status,{awake,sizeof(awake)},1)); h.vehicle.tick();
        query=h.last(); assert(!query.hello); h.inject(h.response(query,{soc,sizeof(soc)},1)); h.vehicle.tick();
        assert(results.size()==1 && results[0].code==VehicleResultCode::Soc);
        const auto &d=results[0].diagnostics; print("C reused",d);
        assert(d.failure==VehicleFailure::None && d.vcsec_reused && d.info_reused && d.challenges==0 && d.vcsec_answered && d.info_answered);
    }
    // D) Link loss is classified separately, never as a timeout.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,45,h.callback(results),soc_timing)); h.vehicle.tick();
        h.vehicle.close(h.link);
        assert(results.size()==1 && results[0].code==VehicleResultCode::Disconnected);
        assert(results[0].diagnostics.failure==VehicleFailure::Disconnected && results[0].diagnostics.stage==VehicleStage::WaitVcsecAuth);
        assert(!results[0].diagnostics.vcsec_answered && !results[0].diagnostics.info_answered); // nothing answered: none claimed
    }
    // D2) Link loss after a valid VCSEC answer keeps that answer in the Disconnected diagnostics (as finish() does).
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,48,h.callback(results),soc_timing));
        h.vehicle.tick(); h.establish(); auto status=h.last();
        h.inject(h.response(status,{awake,sizeof(awake)},1)); h.vehicle.tick();
        assert(results.empty() && h.last().hello && h.last().domain==Domain::Infotainment);
        h.vehicle.close(h.link);
        assert(results.size()==1 && results[0].code==VehicleResultCode::Disconnected);
        const auto &d=results[0].diagnostics; print("D2 disconnect after VCSEC answer",d);
        assert(d.failure==VehicleFailure::Disconnected && d.vcsec_answered && !d.info_answered);
    }
    // E) Authenticated, non-retryable vehicle fault keeps its numeric code.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,46,h.callback(results),soc_timing));
        h.vehicle.tick(); h.establish(); auto status=h.last();
        h.inject(h.response(status,{awake,sizeof(awake)},1,9)); h.vehicle.tick();
        assert(results.size()==1 && results[0].code==VehicleResultCode::Failed);
        const auto &d=results[0].diagnostics; print("E fault",d);
        assert(d.failure==VehicleFailure::VehicleFault && d.fault==9 && d.stage==VehicleStage::WaitStatus);
    }
    std::puts("SoC command classification: silent status (attempt/deadline), silent Infotainment session (challenge cap), reuse, link loss and vehicle fault passed");
}
