#pragma once
#include <cstdint>
// Single EV main-context writer. Always invalidate admission before transition;
// after exhaustion no SLAC identity may become valid again until reboot.
inline bool ev_advance_session(uint64_t &generation, bool &exhausted, int &index, uint64_t &mac)
{
    index = -1; mac = 0;
    if (exhausted || generation == UINT64_MAX) { exhausted = true; return false; }
    ++generation;
    return true;
}
