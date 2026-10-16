#pragma once

#include <tesla_clean/vehicle.hpp>
#include <string>
#include <vector>

class TeslaVehicle;

class TeslaBleTransportAdapter final
{
public:
    explicit TeslaBleTransportAdapter(TeslaVehicle *owner) : owner(owner) {}

    static int random(void *, uint8_t *data, size_t size);
    tesla_clean::VehiclePorts ports();

private:
    TeslaVehicle *owner;
};
