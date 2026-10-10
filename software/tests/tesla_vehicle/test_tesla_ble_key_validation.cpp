#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "../../src/modules/tesla_vehicle/tesla_ble_key_validation.h"

static std::vector<uint8_t> bytes(const std::string &value)
{
    return std::vector<uint8_t>(value.begin(), value.end());
}

int main()
{
    assert(tesla_ble_valid_private_key(bytes("-----BEGIN EC PRIVATE KEY-----\nabc\n-----END EC PRIVATE KEY-----\n")));
    assert(!tesla_ble_valid_private_key(bytes("garbage")));
    assert(!tesla_ble_valid_private_key(bytes("-----BEGIN EC PRIVATE KEY-----\nabc")));
    return 0;
}
