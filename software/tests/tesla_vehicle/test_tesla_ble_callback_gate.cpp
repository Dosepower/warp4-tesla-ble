#include <cassert>
#include <atomic>
#include <thread>
#include <vector>
#include "../../src/modules/tesla_vehicle/tesla_ble_runtime.h"

int main()
{
    TeslaBleCallbackGate gate;
    assert(!gate.enter()); // Closed at boot: no callback may access runtime.
    gate.open();
    {
        auto notification = gate.enter();
        assert(notification);
        gate.close();
        assert(!gate.enter()); // Reject late notifications after stop admission.
        assert(!gate.drained()); // Keep queues alive for the admitted sender.
    }
    assert(gate.drained());
    assert(!gate.enter());
    gate.open();
    std::atomic<int> admitted{0};
    std::atomic<bool> release{false};
    std::vector<std::thread> callbacks;
    for (int i = 0; i < 8; ++i) {
        callbacks.emplace_back([&]() {
            auto lease = gate.enter();
            assert(lease);
            admitted.fetch_add(1);
            while (!release.load()) { std::this_thread::yield(); }
        });
    }
    while (admitted.load() != 8) { std::this_thread::yield(); }
    gate.close();
    assert(!gate.drained());
    // Late concurrent producers must never acquire runtime/queue ownership.
    std::vector<std::thread> late;
    for (int i = 0; i < 8; ++i) {
        late.emplace_back([&]() { for (int j = 0; j < 10000; ++j) { assert(!gate.enter()); } });
    }
    for (auto &thread : late) { thread.join(); }
    assert(!gate.drained());
    release.store(true);
    for (auto &thread : callbacks) { thread.join(); }
    assert(gate.drained());
    // Admission racing close: all successful entries must drain, regardless
    // of which side of the single atomic linearization point they land on.
    for (int iteration = 0; iteration < 100; ++iteration) {
        gate.open();
        std::thread racing([&]() { auto lease = gate.enter(); std::this_thread::yield(); });
        gate.close();
        racing.join();
        assert(gate.drained() && !gate.enter());
    }
}
