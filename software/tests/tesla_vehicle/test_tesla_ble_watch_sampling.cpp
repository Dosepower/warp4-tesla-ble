#include <cassert>
#include <cstdio>
#include "../../src/modules/tesla_vehicle/tesla_ble_operation.h"
int main()
{
    TeslaBleWorkWatch watch;
    // Exact cross-core interleaving: main sampled 100, then worker began at 101.
    watch.begin(101);
    assert(!watch.expired(100, 10000));
    assert(!watch.expired(101, 10000));
    assert(!watch.expired(10101, 10000));
    assert(watch.expired(10102, 10000));
    watch.finish();
    assert(!watch.expired(10103, 10000));
    // Genuine wrap: start before wrap, observation after wrap.
    watch.begin(UINT32_MAX - 4);
    assert(!watch.expired(5, 10));
    assert(watch.expired(6, 10));
    // Same sampling race straddling wrap: now belongs to previous operation.
    watch.begin(2);
    assert(!watch.expired(UINT32_MAX - 1, 10));
    watch.finish();
    std::puts("WorkWatch sampling/wrap regression passed");
}
