#pragma once

enum class TeslaBleConfigUpdateAction {
    None,
    ApplyInLoop,
};

constexpr TeslaBleConfigUpdateAction tesla_ble_config_update_action(bool from_file,
                                                                     bool /*task_running*/,
                                                                     bool changed)
{
    if (from_file || !changed) {
        return TeslaBleConfigUpdateAction::None;
    }
    return TeslaBleConfigUpdateAction::ApplyInLoop;
}
