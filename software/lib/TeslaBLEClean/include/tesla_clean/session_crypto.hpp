// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/key.hpp>
namespace tesla_clean {
struct GcmPacket {
    std::array<uint8_t,12> nonce{};
    std::array<uint8_t,16> tag{};
    std::array<uint8_t,1024> data{};
    size_t length = 0;
};
class SessionCrypto {
public:
    ~SessionCrypto();
    SessionCrypto() = default;
    SessionCrypto(const SessionCrypto &) = delete;
    SessionCrypto &operator=(const SessionCrypto &) = delete;
    bool set_key(Bytes key);
    bool session_info_tag(Bytes vin, Bytes challenge, Bytes encoded, std::array<uint8_t,32> &output) const;
    bool encrypt(Bytes plaintext, Bytes aad, Random random, GcmPacket &output) const;
    bool decrypt(const GcmPacket &packet, Bytes aad, std::array<uint8_t,1024> &output, size_t &length) const;
private:
    std::array<uint8_t,16> key_{};
    bool ready_ = false;
};
}
