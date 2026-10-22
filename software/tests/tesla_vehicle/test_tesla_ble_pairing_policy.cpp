#include <cassert>

#include "../../src/modules/tesla_vehicle/tesla_ble_pairing_policy.h"

int main()
{
    TeslaBlePairingPolicy policy;
    policy.on_key_loaded(true);
    assert(policy.key_present());
    assert(!policy.paired());

    policy.on_pair_request_sent();
    assert(policy.pairing_pending());
    assert(!policy.paired());

    policy.on_authenticated_response();
    assert(policy.key_present());
    assert(policy.paired());
    assert(!policy.pairing_pending());

    assert(!policy.vehicle_matches(0x112233445566ULL));
    policy.bind_vehicle(0x112233445566ULL);
    assert(policy.vehicle_matches(0x112233445566ULL));
    assert(!policy.vehicle_matches(0xAABBCCDDEEFFULL));
    assert(!policy.vehicle_matches(0));

    policy.on_key_removed();
    assert(!policy.key_present());
    assert(!policy.paired());
    assert(!policy.pairing_pending());
    assert(!policy.vehicle_matches(0x112233445566ULL));
    return 0;
}
