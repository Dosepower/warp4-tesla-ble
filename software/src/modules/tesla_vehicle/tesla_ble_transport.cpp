#include "tesla_ble_transport.h"

#include "tesla_vehicle.h"

void TeslaBleTransportAdapter::connect(const std::string & /*address*/)
{
    // Connection lifecycle is owned by the dedicated Tesla BLE task.
}

void TeslaBleTransportAdapter::disconnect()
{
    owner->transport_disconnect();
}

bool TeslaBleTransportAdapter::write(const std::vector<uint8_t> &data)
{
    return owner->transport_write(data);
}
