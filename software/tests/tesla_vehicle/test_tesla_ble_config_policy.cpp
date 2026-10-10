#include <cassert>

#include "../../src/modules/tesla_vehicle/tesla_ble_config_policy.h"

int main()
{
    assert(tesla_ble_config_update_action(true, true, true) == TeslaBleConfigUpdateAction::None);
    assert(tesla_ble_config_update_action(false, false, false) == TeslaBleConfigUpdateAction::None);
    assert(tesla_ble_config_update_action(false, false, true) == TeslaBleConfigUpdateAction::RestartRequired);
    assert(tesla_ble_config_update_action(false, true, true) == TeslaBleConfigUpdateAction::StopAndRestart);
    return 0;
}
