// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/charge_state.hpp>
#include "vectors.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tesla_clean;
static Response encrypted(const Request &request,Bytes payload,uint32_t counter,uint32_t fault=0)
{
    SessionCrypto crypto; assert(crypto.set_key({vectors::key,16})); Response r;
    r.domain=Domain::Infotainment; r.uuid=request.uuid; r.counter=counter; r.flags=1; r.fault=fault;
    std::array<uint8_t,32> aad{};
    assert(response_aad(r.domain,{vectors::vin,17},{request.identifier.data(),request.identifier_length},counter,1,fault,aad));
    auto nonce=[](void *,unsigned char *p,size_t n) { assert(n==12); std::memcpy(p,vectors::nonce,n); return 0; };
    assert(crypto.encrypt(payload,{aad.data(),aad.size()},{nonce,nullptr},r.packet)); return r;
}
int main()
{
    constexpr uint8_t query[]={0x12,0x04,0x0a,0x02,0x12,0x00};
    auto request=get_charge_state_payload(); assert(request.size==sizeof(query) && !std::memcmp(request.data,query,sizeof(query)));
    // CarServer.Response.actionStatus={}, VehicleData.charge_state.battery_level=72.
    std::array<uint8_t,11> payload{0x0a,0x00,0x12,0x07,0x1a,0x05,0x90,0x07,0x48,0x08,0x00};
    uint8_t battery=255; assert(decode_charge_state({payload.data(),payload.size()},battery) && battery==72);
    payload[8]=0; assert(decode_charge_state({payload.data(),payload.size()},battery) && battery==0);
    payload[8]=100; assert(decode_charge_state({payload.data(),payload.size()},battery) && battery==100);
    payload[8]=101; assert(!decode_charge_state({payload.data(),payload.size()},battery) && battery==0);
    payload[8]=72;
    for(size_t n=0;n<payload.size();++n) { assert(!decode_charge_state({payload.data(),n},battery) && battery==0); }
    const uint8_t absent[]={0x0a,0,0x12,2,0x1a,0}; assert(!decode_charge_state({absent,sizeof(absent)},battery));
    const uint8_t usable_only[]={0x0a,0,0x12,5,0x1a,3,0x98,7,72}; assert(!decode_charge_state({usable_only,sizeof(usable_only)},battery));
    const uint8_t duplicate[]={0x0a,0,0x12,8,0x1a,6,0x90,7,72,0x90,7,73}; assert(!decode_charge_state({duplicate,sizeof(duplicate)},battery));
    const uint8_t error[]={0x0a,2,8,1,0x12,5,0x1a,3,0x90,7,72}; assert(!decode_charge_state({error,sizeof(error)},battery));
    const uint8_t no_status[]={0x12,5,0x1a,3,0x90,7,72}; assert(!decode_charge_state({no_status,sizeof(no_status)},battery));
    const uint8_t conflicting[]={0x0a,0,0x12,5,0x1a,3,0x90,7,72,0x1a,0}; assert(!decode_charge_state({conflicting,sizeof(conflicting)},battery));
    const uint8_t negative[]={0x0a,0,0x12,14,0x1a,12,0x90,7,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,1};
    assert(!decode_charge_state({negative,sizeof(negative)},battery));
    assert(!decode_charge_state({nullptr,7},battery));
    SessionCrypto crypto; assert(crypto.set_key({vectors::key,16})); ChargeStateAdmission admission(crypto,{vectors::vin,17});
    Request op; op.context=0x100000005; op.generation=0x200000009; op.started=100; op.transmitted=200; op.total_ms=1000; op.response_ms=500;
    std::memcpy(op.uuid.data(),vectors::challenge,16); std::memcpy(op.identifier.data(),vectors::request_hash,17); op.identifier_length=17;
    assert(admission.arm(op)); auto r=encrypted(op,{payload.data(),payload.size()},1); AuthenticatedSoc result;
    auto wrong=r; wrong.domain=Domain::Vcsec; assert(!admission.accept(wrong,201,op.generation,result) && result.context==0);
    wrong=r; wrong.uuid[0]^=1; assert(!admission.accept(wrong,201,op.generation,result));
    wrong=r; wrong.packet.tag[0]^=1; assert(!admission.accept(wrong,201,op.generation,result));
    assert(!admission.accept(r,201,9,result));
    const uint8_t broken_status[]={10,1,255};
    const auto malformed=encrypted(op,{broken_status,sizeof(broken_status)},1);
    assert(!admission.accept(malformed,201,op.generation,result) && result.context==0);
    assert(admission.accept(r,201,op.generation,result) && result.battery==72 && result.context==op.context && result.generation==op.generation && result.counter==1);
    assert(!admission.accept(r,202,op.generation,result) && result.context==0);
    assert(admission.arm(op)); assert(!admission.accept(r,202,op.generation,result));
    auto failed=encrypted(op,{payload.data(),payload.size()},2,6); assert(!admission.accept(failed,202,op.generation,result));
    assert(admission.arm(op)); auto invalid=encrypted(op,{absent,sizeof(absent)},3); assert(!admission.accept(invalid,203,op.generation,result));
    assert(admission.arm(op)); r=encrypted(op,{payload.data(),payload.size()},4);
    assert(!admission.accept(r,700,op.generation,result) && result.context==0);
    admission.cancel(); assert(admission.arm(op)); assert(admission.accept(r,204,op.generation,result));
    std::puts("Clean ChargeState: exact GetChargeState subset, battery presence/range/malformed/error refusal and authenticated UUID/hash/domain/full-width generation/replay/deadline SoC correlation passed");
}
