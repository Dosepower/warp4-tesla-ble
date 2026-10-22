#include "tesla_ble_transport.h"

#include "tesla_vehicle.h"

#ifndef TESLA_BLE_MODULE_HOST_TEST
#include <esp_random.h>
#endif

int TeslaBleTransportAdapter::random(void *, uint8_t *data, size_t size)
{
    if (!data || !size) { return -1; }
    esp_fill_random(data, size);
    return 0;
}

tesla_clean::VehiclePorts TeslaBleTransportAdapter::ports()
{
    return {{random, nullptr}, [](void *) { return millis(); },
        [](void *p, tesla_clean::Bytes bytes, uint64_t generation) {
            auto *owner = static_cast<TeslaBleTransportAdapter *>(p)->owner;
            return owner->transport_owns(generation) && owner->transport_write_bytes(bytes.data, bytes.size) &&
                   owner->transport_owns(generation);
        }, [](void *p, uint64_t generation) { return static_cast<TeslaBleTransportAdapter *>(p)->owner->transport_owns(generation); },
        this, 20, [](void *p, uint64_t generation) {
            return static_cast<TeslaBleTransportAdapter *>(p)->owner->transport_recover_frame(generation);
        }};
}
