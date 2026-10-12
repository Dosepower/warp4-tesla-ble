#include <cassert>
#include "../../src/modules/tesla_vehicle/tesla_ble_result.h"

int main()
{
    const uint64_t bound = 0x112233445566;
    TeslaBleRequestIdentity queued{3, {7, 2, bound}, 9, 11};
    auto current = queued;
    assert(tesla_ble_result_current(queued, current, bound, 2, true, true, true, 55));

    // Split publication: new generation/index with the old authorized MAC.
    // Another MAC matches the SAME masked profile when the event is consumed.
#ifndef ONLY_DISCONNECT
    current.vehicle.mac = bound + 1;
    assert(!tesla_ble_result_current(queued, current, bound, 2, true, true, true, 55));
#endif

    // The event was queued before the disconnect callback, but consumed after.
    current = queued;
    ++current.connection;
    assert(!tesla_ble_result_current(queued, current, bound, 2, false, true, true, 55));
}
