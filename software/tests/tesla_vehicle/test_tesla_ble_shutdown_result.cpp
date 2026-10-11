#include <cassert>
#include <cstring>
#include "../../src/modules/tesla_vehicle/tesla_ble_runtime.h"

int main()
{
    TeslaBleShutdownResult result;
    assert(result.read() == nullptr);
    // A key operation completes after stop was signalled and the event queue is
    // full/discarded. The acknowledged outcome, not stale UI state, wins.
    result.publish(true, "Failed to initialize Bluetooth");
    assert(result.read()->key_present);
    assert(std::strcmp(result.read()->error, "Failed to initialize Bluetooth") == 0);
    TeslaBleShutdownResult deletion;
    deletion.publish(false, "Could not remove Tesla key material");
    assert(!deletion.read()->key_present);
    assert(std::strcmp(deletion.read()->error, "Could not remove Tesla key material") == 0);
}
