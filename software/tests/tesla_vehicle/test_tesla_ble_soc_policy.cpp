#include <cassert>
#include <cstdint>
#include <cmath>
#include <initializer_list>

#include "../../src/modules/tesla_vehicle/tesla_ble_soc_policy.h"

int main()
{
    TeslaBleSocPolicy policy;
    constexpr uint32_t SPACING = TeslaBleSocPolicy::MIN_WAKE_SPACING_MS;
    uint32_t now = 1000;

    // No active, matching EV: never poll or accept a result.
    assert(!policy.begin_request(1, 1, false, now).allowed);
    assert(!policy.accept_result(1, 1, 50.0f));

    // A matching configured EV may be polled without waking.
    policy.on_vehicle_changed(7, 2);
    auto no_wake = policy.begin_request(7, 2, false, now);
    assert(no_wake.allowed);
    assert(!no_wake.allow_wake && !no_wake.wake_limited && no_wake.wake_token == 0);
    assert(policy.accept_result(no_wake.generation, 2, 63.0f));

    // Stale replies and replies for another profile are rejected.
    auto stale = policy.begin_request(7, 2, false, now);
    policy.on_vehicle_changed(8, 3);
    assert(!policy.accept_result(stale.generation, 2, 64.0f));
    assert(!policy.accept_result(policy.generation(), 2, 64.0f));

    // Repeated wakes across separate operations (user policy), spaced.
    policy.on_vehicle_changed(9, 4);
    auto wake_1 = policy.begin_request(9, 4, true, now);
    assert(wake_1.allowed && wake_1.allow_wake && wake_1.wake_token != 0);
    // Unsettled reservation: a concurrent request never wakes (fail closed).
    auto concurrent = policy.begin_request(9, 4, true, now);
    assert(concurrent.allowed && !concurrent.allow_wake && concurrent.wake_limited && concurrent.wake_token == 0);
    policy.settle_wake(concurrent, true, now); // no token: no effect
    assert(!policy.wake_available(now));
    policy.settle_wake(wake_1, true, now);     // possibly exposed: spacing starts
    assert(!policy.wake_available(now) && !policy.wake_available(now + SPACING - 1));
    auto too_early = policy.begin_request(9, 4, true, now + SPACING - 1);
    assert(too_early.allowed && !too_early.allow_wake && too_early.wake_limited);
    policy.request_refused(too_early, now + SPACING - 1);
    assert(!policy.wake_available(now + SPACING - 1)); // refusal never shortens the spacing
    now += SPACING;
    auto wake_2 = policy.begin_request(9, 4, true, now); // same plug-in session: allowed again
    assert(wake_2.allow_wake && wake_2.wake_token != wake_1.wake_token);

    // Definitely unexposed reservation (awake car, failure before wake TX):
    // released without starting the spacing.
    policy.settle_wake(wake_2, false, now);
    assert(policy.wake_available(now));
    policy.settle_wake(wake_2, false, now); // second settlement: no effect
    assert(policy.wake_available(now));
    auto wake_3 = policy.begin_request(9, 4, true, now);
    assert(wake_3.allow_wake);
    // Enqueue refusal releases the reservation.
    policy.request_refused(wake_3, now);
    assert(policy.wake_available(now));

    // Spacing survives an identity change (a vehicle may have been woken), and a
    // stale generation's exposure still starts it; stale tokens never release.
    auto old_generation = policy.begin_request(9, 4, true, now);
    assert(old_generation.allow_wake);
    policy.on_vehicle_changed(10, 4);
    auto current = policy.begin_request(10, 4, true, now);
    assert(current.allow_wake && current.wake_token != old_generation.wake_token);
    policy.settle_wake(old_generation, false, now);
    assert(!policy.wake_available(now)); // current reservation still held
    policy.settle_wake(current, false, now);
    assert(policy.wake_available(now));
    policy.settle_wake(old_generation, true, now + 5); // late exposure report after replug
    assert(!policy.wake_available(now + 5) && policy.wake_available(now + 5 + SPACING));

    // Exposure after the vehicle changed during the request.
    now += 5 + SPACING;
    auto during = policy.begin_request(10, 4, true, now);
    assert(during.allow_wake);
    policy.on_vehicle_changed(11, 4);
    assert(policy.wake_available(now)); // reservation dropped with the identity
    policy.settle_wake(during, true, now + 1);
    assert(!policy.wake_available(now + 1));

    // Forged/unknown token is ignored.
    TeslaBleSocRequestDecision forged; forged.wake_token = UINT64_MAX; forged.generation = 11;
    now += 1 + SPACING;
    policy.settle_wake(forged, true, now);
    assert(policy.wake_available(now));

    // Millis wraparound: spacing is computed modulo 2^32.
    TeslaBleSocPolicy wrap; wrap.on_vehicle_changed(1, 0);
    auto w = wrap.begin_request(1, 0, true, UINT32_MAX - 10);
    assert(w.allow_wake); wrap.settle_wake(w, true, UINT32_MAX - 10);
    assert(!wrap.wake_available(5) && wrap.wake_available(uint32_t(UINT32_MAX - 10 + SPACING)));

    // A request without wake permission never reserves.
    auto plain = policy.begin_request(11, 4, false, now);
    assert(plain.allowed && !plain.allow_wake && plain.wake_token == 0 && policy.wake_available(now));

    // Invalid SoC values are never accepted.
    for (float invalid : {-0.1f, 100.1f, NAN}) {
        assert(policy.begin_request(policy.generation(), 4, false, now).allowed);
        assert(!policy.accept_result(policy.generation(), 4, invalid));
    }

    // Disconnect invalidates all in-flight requests.
    auto pending = policy.begin_request(policy.generation(), 4, false, now);
    assert(pending.allowed);
    policy.on_vehicle_changed(12, -1);
    assert(!policy.accept_result(pending.generation, 4, 42.0f));

    // A BLE disconnect invalidates a pending response even if the same EV
    // session remains active.
    policy.on_vehicle_changed(13, 4);
    auto transport_pending = policy.begin_request(13, 4, false, now);
    assert(transport_pending.allowed);
    policy.on_transport_disconnected();
    assert(!policy.accept_result(transport_pending.generation, 4, 42.0f));

    return 0;
}
