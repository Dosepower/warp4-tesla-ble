// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/session_wire.hpp>
#include <tesla_clean/protobuf.hpp>
namespace tesla_clean {
// Source: official universal_message.proto:16-20,69-101; signatures.proto:55-74.
bool encode_session_request(const ChallengeView &challenge,Bytes address,Frame &out)
{
    out={};
    if(!address.valid() || address.size!=16 || challenge.public_point[0]!=4 ||
       (challenge.domain!=Domain::Vcsec && challenge.domain!=Domain::Infotainment)) { return false; }
    ProtoWriter<24> to,from; ProtoWriter<72> body; ProtoWriter<128> envelope;
    if(!to.integer(1,static_cast<uint32_t>(challenge.domain)) || !from.block(2,address) ||
       !body.block(1,{challenge.public_point.data(),65}) || !envelope.block(6,to.bytes()) ||
       !envelope.block(7,from.bytes()) || !envelope.block(14,body.bytes()) ||
       !envelope.block(51,{challenge.uuid.data(),16})) { return false; }
    const auto wire=envelope.bytes(); std::memcpy(out.data.data(),wire.data,wire.size); out.length=wire.size; return true;
}
namespace {
bool routed(Bytes input,bool domain,Bytes address,Domain &value)
{
    ProtoReader reader(input); ProtoField field; bool seen=false;
    while(reader.next(field)) {
        if(field.number!=1 && field.number!=2) { continue; }
        if(seen || field.number!=(domain?1u:2u)) { return false; } seen=true;
        if(domain) {
            if(field.wire!=0 || (field.integer!=2 && field.integer!=3)) { return false; }
            value=static_cast<Domain>(field.integer);
        } else if(field.wire!=2 || field.bytes.size!=16 || std::memcmp(field.bytes.data,address.data,16)) { return false; }
    }
    return reader.done() && seen;
}
bool tag_value(Bytes signature,std::array<uint8_t,32> &output)
{
    ProtoReader reader(signature); ProtoField field; Bytes hmac; bool seen=false;
    while(reader.next(field)) {
        if(field.number==5 || field.number==8 || field.number==9) { return false; }
        if(field.number!=6) { continue; }
        if(seen || field.wire!=2) { return false; } seen=true; hmac=field.bytes;
    }
    if(!reader.done() || !seen) { return false; }
    ProtoReader tags(hmac); seen=false;
    while(tags.next(field)) {
        if(field.number!=1) { continue; }
        if(seen || field.wire!=2 || field.bytes.size!=32) { return false; } seen=true;
        std::memcpy(output.data(),field.bytes.data,32);
    }
    return tags.done() && seen;
}
}
bool decode_session_reply(Bytes wire,Bytes address,SessionWireReply &out)
{
    out={};
    if(!wire.valid() || !address.valid() || address.size!=16) { return false; }
    ProtoReader reader(wire); ProtoField field; Bytes to,from,body,signature,uuid,status; unsigned seen=0;
    while(reader.next(field)) {
        Bytes *target=nullptr; unsigned bit=0;
        switch(field.number) {
        case 6: target=&to; bit=1; break;
        case 7: target=&from; bit=2; break;
        case 15: target=&body; bit=4; break;
        case 13: target=&signature; bit=8; break;
        case 50: target=&uuid; bit=16; break;
        case 12: target=&status; bit=32; break;
        case 52: bit=64; break;
        case 10: case 14: return false;
        default: continue;
        }
        if(seen&bit) { return false; } seen|=bit;
        if(target) { if(field.wire!=2) { return false; } *target=field.bytes; }
        else if(field.wire!=0 || field.integer>UINT32_MAX) { return false; }
    }
    SessionWireReply candidate;
    if(!reader.done() || (seen&31)!=31 || body.size==0 || body.size>candidate.encoded.size() || uuid.size!=16 ||
       !routed(to,false,address,candidate.domain) || !routed(from,true,{},candidate.domain) || !tag_value(signature,candidate.tag)) { return false; }
    if(seen&32) {
        ProtoReader s(status); unsigned fields=0;
        while(s.next(field)) {
            if(field.number!=1 && field.number!=2) { continue; }
            const unsigned bit=1u<<field.number;
            if((fields&bit) || field.wire!=0 || field.integer>(field.number==1?2u:28u)) { return false; } fields|=bit;
        }
        if(!s.done()) { return false; }
    }
    std::memcpy(candidate.uuid.data(),uuid.data,16); std::memcpy(candidate.encoded.data(),body.data,body.size);
    candidate.length=body.size; out=candidate; return true;
}
}
