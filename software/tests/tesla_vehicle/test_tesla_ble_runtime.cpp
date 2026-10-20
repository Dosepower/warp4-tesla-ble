#include <cassert>
#include <memory>

#include "../../src/modules/tesla_vehicle/tesla_ble_runtime.h"

struct Resource {
    explicit Resource(int &live) : live(live) { ++live; }
    ~Resource() { --live; }
    int &live;
};

int main()
{
    int live = 0;
    int allocations = 0;
    TeslaBleRuntimeOwner<Resource> owner;
    auto allocate = [&]() {
        ++allocations;
        return std::make_unique<Resource>(live);
    };
    auto activate = [](Resource &) { return true; };

    // Boot-disabled must not even invoke an allocator or task starter.
    assert(!owner.start(false, allocate, activate));
    assert(allocations == 0 && live == 0 && owner.get() == nullptr);

    // Enable exactly once, without replacing a live worker's runtime.
    assert(owner.start(true, allocate, activate));
    Resource *first = owner.get();
    const uint32_t epoch = owner.epoch();
    assert(owner.accepts_event(epoch));
    assert(!owner.accepts_event(epoch - 1));
    assert(!owner.start(true, allocate, activate));
    assert(owner.get() == first && allocations == 1 && live == 1);

    // A failed task start rolls back the fully allocated candidate immediately.
    TeslaBleRuntimeOwner<Resource> failed;
    assert(!failed.start(true, allocate, [](Resource &) { return false; }));
    assert(failed.get() == nullptr && live == 1);

    // Disable cannot free resources until worker ACK and callback drain.
    int signals = 0;
    int quiesces = 0;
    assert(owner.begin_stop([&](Resource &) { ++signals; }));
    assert(!owner.accepts_event(epoch));
    assert(!owner.begin_stop([&](Resource &) { ++signals; }));
    assert(signals == 1);
    auto quiesce = [&](Resource &) { ++quiesces; return true; };
    assert(!owner.finish_stop(false, true, true, quiesce));
    assert(!owner.finish_stop(true, false, true, quiesce));
    assert(live == 1 && quiesces == 0);
    assert(owner.finish_stop(true, true, true, quiesce));
    assert(live == 0 && owner.get() == nullptr && quiesces == 1);
    assert(owner.restart_required());
    assert(!owner.start(true, allocate, activate));
    assert(!owner.begin_stop([&](Resource &) { ++signals; }));
    assert(!owner.finish_stop(true, true, true, quiesce));
    assert(signals == 1 && quiesces == 1);
    assert(!owner.accepts_event(epoch));
    return 0;
}
