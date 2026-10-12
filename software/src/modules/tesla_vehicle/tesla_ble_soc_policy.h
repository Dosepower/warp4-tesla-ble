/* esp32-firmware
 * Copyright (C) 2026
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#pragma once

#include <cmath>
#include <cstdint>

struct TeslaBleSocRequestDecision {
    bool allowed = false;
    bool allow_wake = false;
    uint64_t generation = 0;
};

// Keeps the safety-critical request/result gating independent from the BLE
// transport. Every EV change creates a new generation. A result is accepted
// only for the same generation and the same configured WARP EV profile.
class TeslaBleSocPolicy
{
public:
    void on_vehicle_changed(uint64_t generation, int active_ev_index)
    {
        generation_ = generation;
        active_ev_index_ = active_ev_index;
        wake_used_ = false;
        request_active_ = false;
    }

    void on_transport_disconnected()
    {
        request_active_ = false;
    }

    TeslaBleSocRequestDecision begin_request(uint64_t expected_generation,
                                             int configured_ev_index,
                                             bool request_wake)
    {
        TeslaBleSocRequestDecision decision;
        decision.generation = generation_;

        if (active_ev_index_ < 0 ||
            expected_generation != generation_ ||
            configured_ev_index != active_ev_index_) {
            return decision;
        }

        decision.allowed = true;
        request_active_ = true;
        decision.allow_wake = request_wake && !wake_used_;
        if (decision.allow_wake) {
            wake_used_ = true;
        }
        return decision;
    }

    void request_refused(const TeslaBleSocRequestDecision &decision)
    {
        if (decision.generation != generation_) { return; }
        request_active_ = false;
        // Wake permission was reserved by begin_request, not actually used.
        if (decision.allow_wake) { wake_used_ = false; }
    }

    bool accept_result(uint64_t request_generation,
                       int configured_ev_index,
                       float soc)
    {
        const bool accepted = request_active_ &&
                              active_ev_index_ >= 0 &&
                              request_generation == generation_ &&
                              configured_ev_index == active_ev_index_ &&
                              std::isfinite(soc) && soc >= 0.0f && soc <= 100.0f;
        request_active_ = false;
        return accepted;
    }

    uint64_t generation() const
    {
        return generation_;
    }

private:
    uint64_t generation_ = 0;
    int active_ev_index_ = -1;
    bool wake_used_ = false;
    bool request_active_ = false;
};
