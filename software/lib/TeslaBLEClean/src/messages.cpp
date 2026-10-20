// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/messages.hpp>
#include <tesla_clean/protobuf.hpp>
namespace tesla_clean {
namespace {
bool scalar(Bytes input,uint32_t wanted,uint32_t &output)
{
    output=0; ProtoReader reader(input); ProtoField field; bool found=false;
    while(reader.next(field)) {
        if(field.number!=wanted) { continue; }
        if(found || field.wire!=0 || field.integer>UINT32_MAX) { return false; }
        output=static_cast<uint32_t>(field.integer); found=true;
    }
    return reader.done(); // omitted scalar is a proto3 default
}
bool block(Bytes input,uint32_t wanted,Bytes &output)
{
    output={}; ProtoReader reader(input); ProtoField field; bool found=false;
    while(reader.next(field)) {
        if(field.number!=wanted) { continue; }
        if(found || field.wire!=2) { return false; }
        output=field.bytes; found=true;
    }
    return reader.done() && found;
}
bool destination(Bytes input,bool domain,Bytes address,uint32_t &value)
{
    ProtoReader reader(input); ProtoField field; bool found=false;
    while(reader.next(field)) {
        if(field.number!=1 && field.number!=2) { continue; }
        if(found || (domain?field.number!=1:field.number!=2)) { return false; }
        found=true;
        if(domain) {
            if(field.wire!=0 || (field.integer!=2 && field.integer!=3)) { return false; }
            value=static_cast<uint32_t>(field.integer);
        } else if(field.wire!=2 || field.bytes.size!=address.size || std::memcmp(field.bytes.data,address.data,address.size)) { return false; }
    }
    return reader.done() && found;
}
}
OutgoingMessages::OutgoingMessages(const SessionCrypto &crypto,const SessionInfo &session,Bytes vin,Bytes public_point)
    : crypto_(crypto),session_(session),counter_(session.counter)
{
    valid_=vin.valid() && vin.size==vin_.size() && public_point.valid() && public_point.size==public_.size() && public_point.data[0]==4;
    if(valid_) { std::memcpy(vin_.data(),vin.data,vin.size); std::memcpy(public_.data(),public_point.data,public_point.size); }
}
bool OutgoingMessages::build(Bytes payload,const Route &route,uint32_t elapsed_ms,uint32_t ttl_seconds,Random random,EncodedRequest &output)
{
    // Output/input aliasing is refused before clearing the output structure.
    const auto first=reinterpret_cast<uintptr_t>(payload.data),second=reinterpret_cast<uintptr_t>(&output);
    const bool overlap=payload.size && payload.data && (first<=second?second-first<payload.size:first-second<sizeof(output));
    output={};
    if(overlap || !valid_ || !payload.valid() || payload.size==0 || payload.size>800 || counter_==UINT32_MAX || ttl_seconds==0 ||
       ttl_seconds>60 || elapsed_ms>INT32_MAX || (route.domain!=Domain::Vcsec && route.domain!=Domain::Infotainment)) { return false; }
    const uint64_t expires=uint64_t{session_.clock}+elapsed_ms/1000+ttl_seconds;
    if(expires>UINT32_MAX) { return false; }
    const uint32_t candidate=counter_+1;
    Metadata meta; const uint8_t method=5,domain=static_cast<uint8_t>(route.domain); std::array<uint8_t,32> aad{};
    if(!meta.add(0,{&method,1}) || !meta.add(1,{&domain,1}) || !meta.add(2,{vin_.data(),vin_.size()}) ||
       !meta.add(3,{session_.epoch.data(),session_.epoch.size()}) || !meta.integer(4,static_cast<uint32_t>(expires)) ||
       !meta.integer(5,candidate) || !meta.integer(7,1) || !meta.sha256({},aad)) { return false; }
    GcmPacket encrypted;
    if(!crypto_.encrypt(payload,{aad.data(),aad.size()},random,encrypted)) { return false; }
    ProtoWriter<24> to,from; ProtoWriter<72> identity; ProtoWriter<80> gcm; ProtoWriter<192> signature; ProtoWriter<1024> message;
    if(!to.integer(1,domain) || !from.block(2,{route.address.data(),route.address.size()}) || !identity.block(1,{public_.data(),public_.size()}) ||
       !gcm.block(1,{session_.epoch.data(),16}) || !gcm.block(2,{encrypted.nonce.data(),12}) || !gcm.integer(3,candidate) ||
       !gcm.fixed32(4,static_cast<uint32_t>(expires)) || !gcm.block(5,{encrypted.tag.data(),16}) ||
       !signature.block(1,identity.bytes()) || !signature.block(5,gcm.bytes()) || !message.block(6,to.bytes()) ||
       !message.block(7,from.bytes()) || !message.block(10,{encrypted.data.data(),encrypted.length}) || !message.block(13,signature.bytes()) ||
       !message.block(51,{route.uuid.data(),16}) || !message.integer(52,1)) { return false; }
    const Bytes wire=message.bytes();
    std::memcpy(output.bytes.data(),wire.data,wire.size); output.length=wire.size;
    output.identifier[0]=5; std::memcpy(output.identifier.data()+1,encrypted.tag.data(),16); output.identifier_length=17;
    output.counter=candidate; counter_=candidate;
    return true;
}
bool decode_encrypted_response(Bytes input,Bytes routing_address,Response &output)
{
    output={};
    if(!input.valid() || !routing_address.valid() || routing_address.size!=16) { return false; }
    Bytes to,from,payload,signature,status,uuid; uint32_t flags=0; unsigned seen=0;
    ProtoReader reader(input); ProtoField field;
    while(reader.next(field)) {
        unsigned bit=0; Bytes *target=nullptr;
        switch(field.number) {
        case 6: bit=1; target=&to; break;
        case 7: bit=2; target=&from; break;
        case 10: bit=4; target=&payload; break;
        case 12: bit=8; target=&status; break;
        case 13: bit=16; target=&signature; break;
        case 50: bit=32; target=&uuid; break;
        case 52: bit=64; break;
        case 14: case 15: return false;
        default: continue;
        }
        if(seen&bit) { return false; } seen|=bit;
        if(target) { if(field.wire!=2) { return false; } *target=field.bytes; }
        else { if(field.wire!=0 || field.integer>UINT32_MAX) { return false; } flags=static_cast<uint32_t>(field.integer); }
    }
    if(!reader.done() || (seen&55)!=55 || uuid.size!=16 || payload.size>1024) { return false; }
    Response candidate; uint32_t domain=0,unused=0;
    if(!destination(to,false,routing_address,unused) || !destination(from,true,{},domain)) { return false; }
    candidate.domain=static_cast<Domain>(domain); candidate.flags=flags;
    if((seen&8) && (!scalar(status,2,candidate.fault) || !scalar(status,1,unused) || unused>2)) { return false; }
    // Only an AES-GCM response signature is admissible; reject another oneof member.
    ProtoReader sig_reader(signature);
    while(sig_reader.next(field)) { if(field.number==5 || field.number==6 || field.number==8) { return false; } }
    Bytes gcm,nonce,tag;
    if(!sig_reader.done() || !block(signature,9,gcm) || !block(gcm,1,nonce) || !block(gcm,3,tag) ||
       !scalar(gcm,2,candidate.counter) || nonce.size!=12 || tag.size!=16) { return false; }
    std::memcpy(candidate.uuid.data(),uuid.data,16); std::memcpy(candidate.packet.nonce.data(),nonce.data,12);
    std::memcpy(candidate.packet.tag.data(),tag.data,16);
    if(payload.size) { std::memcpy(candidate.packet.data.data(),payload.data,payload.size); }
    candidate.packet.length=payload.size; output=candidate; return true;
}
}
