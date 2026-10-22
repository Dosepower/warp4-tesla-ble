// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/replay.hpp>
#include <cassert>
#include <cstdio>
#include <cstdint>
using namespace tesla_clean;
int main()
{
    ReplayWindow w;
    assert(w.admit(0)); assert(!w.admit(0)); assert(w.admit(40));
    assert(w.admit(8)); assert(!w.admit(8)); // exactly 32 behind
    assert(!w.admit(7)); assert(w.admit(39)); assert(!w.admit(39));
    assert(w.admit(41)); assert(!w.admit(40)); assert(!w.admit(8));
    assert(w.admit(UINT32_MAX)); assert(!w.admit(0)); assert(!w.admit(UINT32_MAX));
    assert(w.admit(UINT32_MAX-32)); assert(!w.admit(UINT32_MAX-33));
    w.reset(); assert(w.admit(0));
    // Exhaustive small-sequence oracle, independent set of observed counters.
    for (uint32_t start=0; start<40; ++start) {
        ReplayWindow check;
        bool seen[100]{}; uint32_t greatest=0; bool used=false;
        for (uint32_t i=0; i<400; ++i) {
            uint32_t value=(i*37+start)%100;
            bool allowed=!used || (!seen[value] && (value>greatest || greatest-value<=32));
            assert(check.admit(value)==allowed);
            if (allowed) { seen[value]=true; if (!used || value>greatest) { greatest=value; } used=true; }
        }
    }
    ReplayWindow vcsec, infotainment;
    assert(vcsec.admit(8)); assert(infotainment.admit(8)); assert(!vcsec.admit(8));
    std::puts("Clean replay: independent-set oracle, 32-position boundary, duplicates, large gaps, UINT32_MAX/no rollover and domain isolation passed");
}
