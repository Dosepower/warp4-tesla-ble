#include <cassert>
#include <memory>
#include "../../src/modules/tesla_vehicle/tesla_ble_runtime.h"

struct Resource { int &live; explicit Resource(int &n) : live(n) { ++live; } ~Resource() { --live; } };
int main()
{
    int live = 0, launches = 0;
    TeslaBleRuntimeOwner<Resource> owner;
    assert(owner.start(true, [&]() { return std::make_unique<Resource>(live); }, [](Resource &) { return true; }));
    TeslaBleTeardown stop;
    owner.begin_stop([&](Resource &) { stop.begin(100); });
    auto launch = [&]() { ++launches; return true; };
    assert(stop.poll(101, false, true, launch) == TeslaBleTeardown::Result::Pending);
    assert(launches == 0 && live == 1);
    assert(stop.poll(102, true, false, launch) == TeslaBleTeardown::Result::Pending);
    assert(stop.poll(103, true, true, launch) == TeslaBleTeardown::Result::Pending);
    assert(launches == 1);
    // Main never executes shutdown or waits for its result; repeated polls are bounded.
    for (int i = 0; i < 10000; ++i) {
        assert(stop.poll(104, true, true, launch) == TeslaBleTeardown::Result::Pending);
    }
    assert(launches == 1 && live == 1);
    stop.complete(false);
    assert(stop.poll(105, true, true, launch) == TeslaBleTeardown::Result::Failed);
    // Failed quiescence is terminal: no retry or late completion can free ownership.
    stop.complete(true);
    assert(stop.poll(106, true, true, launch) == TeslaBleTeardown::Result::Failed);
    assert(launches == 1 && live == 1);

    TeslaBleTeardown hung;
    hung.begin(0);
    assert(hung.poll(TeslaBleTeardown::TIMEOUT_MS + 1, false, false, launch) == TeslaBleTeardown::Result::Failed);
    assert(hung.failed());

    TeslaBleTeardown completed;
    completed.begin(0);
    completed.poll(1, true, true, launch);
    completed.complete(true);
    assert(completed.poll(2, true, true, launch) == TeslaBleTeardown::Result::Succeeded);
    assert(owner.finish_stop(true, true, true, [&](Resource &) { return completed.succeeded(); }));
    assert(live == 0 && owner.restart_required());
}
