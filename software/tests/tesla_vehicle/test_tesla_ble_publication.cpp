#include <cassert>
#include <chrono>
#include <future>
#include <thread>
#include "../../src/modules/tesla_vehicle/tesla_ble_result.h"

int main()
{
    TeslaBleSession session;
    const TeslaBleIdentity old{1, 2, 0x112233445566}, next{2, 2, 0x223344556677};
    session.publish(old);
    const auto generation = session.connection();
    std::promise<void> held, release;
    auto release_signal = release.get_future();
    std::thread worker([&]() {
        session.connect(generation, [&]() {
            held.set_value();
            release_signal.wait(); // Simulate a preempted session-state holder.
        });
    });
    held.get_future().wait();
    auto main_publication = std::async(std::launch::async, [&]() { session.publish(next); });
    // Publication in the shared loop must not wait for the worker's state lock.
    assert(main_publication.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready);
    assert(session.identity() == next);
    release.set_value();
    worker.join();
    main_publication.get();
}
