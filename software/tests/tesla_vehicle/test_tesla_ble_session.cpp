#include <atomic>
#include <cassert>
#include <thread>
#include <initializer_list>
#include "../../src/modules/tesla_vehicle/tesla_ble_result.h"

int main()
{
    TeslaBleSession session;
    const TeslaBleIdentity car{7, 2, 0x112233445566};
    session.publish(car);
    session.bind(car.mac);
    // Real production ordering boundary: connect clears old request state.
    auto connect = [&]() { session.disconnect(); session.connect(); return true; };
    auto token = tesla_ble_prepare_request(session, 3, car, 2, connect);
    assert(token.request != 0 && session.matches(token));
    int applied = 0;
    auto apply = [&](float) { ++applied; };
    assert(session.consume(token, car, 2, true, true, 55, apply));
    assert(applied == 1);
    token = session.arm(3, car, 2);
    assert(token.request != 0);
    // Descriptor completion was observed, then disconnect wins before ready.
    const auto setup_generation = session.connection();
    session.disconnect();
    bool published_ready = false;
    assert(!session.connect(setup_generation, [&]() { published_ready = true; }));
    assert(!published_ready);
    session.disconnect();
    assert(!session.consume(token, car, 2, true, true, 60, apply));
    assert(applied == 1);
    connect();
    for (float invalid : {NAN, -0.1f, 100.1f}) {
        token = session.arm(3, car, 2);
        assert(token.request != 0);
        assert(!session.consume(token, car, 2, true, true, invalid, apply));
    }
    assert(applied == 1);

    token = tesla_ble_prepare_request(session, 3, car, 2, connect);
    auto same_profile = car;
    ++same_profile.generation;
    ++same_profile.mac;
    session.publish(same_profile);
    assert(!session.matches(token));
    assert(!session.consume(token, same_profile, 2, true, true, 60, apply));
    // Captured identity changes during connect: no request may be armed.
    session.publish(car);
    token = tesla_ble_prepare_request(session, 3, car, 2, [&]() {
        connect(); session.publish(same_profile); return true;
    });
    assert(token.request == 0);

    // Publication interleaving: every observation is a complete old or new tuple.
    session.publish(car);
    std::atomic<bool> done{false};
    std::thread publisher([&]() {
        for (int i = 0; i < 100000; ++i) {
            session.publish(same_profile); session.publish(car);
        }
        done.store(true);
    });
    do {
        auto snapshot = session.identity();
        // Bounded readers may reject a colliding publication, but never return
        // a torn tuple with an active MAC/profile.
        assert(!snapshot || snapshot == car || snapshot == same_profile);
    } while (!done.load());
    publisher.join();
}
