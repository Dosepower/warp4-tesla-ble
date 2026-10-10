#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

inline bool tesla_ble_valid_private_key(const std::vector<uint8_t> &key)
{
    static constexpr std::string_view begin = "-----BEGIN EC PRIVATE KEY-----";
    static constexpr std::string_view end = "-----END EC PRIVATE KEY-----";
    if (key.size() < begin.size() + end.size()) {
        return false;
    }
    const auto begin_it = std::search(key.begin(), key.end(), begin.begin(), begin.end());
    const auto end_it = std::search(key.begin(), key.end(), end.begin(), end.end());
    return begin_it == key.begin() && end_it != key.end() && end_it > begin_it;
}
