#include <cassert>
#include "../../src/modules/tesla_vehicle/tesla_ble_key_command.h"
int main()
{
    int disconnects = 0, discarded = 0, rebuilt = 0;
    auto result = tesla_ble_forget([]() { return false; }, [&]() { ++disconnects; },
        [&]() { ++discarded; }, []() { return false; }, [&]() { ++rebuilt; });
    assert(result == TeslaBleForgetResult::failed);
    assert(disconnects == 1 && discarded == 1 && rebuilt == 0);
    // Cancellation before each of the four stages prevents that stage and all
    // following stages. Successful erase followed by stop cannot regenerate keys.
    for (int stop_at = 0; stop_at < 4; ++stop_at) {
        int checks = 0, actions = 0;
        result = tesla_ble_forget([&]() { return checks++ >= stop_at; },
            [&]() { ++actions; }, [&]() { ++actions; }, [&]() { ++actions; return true; },
            [&]() { ++actions; });
        assert(result == TeslaBleForgetResult::cancelled);
        assert(actions == stop_at);
    }
}
