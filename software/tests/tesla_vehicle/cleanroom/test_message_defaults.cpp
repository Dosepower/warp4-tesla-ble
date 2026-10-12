// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/messages.hpp>
#include <tesla_clean/charge_state.hpp>
#include "wire_vectors.hpp"
#include "vectors.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tesla_clean;
int main()
{
    // Removing a complete final flags field is a valid proto3 message, not a malformed truncation.
    Response response;
    assert(decode_encrypted_response({wire_vectors::response,sizeof(wire_vectors::response)-3},{wire_vectors::routing,16},response));
    assert(response.flags==0 && response.counter==1 && response.domain==Domain::Infotainment);
    SessionCrypto crypto; assert(crypto.set_key({wire_vectors::key,16})); ChargeStateAdmission admission(crypto,{vectors::vin,17});
    Request op; op.context=27; op.generation=3; op.total_ms=1000; op.response_ms=500;
    std::memcpy(op.uuid.data(),wire_vectors::uuid,16); op.identifier_length=17;
    std::memcpy(op.identifier.data(),wire_vectors::identifier,17);
    AuthenticatedSoc soc; assert(admission.arm(op));
    assert(!admission.accept(response,1,3,soc) && soc.context==0); // clearing wire flags invalidates authenticated AAD
    assert(decode_encrypted_response({wire_vectors::response,sizeof(wire_vectors::response)},{wire_vectors::routing,16},response));
    assert(admission.accept(response,2,3,soc) && soc.battery==72);
    std::puts("Clean envelope proto3 defaults: omitted flags decode as zero but cannot bypass authenticated flags/AAD; original valid reply remains admissible passed");
}
