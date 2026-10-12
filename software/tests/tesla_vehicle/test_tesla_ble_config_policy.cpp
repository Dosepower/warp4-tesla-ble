#include <cassert>

#include "../../src/modules/tesla_vehicle/tesla_ble_config_policy.h"

int main()
{
    assert(tesla_ble_config_update_action(true, true, true) == TeslaBleConfigUpdateAction::None);
    assert(tesla_ble_config_update_action(false, false, false) == TeslaBleConfigUpdateAction::None);
    // First enable after a disabled boot is safe without rebooting.
    assert(tesla_ble_config_update_action(false, false, true) == TeslaBleConfigUpdateAction::ApplyInLoop);
    // The callback must only defer a transition, never promise a hot restart.
    assert(tesla_ble_config_update_action(false, true, true) == TeslaBleConfigUpdateAction::ApplyInLoop);
    return 0;
}
