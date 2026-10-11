// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/charge_state.hpp>
#include <tesla_clean/protobuf.hpp>
#include <mbedtls/platform_util.h>
namespace tesla_clean {
namespace {
// Check only ordinary protobuf syntax on the supported nested message path.
// A syntactically valid action error/missing battery remains a semantic failure,
// not successful SoC. Unknown ordinary fields do not acquire recursive meaning.
bool well_formed(Bytes encoded,unsigned schema)
{
    ProtoReader reader(encoded); ProtoField field;
    while(reader.next(field)) {
        if((schema==0 && (field.number==1 || field.number==2)) || (schema==2 && field.number==3)) {
            const unsigned nested=schema==0?(field.number==1?1:2):3;
            if(field.wire!=2 || !well_formed(field.bytes,nested)) { return false; }
        } else if((schema==1 && field.number==1) || (schema==3 && field.number==114)) {
            if(field.wire!=0) { return false; }
        }
    }
    return reader.done();
}
bool validate_charge(Bytes encoded,void *) { return charge_state_well_formed(encoded); }
bool submessage(Bytes encoded,uint32_t field,Bytes &output)
{
    ProtoReader reader(encoded); ProtoField part; bool found=false;
    while(reader.next(part)) {
        if(part.number!=field) { continue; }
        if(found || part.wire!=2) { return false; }
        found=true; output=part.bytes;
    }
    return reader.done() && found;
}
bool action_ok(Bytes encoded)
{
    ProtoReader reader(encoded); ProtoField field; bool seen=false;
    while(reader.next(field)) {
        if(field.number!=1) { continue; }
        if(seen || field.wire!=0 || field.integer!=0) { return false; }
        seen=true;
    }
    return reader.done(); // proto3 omitted result means OK, but status message must exist
}
}
bool charge_state_well_formed(Bytes encoded) { return well_formed(encoded,0); }
Bytes get_charge_state_payload()
{
    // Official car_server.proto Action.2 / VehicleAction.1 / GetVehicleData.2={}
    static constexpr uint8_t query[]={0x12,0x04,0x0a,0x02,0x12,0x00};
    return {query,sizeof(query)};
}
bool decode_charge_state(Bytes encoded,uint8_t &battery)
{
    battery=0;
    Bytes data,status; ProtoReader reader(encoded); ProtoField field; unsigned seen=0;
    while(reader.next(field)) {
        if(field.number==3 || field.number==5 || field.number==9) { return false; }
        if(field.number!=1 && field.number!=2) { continue; }
        const unsigned bit=1u<<field.number;
        if((seen&bit) || field.wire!=2) { return false; }
        seen|=bit;
        if(field.number==1) { status=field.bytes; } else { data=field.bytes; }
    }
    Bytes charge;
    if(!reader.done() || seen!=6 || !action_ok(status) || !submessage(data,3,charge)) { return false; }
    ProtoReader charge_reader(charge); bool found=false; uint8_t candidate=0;
    while(charge_reader.next(field)) {
        if(field.number!=114) { continue; }
        if(found || field.wire!=0 || field.integer>100) { return false; }
        candidate=static_cast<uint8_t>(field.integer); found=true;
    }
    if(!charge_reader.done() || !found) { return false; }
    battery=candidate; return true;
}
bool ChargeStateAdmission::accept(const Response &response,uint32_t now,uint64_t generation,AuthenticatedSoc &output)
{
    output={}; std::array<uint8_t,1024> plaintext{}; VerifiedReply reply; uint8_t battery=0;
    const bool accepted=guard_.accept(response,now,generation,plaintext,reply,validate_charge) && reply.fault==0 &&
        decode_charge_state({plaintext.data(),reply.length},battery);
    mbedtls_platform_zeroize(plaintext.data(),plaintext.size());
    if(accepted) { output={reply.context,generation,reply.counter,battery}; }
    return accepted;
}
}
