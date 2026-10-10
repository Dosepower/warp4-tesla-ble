#pragma once

enum class TeslaBleConfigUpdateAction {
    None,
    RestartRequired,
    StopAndRestart,
};

constexpr TeslaBleConfigUpdateAction tesla_ble_config_update_action(bool from_file,
                                                                     bool task_running,
                                                                     bool changed)
{
    if (from_file || !changed) {
        return TeslaBleConfigUpdateAction::None;
    }
    return task_running ? TeslaBleConfigUpdateAction::StopAndRestart
                        : TeslaBleConfigUpdateAction::RestartRequired;
}
