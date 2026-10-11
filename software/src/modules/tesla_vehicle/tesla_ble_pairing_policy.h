#pragma once

#include <cstdint>

class TeslaBlePairingPolicy
{
public:
    void on_key_loaded(bool present)
    {
        key_present_ = present;
        paired_ = false;
        pairing_pending_ = false;
    }

    void on_pair_request_sent()
    {
        key_present_ = true;
        paired_ = false;
        pairing_pending_ = true;
    }

    void on_authenticated_response()
    {
        if (key_present_) {
            paired_ = true;
        }
        pairing_pending_ = false;
    }

    void on_pairing_error()
    {
        pairing_pending_ = false;
    }

    void on_key_removed()
    {
        key_present_ = false;
        paired_ = false;
        pairing_pending_ = false;
        bound_vehicle_mac_ = 0;
    }

    void bind_vehicle(uint64_t mac) { bound_vehicle_mac_ = mac; }
    bool vehicle_matches(uint64_t mac) const { return mac != 0 && mac == bound_vehicle_mac_; }
    uint64_t bound_vehicle_mac() const { return bound_vehicle_mac_; }

    bool key_present() const { return key_present_; }
    bool paired() const { return paired_; }
    bool pairing_pending() const { return pairing_pending_; }

private:
    bool key_present_ = false;
    bool paired_ = false;
    bool pairing_pending_ = false;
    uint64_t bound_vehicle_mac_ = 0;
};
