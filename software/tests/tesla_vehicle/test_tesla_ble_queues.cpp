#include <cassert>
#include "../../src/modules/tesla_vehicle/tesla_ble_runtime.h"

static int live = 0;
static void destroy(int handle) { assert(handle != 0); --live; }

int main()
{
    // Failure at either allocation releases the other queue immediately.
    for (int fail_at = 1; fail_at <= 2; ++fail_at) {
        TeslaBleQueues<int, destroy> queues;
        int calls = 0;
        auto create = [&]() {
            if (++calls == fail_at) { return 0; }
            ++live;
            return calls;
        };
        assert(!queues.create(create));
        assert(live == 0 && queues.rx() == 0 && queues.events() == 0);
    }
    {
        TeslaBleQueues<int, destroy> queues;
        assert(queues.create([]() { return ++live; }));
        assert(live == 2);
    }
    assert(live == 0);
}
