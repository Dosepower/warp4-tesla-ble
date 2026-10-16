// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/authentication.hpp>
#include <tesla_clean/messages.hpp>
#include <tesla_clean/framing.hpp>
namespace tesla_clean {
struct SessionWireReply {
    Domain domain=Domain::Vcsec;
    std::array<uint8_t,16> uuid{};
    std::array<uint8_t,512> encoded{};
    size_t length=0;
    std::array<uint8_t,32> tag{};
    SessionReply view(uint64_t generation) const { return {domain,generation,uuid,{encoded.data(),length},{tag.data(),tag.size()}}; }
};
// The nested challenge is omitted like the official dispatcher: outer UUID is the challenge.
bool encode_session_request(const ChallengeView &challenge,Bytes routing_address,Frame &output);
// Decoding does not authenticate. Routing, domain, complete UUID and HMAC wrapper are required.
bool decode_session_reply(Bytes wire,Bytes routing_address,SessionWireReply &output);
}
