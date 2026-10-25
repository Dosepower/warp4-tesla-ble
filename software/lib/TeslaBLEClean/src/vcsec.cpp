// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/vcsec.hpp>
#include <tesla_clean/protobuf.hpp>
namespace tesla_clean {
bool pair_charging_manager(const IdentityLease &identity,VcsecPacket &output,KeyFormFactor form_factor)
{
    output={}; std::array<uint8_t,65> point{};
    if(!identity.public_point(point)) { return false; }
    // Official vcsec.go addKeyPayload:151-168 / security.go
    // SendAddKeyRequestWithRole:338-359. Default preserves production CloudKey;
    // dedicated local embedded bench explicitly selects Unknown (no such enum).
    ProtoWriter<72> key; ProtoWriter<80> permission; ProtoWriter<8> metadata;
    ProtoWriter<96> whitelist; ProtoWriter<104> unsigned_message; ProtoWriter<112> signed_message; ProtoWriter<128> envelope;
    if(!key.block(1,{point.data(),point.size()}) || !permission.block(1,key.bytes()) || !permission.integer(4,6) ||
       !metadata.integer(1,static_cast<uint8_t>(form_factor)) || !whitelist.block(5,permission.bytes()) || !whitelist.block(6,metadata.bytes()) ||
       !unsigned_message.block(16,whitelist.bytes()) || !signed_message.block(2,unsigned_message.bytes()) || !signed_message.integer(3,2) ||
       !envelope.block(1,signed_message.bytes())) { return false; }
    const Bytes wire=envelope.bytes(); std::memcpy(output.bytes.data(),wire.data,wire.size); output.length=wire.size;
    return true;
}
Bytes status_payload()
{
    static constexpr uint8_t query[]={0x0a,0}; return {query,sizeof(query)};
}
bool wake_payload(bool explicitly_authorized,VcsecPacket &output)
{
    output={};
    if(!explicitly_authorized) { return false; }
    output.bytes[0]=0x10; output.bytes[1]=30; output.length=2; return true;
}
bool decode_vcsec_status(Bytes input,VcsecStatus &output)
{
    output={}; ProtoReader reader(input); ProtoField field; bool found=false; VcsecStatus candidate;
    while(reader.next(field)) {
        if(field.number==16 || field.number==17 || field.number==46 ||
           (field.number>=6 && field.number<=15) || field.number==50 || field.number==51 || field.number==52) { return false; }
        if(field.number!=1 && field.number!=4) { continue; }
        if(found || field.wire!=2) { return false; } found=true;
        const bool vehicle=field.number==1; ProtoReader sub(field.bytes); unsigned seen=0; bool whitelist=false;
        candidate.kind=vehicle?VcsecKind::Vehicle:VcsecKind::Pairing;
        uint32_t operation=0,whitelist_operation=0;
        while(sub.next(field)) {
            if(vehicle) {
                if(field.number!=3) { continue; }
                if(seen || field.wire!=0 || field.integer>2) { return false; } seen=1;
                candidate.sleep=static_cast<SleepState>(field.integer);
            } else if(field.number==1) {
                if(seen&1 || field.wire!=0 || field.integer>UINT32_MAX) { return false; } seen|=1;
                operation=static_cast<uint32_t>(field.integer);
            } else if(field.number==2) { return false; } // signed-message status is not pairing completion
            else if(field.number==3) {
                if(seen&2 || field.wire!=2) { return false; } seen|=2; whitelist=true;
                ProtoReader details(field.bytes); ProtoField part; unsigned detail_seen=0;
                while(details.next(part)) {
                    if(part.number!=1 && part.number!=3) { continue; }
                    const unsigned bit=1u<<part.number;
                    if(detail_seen&bit || part.wire!=0 || part.integer>UINT32_MAX) { return false; }
                    detail_seen|=bit;
                    if(part.number==1) { candidate.information=static_cast<uint32_t>(part.integer); }
                    else { whitelist_operation=static_cast<uint32_t>(part.integer); }
                }
                if(!details.done()) { return false; }
            }
        }
        if(!sub.done() || (!vehicle && !whitelist && operation!=1)) { return false; }
        if(!vehicle) {
            // protocol.md:824-839: outer WAIT without details means NFC wait;
            // other legacy outer values never override a populated result.
            candidate.whitelist=whitelist;
            candidate.outer_operation=operation;
            candidate.whitelist_operation=whitelist_operation;
            candidate.operation=!whitelist?VcsecOperation::Waiting:
                candidate.information || whitelist_operation>1?VcsecOperation::Error:
                whitelist_operation==1?VcsecOperation::Waiting:VcsecOperation::Success;
        }
    }
    if(!reader.done() || !found) { return false; }
    output=candidate; return true;
}
bool decode_pairing_reply(Bytes input,Bytes address,VcsecStatus &output)
{
    if(decode_vcsec_status(input,output)) { return output.kind==VcsecKind::Pairing; }
    output={};
    if(!address.valid() || address.size!=16) { return false; }
    ProtoReader reader(input); ProtoField f; Bytes to,from,payload; unsigned seen=0;
    while(reader.next(f)) {
        Bytes *target=nullptr; unsigned bit=0;
        switch(f.number) {
        case 6: target=&to;bit=1;break;
        case 7: target=&from;bit=2;break;
        case 10: target=&payload;bit=4;break;
        // Only the documented unencrypted application status route. UUID/HMAC
        // session processing stays strict and independent in session_wire.cpp.
        default: return false;
        }
        if((seen&bit) || f.wire!=2) { return false; } seen|=bit; *target=f.bytes;
    }
    if(!reader.done() || seen!=7) { return false; }
    ProtoReader destination(to),source(from);
    if(!destination.next(f) || f.number!=2 || f.wire!=2 || f.bytes.size!=16 ||
       std::memcmp(f.bytes.data,address.data,16) || !destination.done()) { return false; }
    if(!source.next(f) || f.number!=1 || f.wire!=0 || f.integer!=2 || !source.done()) { return false; }
    return decode_vcsec_status(payload,output) && output.kind==VcsecKind::Pairing;
}
}
