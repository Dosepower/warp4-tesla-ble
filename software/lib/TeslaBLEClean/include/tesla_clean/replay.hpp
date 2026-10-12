// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
namespace tesla_clean {
class ReplayWindow {
public:
    // Official window.go semantics: ordinary integers, never modulo counter rollover.
    bool admit(uint32_t value)
    {
        if (!used_) { maximum_ = value; used_ = true; return true; }
        if (value == maximum_) { return false; }
        if (value < maximum_) {
            const uint32_t distance = maximum_ - value;
            if (distance > 32) { return false; }
            const uint32_t bit = uint32_t{1} << (distance - 1);
            if ((history_ & bit) != 0) { return false; }
            history_ |= bit;
            return true;
        }
        const uint32_t step = value - maximum_;
        history_ = step < 32 ? history_ << step : 0;
        if (step <= 32) { history_ |= uint32_t{1} << (step - 1); }
        maximum_ = value;
        return true;
    }
    void reset() { maximum_ = history_ = 0; used_ = false; }
private:
    uint32_t maximum_ = 0;
    uint32_t history_ = 0;
    bool used_ = false;
};
}
