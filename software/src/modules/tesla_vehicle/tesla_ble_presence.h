#pragma once
#include <cstddef>
// Wire tokens fit ESP32 WString SSO (14 characters), including all copies.
// The UI maps wire "cleanup" to the semantic cleanup-required state.
static constexpr size_t TESLA_BLE_PRESENCE_MAX_LENGTH = 7;
inline const char *tesla_ble_presence_token(bool present, bool known, bool cleanup)
{
    return cleanup ? "cleanup" : !known ? "unknown" : present ? "present" : "absent";
}
template<class ConfigT>
inline void tesla_ble_publish_presence(ConfigT target, bool present, bool known = true, bool cleanup = false)
{
    // Explicit checked construction/move; no update_value<String> by-value copy.
    // All tokens are bounded SSO. Even hypothetical allocation refusal leaves
    // the old valid state rather than invoking Config's invalid-string abort.
    target->updateStringChecked(tesla_ble_presence_token(present, known, cleanup));
}
