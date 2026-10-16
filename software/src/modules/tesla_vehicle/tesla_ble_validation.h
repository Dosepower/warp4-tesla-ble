#pragma once

#include <cctype>
#include <cstring>

inline bool tesla_ble_valid_vin(const char *vin)
{
    if (vin == nullptr || std::strlen(vin) != 17) {
        return false;
    }
    for (size_t i = 0; i < 17; ++i) {
        const char c = vin[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) ||
            c == 'I' || c == 'O' || c == 'Q') {
            return false;
        }
    }
    return true;
}

inline bool tesla_ble_valid_address(const char *address)
{
    if (address == nullptr || std::strlen(address) != 17) {
        return false;
    }
    for (size_t i = 0; i < 17; ++i) {
        if ((i % 3) == 2) {
            if (address[i] != ':') {
                return false;
            }
        } else if (!std::isxdigit(static_cast<unsigned char>(address[i]))) {
            return false;
        }
    }
    return true;
}
