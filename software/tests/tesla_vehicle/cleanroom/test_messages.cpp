// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/messages.hpp>
#include <tesla_clean/charge_state.hpp>
#include "wire_vectors.hpp"
#include "vectors.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tesla_clean;
static int nonce(void *,unsigned char *p,size_t n) { if(n!=12) { return -1; } std::memcpy(p,wire_vectors::nonce,n); return 0; }
int main()
{
    SessionCrypto crypto; assert(crypto.set_key({wire_vectors::key,16})); SessionInfo session;
    session.counter=7; session.clock=1234; std::memcpy(session.epoch.data(),wire_vectors::epoch,16);
    Route route; route.domain=Domain::Infotainment; std::memcpy(route.uuid.data(),wire_vectors::uuid,16); std::memcpy(route.address.data(),wire_vectors::routing,16);
    OutgoingMessages messages(crypto,session,{vectors::vin,17},{wire_vectors::public_point,65});
    EncodedRequest output;
    assert(messages.build(get_charge_state_payload(),route,0,5,{nonce,nullptr},output));
    assert(output.length==sizeof(wire_vectors::outgoing) && !std::memcmp(output.bytes.data(),wire_vectors::outgoing,output.length));
    assert(output.identifier_length==17 && !std::memcmp(output.identifier.data(),wire_vectors::identifier,17) && output.counter==8 && messages.counter()==8);
    auto refused=[](void *,unsigned char *,size_t) { return -1; };
    assert(!messages.build(get_charge_state_payload(),route,0,5,{refused,nullptr},output) && output.length==0 && messages.counter()==8);
    const uint8_t byte=0; std::array<uint8_t,1024> too_large{};
    assert(!messages.build({too_large.data(),too_large.size()},route,0,5,{nonce,nullptr},output) && messages.counter()==8);
    assert(!messages.build({nullptr,1},route,0,5,{nonce,nullptr},output));
    assert(!messages.build({&byte,1},route,0,0,{nonce,nullptr},output));
    auto invalid=route; invalid.domain=static_cast<Domain>(1);
    assert(!messages.build({&byte,1},invalid,0,5,{nonce,nullptr},output) && messages.counter()==8);
    assert(messages.build(get_charge_state_payload(),route,0,5,{nonce,nullptr},output) && messages.counter()==9);
    session.counter=UINT32_MAX; OutgoingMessages exhausted(crypto,session,{vectors::vin,17},{wire_vectors::public_point,65});
    assert(!exhausted.build(get_charge_state_payload(),route,0,5,{nonce,nullptr},output) && exhausted.counter()==UINT32_MAX);
    session.counter=7; session.clock=UINT32_MAX-1; OutgoingMessages expired(crypto,session,{vectors::vin,17},{wire_vectors::public_point,65});
    assert(!expired.build(get_charge_state_payload(),route,0,5,{nonce,nullptr},output) && expired.counter()==7);
    Response response;
    assert(decode_encrypted_response({wire_vectors::response,sizeof(wire_vectors::response)},{wire_vectors::routing,16},response));
    assert(response.domain==Domain::Infotainment && response.counter==1 && response.flags==1 && response.fault==0 && response.uuid==route.uuid);
    std::array<uint8_t,16> wrong_route{}; assert(!decode_encrypted_response({wire_vectors::response,sizeof(wire_vectors::response)},{wrong_route.data(),16},response) && response.packet.length==0);
    for(size_t n=0;n<sizeof(wire_vectors::response);++n) {
        // Complete removal of the optional final proto3 flags field is wire-valid.
        // test_message_defaults independently requires its HMAC/GCM admission to fail.
        if(n==sizeof(wire_vectors::response)-3) {
            assert(decode_encrypted_response({wire_vectors::response,n},{wire_vectors::routing,16},response) && response.flags==0);
        } else { assert(!decode_encrypted_response({wire_vectors::response,n},{wire_vectors::routing,16},response)); }
    }
    auto corrupt=std::array<uint8_t,sizeof(wire_vectors::response)+3>{}; std::memcpy(corrupt.data(),wire_vectors::response,sizeof(wire_vectors::response));
    // Duplicate known flags are refused rather than allowing parser-default differences.
    corrupt[sizeof(wire_vectors::response)]=0xa0; corrupt[sizeof(wire_vectors::response)+1]=3; corrupt[sizeof(wire_vectors::response)+2]=1;
    assert(!decode_encrypted_response({corrupt.data(),corrupt.size()},{wire_vectors::routing,16},response));
    assert(decode_encrypted_response({wire_vectors::response,sizeof(wire_vectors::response)},{wire_vectors::routing,16},response));
    ChargeStateAdmission admission(crypto,{vectors::vin,17}); Request op; op.context=27; op.generation=(uint64_t{1}<<32)+3;
    op.started=op.transmitted=0; op.total_ms=1000; op.response_ms=500; op.uuid=route.uuid;
    op.identifier_length=17; std::memcpy(op.identifier.data(),wire_vectors::identifier,17);
    AuthenticatedSoc soc; assert(admission.arm(op) && admission.accept(response,1,op.generation,soc) && soc.battery==72 && soc.context==27);
    std::puts("Clean routable messages: independent AES-GCM/protobuf KAT, transactional encode counter, bounds/RNG/overflow refusal, routed encrypted-response decode and authenticated SoC passed");
}
