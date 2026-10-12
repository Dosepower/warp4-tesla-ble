#include <cassert>

#include "../../src/modules/tesla_vehicle/tesla_ble_validation.h"

int main()
{
    assert(tesla_ble_valid_vin("5YJ3E7EA7KF000000"));
    assert(!tesla_ble_valid_vin("5YJ3E7EA7KF00000"));
    assert(!tesla_ble_valid_vin("5YJ3E7EA7KF00000I"));
    assert(!tesla_ble_valid_vin("5yj3e7ea7kf000000"));

    assert(tesla_ble_valid_address("AA:BB:CC:DD:EE:FF"));
    assert(tesla_ble_valid_address("aa:bb:cc:dd:ee:ff"));
    assert(!tesla_ble_valid_address("AA-BB-CC-DD-EE-FF"));
    assert(!tesla_ble_valid_address("GG:BB:CC:DD:EE:FF"));
    return 0;
}
