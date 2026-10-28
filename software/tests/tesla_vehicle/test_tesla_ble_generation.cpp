// R9 lifecycle/result identity: executable boundary and narrowing regressions.
#include "../../src/modules/tesla_vehicle/tesla_ble_clean_platform.h"
#include <atomic>
#include <optional>
#define private public
#include "../../src/modules/tesla_vehicle/tesla_ble_result.h"
#include "../../src/modules/tesla_vehicle/tesla_ble_runtime.h"
#include "../../src/modules/tesla_vehicle/tesla_ble_command_outcome.h"
#undef private
#include "../../src/modules/tesla_vehicle/tesla_ble_soc_policy.h"
#include <cassert>
#include <cstdio>
#include <memory>

static void boundary(uint64_t initial) {
    TeslaBleSession session;
    TeslaBleIdentity id{0,2,0x112233445566}; id.generation = initial;
    session.publish(id); session.bind(id.mac); session.connect();
    session.current.connection = initial; session.sequence = initial;
    auto old = session.arm(initial,id,2);
    assert(old.request == initial + 1 && old.connection == initial && old.epoch == initial);
    const auto disconnected = session.disconnect();
    assert(disconnected == initial + 1 && session.connection() == initial + 1);
    assert(!session.matches(old));
    session.connect(); auto fresh = session.arm(initial+1,id,2);
    assert(fresh.request == initial+2 && session.matches(fresh));
    session.disarm(old); assert(session.matches(fresh));
    auto alias = fresh; alias.connection -= uint64_t{1} << 32;
    assert(!session.matches(alias));
    alias = fresh; alias.request -= uint64_t{1} << 32; assert(!session.matches(alias));
    alias = fresh; alias.epoch -= uint64_t{1} << 32; assert(!session.matches(alias));
    id.generation += uint64_t{1} << 32; session.publish(id);
    assert(!session.matches(fresh) && session.identity()->generation == id.generation);
    TeslaBleSocPolicy policy; policy.on_vehicle_changed(initial,2);
    auto decision = policy.begin_request(initial,2,false,0); assert(decision.allowed);
    policy.on_vehicle_changed(initial+(uint64_t{1}<<32),2);
    assert(policy.begin_request(initial+(uint64_t{1}<<32),2,false,0).allowed);
    assert(!policy.accept_result(decision.generation,2,50));
    TeslaBleCommandOutcome outcome; outcome.sequence = initial;
    outcome.publish(TeslaBleCommandOutcome::Kind::PairStarted,true);
    auto value=outcome.take(); assert(value && value->order == initial+1);
    assert(tesla_ble_publication_newer(value->order,initial));
    assert(!tesla_ble_publication_newer(initial,value->order));
    assert(!outcome.take());
    struct Runtime {};
    TeslaBleRuntimeOwner<Runtime> owner; owner.runtime_epoch=initial;
    assert(owner.start(true,[](){return std::make_unique<Runtime>();},[](auto &){return true;}));
    assert(owner.epoch()==initial+1 && !owner.accepts_event(initial));
    assert(!owner.accepts_event(initial+1-(uint64_t{1}<<32)));
}
int main() {
    static_assert(sizeof(TeslaBleRequestIdentity::connection) == sizeof(uint64_t));
    static_assert(sizeof(TeslaBleRequestIdentity::request) == sizeof(uint64_t));
    TeslaBlePendingDisconnect pending;
    assert(pending.synchronization_initialized() && !pending.take());
    pending.store(0); const auto zero = pending.take();
    assert(zero && *zero == 0 && !pending.take());
    pending.store(UINT64_MAX); pending.store(0);
    assert(pending.take() == uint64_t{0} && !pending.take());
    for (uint64_t initial : {uint64_t(5),uint64_t(UINT32_MAX)-1,uint64_t(UINT32_MAX),uint64_t(UINT32_MAX)+1}) { boundary(initial); }
    TeslaBleIdentitySnapshot snapshot;
    snapshot.writer_version=UINT32_MAX-1; snapshot.version.store(UINT32_MAX-1);
    TeslaBleIdentity id{0,2,0x112233445566}; id.generation = uint64_t(snapshot.writer_version)+3;
    snapshot.publish(id); assert(snapshot.read()==id); // Coherence tag itself may wrap.
    std::puts("R9 identities: 4 session/request/epoch/EV/policy/publication boundaries, low-word aliases rejected; coherent split-word identity across tag wrap; zero-valued pending token retained and taken once");
}
