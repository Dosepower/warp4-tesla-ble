#include <cassert>
#include <cstdint>
#include <cmath>
#include <initializer_list>

#include "../../src/modules/tesla_vehicle/tesla_ble_soc_policy.h"

int main()
{
    TeslaBleSocPolicy policy;

    // No active, matching EV: never poll or accept a result.
    assert(!policy.begin_request(1, 1, false).allowed);
    assert(!policy.accept_result(1, 1, 50.0f));

    // A matching configured EV may be polled without waking.
    policy.on_vehicle_changed(7, 2);
    auto no_wake = policy.begin_request(7, 2, false);
    assert(no_wake.allowed);
    assert(!no_wake.allow_wake);
    assert(policy.accept_result(no_wake.generation, 2, 63.0f));

    // Stale replies and replies for another profile are rejected.
    auto stale = policy.begin_request(7, 2, false);
    policy.on_vehicle_changed(8, 3);
    assert(!policy.accept_result(stale.generation, 2, 64.0f));
    assert(!policy.accept_result(policy.generation(), 2, 64.0f));

    // Exactly one wake attempt is allowed for each plug/session generation.
    policy.on_vehicle_changed(9, 4);
    auto wake_1 = policy.begin_request(9, 4, true);
    assert(wake_1.allowed && wake_1.allow_wake);
    auto wake_2 = policy.begin_request(9, 4, true);
    assert(wake_2.allowed && !wake_2.allow_wake);
    policy.on_vehicle_changed(10, 4);
    auto wake_3 = policy.begin_request(10, 4, true);
    assert(wake_3.allowed && wake_3.allow_wake);

    // Invalid SoC values are never accepted.
    for (float invalid : {-0.1f, 100.1f, NAN}) {
        assert(policy.begin_request(policy.generation(), 4, false).allowed);
        assert(!policy.accept_result(policy.generation(), 4, invalid));
    }

    // Disconnect invalidates all in-flight requests.
    auto pending = policy.begin_request(policy.generation(), 4, false);
    assert(pending.allowed);
    policy.on_vehicle_changed(11, -1);
    assert(!policy.accept_result(pending.generation, 4, 42.0f));

    // A BLE disconnect invalidates a pending response even if the same EV
    // session remains active.
    policy.on_vehicle_changed(12, 4);
    auto transport_pending = policy.begin_request(12, 4, false);
    assert(transport_pending.allowed);
    policy.on_transport_disconnected();
    assert(!policy.accept_result(transport_pending.generation, 4, 42.0f));

    return 0;
}
