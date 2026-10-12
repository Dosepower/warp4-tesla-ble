#include <cassert>
#include "../../src/modules/tesla_vehicle/tesla_ble_runtime.h"

int main()
{
    int filesystem_probes = 0;
    auto storage_probe = [&]() { ++filesystem_probes; return true; };
    assert(!tesla_ble_initial_key_presence(false, storage_probe));
    assert(filesystem_probes == 0);
    assert(tesla_ble_initial_key_presence(true, storage_probe));
    assert(filesystem_probes == 1);
}
