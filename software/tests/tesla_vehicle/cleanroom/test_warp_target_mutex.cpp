// SPDX-License-Identifier: Apache-2.0
// Compile the actual ESP_PLATFORM adapter against a faulting SDK boundary.
#define ESP_PLATFORM
#include "../../../src/modules/tesla_vehicle/tesla_ble_clean_platform.h"
#undef ESP_PLATFORM
#include <cstdio>
int main() {
    using tesla_warp::CheckedMutex; using namespace TargetMutexFixture;
    fail_create=true;
    { CheckedMutex m; assert(!m.initialized()); CheckedMutex::Guard g(m); assert(!g && !takes && !gives && !live); }
    assert(!deletes); fail_create=false;
    {
        CheckedMutex m; assert(m.initialized()); fail_take=true;
        { CheckedMutex::Guard g(m); assert(!g && !gives); }
        fail_take=false;
        { CheckedMutex::Guard g(m); assert(g); CheckedMutex::Guard refused(m,true); assert(!refused); }
        { CheckedMutex::Guard g(m,true); assert(g); }
    }
    assert(creates==2 && deletes==1 && !live && gives==2);
    std::puts("Actual ESP_PLATFORM CheckedMutex: SDK create/take refusal, no invalid handle use/unbalanced give, contention refusal, retry and balanced destruction passed; SDK modeled not target executed");
}
