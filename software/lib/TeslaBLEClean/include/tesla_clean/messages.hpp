// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/response.hpp>
#include <tesla_clean/session_info.hpp>
namespace tesla_clean {
struct Route { Domain domain=Domain::Vcsec; std::array<uint8_t,16> address{},uuid{}; };
struct EncodedRequest { std::array<uint8_t,1024> bytes{}; size_t length=0; std::array<uint8_t,17> identifier{}; size_t identifier_length=0; uint32_t counter=0; };
class OutgoingMessages {
    const SessionCrypto &crypto_;
    SessionInfo session_;
    std::array<uint8_t,17> vin_{};
    std::array<uint8_t,65> public_{};
    uint32_t counter_=0;
    bool valid_=false;
public:
    OutgoingMessages(const SessionCrypto &crypto,const SessionInfo &session,Bytes vin,Bytes public_point);
    // elapsed_ms since authenticated SessionInfo clock snapshot; counter commits only after complete encoding.
    bool build(Bytes payload,const Route &route,uint32_t elapsed_ms,uint32_t ttl_seconds,Random random,EncodedRequest &output);
    uint32_t counter() const { return counter_; }
};
bool decode_encrypted_response(Bytes input,Bytes routing_address,Response &output);
}
