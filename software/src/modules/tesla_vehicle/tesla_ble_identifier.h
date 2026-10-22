#pragma once
#include "tesla_ble_validation.h"
#include <mbedtls/sha1.h>

// Tesla's VIN-derived advertising search name, NOT a BLE address or an
// authentication proof. Shared by production discovery and diagnostic pairing.
inline bool tesla_ble_vehicle_name(const char *vin, char (&name)[19])
{
    std::memset(name, 0, sizeof(name));
    if (!tesla_ble_valid_vin(vin)) { return false; }
    uint8_t digest[20]{};
    if (mbedtls_sha1(reinterpret_cast<const uint8_t *>(vin), 17, digest)) { return false; }
    constexpr char hex[] = "0123456789abcdef";
    name[0] = 'S';
    for (unsigned i = 0; i < 8; ++i) {
        name[1 + 2*i] = hex[digest[i] >> 4];
        name[2 + 2*i] = hex[digest[i] & 15];
    }
    name[17] = 'C';
    return true;
}
