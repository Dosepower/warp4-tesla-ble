// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/session_crypto.hpp>
namespace tesla_clean {
struct SessionInfo {
    std::array<uint8_t,65> remote_public{};
    std::array<uint8_t,16> epoch{};
    uint32_t counter=0, clock=0, status=0, handle=0;
};
enum class SessionInfoResult { Rejected, Established, KeyDenied };
// Decoder output is untrusted. Only authenticate_session_info may publish a key.
bool decode_session_info(Bytes encoded, SessionInfo &output);
SessionInfoResult authenticate_session_info(P256Key &private_key, Random random, Bytes vin,
                                           Bytes challenge, Bytes encoded, Bytes tag,
                                           SessionInfo &output, std::array<uint8_t,16> &session_key);
}
