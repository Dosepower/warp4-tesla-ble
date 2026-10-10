#pragma once

#include <adapters.h>
#include <string>
#include <vector>

class TeslaVehicle;

class TeslaBleTransportAdapter final : public TeslaBLE::BleAdapter
{
public:
    explicit TeslaBleTransportAdapter(TeslaVehicle *owner) : owner(owner) {}

    void connect(const std::string &address) override;
    void disconnect() override;
    bool write(const std::vector<uint8_t> &data) override;

private:
    TeslaVehicle *owner;
};
