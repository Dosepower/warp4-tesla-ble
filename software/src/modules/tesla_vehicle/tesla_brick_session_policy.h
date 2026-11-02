#pragma once
#include <cstring>
// Pure policy: fail closed on real chargers, unavailable identity, security, or autocharge.
inline bool tesla_brick_session_allowed(const char *config_type, bool evse, bool evse_v2,
                                       bool secure_boot, bool encrypted, bool autocharge)
{
    return config_type && std::strcmp(config_type, "brick") == 0 &&
           !evse && !evse_v2 && !secure_boot && !encrypted && !autocharge;
}
