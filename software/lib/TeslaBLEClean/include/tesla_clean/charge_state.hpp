// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/response.hpp>
namespace tesla_clean {
Bytes get_charge_state_payload();
bool charge_state_well_formed(Bytes encoded);
bool decode_charge_state(Bytes encoded,uint8_t &battery);
struct AuthenticatedSoc { uint64_t context=0,generation=0; uint32_t counter=0; uint8_t battery=0; };
class ChargeStateAdmission {
    ResponseGuard guard_;
public:
    ChargeStateAdmission(const SessionCrypto &crypto,Bytes vin) : guard_(crypto,Domain::Infotainment,vin) {}
    bool arm(const Request &request) { return guard_.arm(request); }
    void cancel() { guard_.cancel(); }
    bool accept(const Response &response,uint32_t now,uint64_t generation,AuthenticatedSoc &output);
};
}
