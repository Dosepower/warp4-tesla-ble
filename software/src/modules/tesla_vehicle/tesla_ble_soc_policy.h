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
    // Wake was requested, but refused because a wake reservation is still
    // unsettled or the minimum spacing since the last possible wake is running.
    bool wake_limited = false;
    uint64_t generation = 0;
    // Nonzero only while this request owns the wake reservation.
    uint64_t wake_token = 0;
};

// Keeps the safety-critical request/result gating independent from the BLE
// transport. Every EV change creates a new generation. A result is accepted
// only for the same generation and the same configured WARP EV profile.
//
// Wake policy (explicit user decision, replaces "one wake per plug-in"):
// each SoC operation sends at most ONE wake (tesla_clean::Vehicle never resends
// it inside an operation). Separate operations may wake an asleep vehicle
// again, rate-limited by MIN_WAKE_SPACING_MS between wakes that may have
// reached the vehicle. begin_request only RESERVES the wake; the request
// settles the reservation once with the controller's exposure fact:
// - possibly exposed (sent, partially/refusedly written, unacknowledged):
//   the spacing starts at settlement (never earlier than the real exposure);
// - definitely not on the wire (vehicle already awake, skipped, failed before
//   the wake stage, enqueue refused): released, spacing untouched.
// An unsettled reservation blocks further wakes (fail closed). Exposure
// reported by a stale generation still starts the spacing: some vehicle may
// have been woken. Stale tokens never release a newer reservation.
class TeslaBleSocPolicy
{
public:
    static constexpr uint32_t MIN_WAKE_SPACING_MS = 120000;

    void on_vehicle_changed(uint64_t generation, int active_ev_index)
    {
        generation_ = generation;
        active_ev_index_ = active_ev_index;
        wake_reservation_ = 0;
        request_active_ = false;
    }

    void on_transport_disconnected()
    {
        request_active_ = false;
    }

    TeslaBleSocRequestDecision begin_request(uint64_t expected_generation,
                                             int configured_ev_index,
                                             bool request_wake,
                                             uint32_t now_ms)
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
        if (!request_wake) { return decision; }
        decision.allow_wake = wake_available(now_ms) && next_wake_token_ != UINT64_MAX;
        decision.wake_limited = !decision.allow_wake;
        if (decision.allow_wake) {
            // Tokens are never reused, also across generations.
            wake_reservation_ = ++next_wake_token_;
            decision.wake_token = wake_reservation_;
        }
        return decision;
    }

    void request_refused(const TeslaBleSocRequestDecision &decision, uint32_t now_ms)
    {
        if (decision.generation == generation_) { request_active_ = false; }
        // Refused before enqueue: nothing of this request reached the vehicle.
        settle_wake(decision, false, now_ms);
    }

    void settle_wake(const TeslaBleSocRequestDecision &decision, bool exposed, uint32_t now_ms)
    {
        if (decision.wake_token == 0 || decision.wake_token > next_wake_token_) { return; }
        if (exposed) { last_wake_ms_ = now_ms; wake_seen_ = true; }
        if (decision.wake_token == wake_reservation_) { wake_reservation_ = 0; }
    }

    bool wake_available(uint32_t now_ms) const
    {
        return wake_reservation_ == 0 &&
               (!wake_seen_ || uint32_t(now_ms - last_wake_ms_) >= MIN_WAKE_SPACING_MS);
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
    uint64_t wake_reservation_ = 0;
    uint64_t next_wake_token_ = 0;
    uint32_t last_wake_ms_ = 0;
    bool wake_seen_ = false;
    bool request_active_ = false;
};
