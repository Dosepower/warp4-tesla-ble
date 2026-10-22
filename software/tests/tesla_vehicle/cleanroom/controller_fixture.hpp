// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/vehicle.hpp>
#include <tesla_clean/protobuf.hpp>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include "vectors.hpp"
#include <cassert>
#include <cstring>
#include <vector>
using namespace tesla_clean;
namespace controller_fixture {
struct Rng {
    mbedtls_ctr_drbg_context state;
    unsigned uuid=0;
    static int entropy(void *,unsigned char *p,size_t n) { std::memset(p,17,n); return 0; }
    static int fill(void *p,unsigned char *out,size_t n) {
        auto &r=*static_cast<Rng *>(p);
        if(n==16) { ++r.uuid; for(size_t i=0;i<n;++i) { out[i]=static_cast<uint8_t>(i+r.uuid); } return 0; }
        return mbedtls_ctr_drbg_random(&r.state,out,n);
    }
    Rng() { mbedtls_ctr_drbg_init(&state); assert(!mbedtls_ctr_drbg_seed(&state,entropy,nullptr,nullptr,0)); }
    ~Rng() { mbedtls_ctr_drbg_free(&state); }
    Random get() { return {fill,this}; }
};
inline void seed(IdentityStore &keys,Rng &r)
{
    P256Key key; std::array<uint8_t,32> scalar{}; scalar[31]=1;
    assert(key.import_scalar({scalar.data(),32},r.get()));
    std::array<uint8_t,512> pem{}; size_t n=0; assert(key.export_pem(pem.data(),512,n));
    assert(keys.import_pem({pem.data(),n},r.get()));
}
inline Bytes field(Bytes b,uint32_t id)
{
    ProtoReader reader(b); ProtoField f; while(reader.next(f)) { if(f.number==id) { assert(f.wire==2); return f.bytes; } }
    assert(reader.done()); return {};
}
inline uint32_t integer(Bytes b,uint32_t id)
{
    ProtoReader reader(b); ProtoField f;
    while(reader.next(f)) { if(f.number==id) {
        if(f.wire==5) { return uint32_t{f.bytes.data[0]}|(uint32_t{f.bytes.data[1]}<<8)|(uint32_t{f.bytes.data[2]}<<16)|(uint32_t{f.bytes.data[3]}<<24); }
        assert(f.wire==0 && f.integer<=UINT32_MAX); return static_cast<uint32_t>(f.integer);
    } }
    assert(reader.done()); return 0;
}
inline void tlv(std::vector<uint8_t> &out,uint8_t tag,Bytes b)
{
    assert(b.valid() && b.size<=255); out.push_back(tag); out.push_back(static_cast<uint8_t>(b.size));
    out.insert(out.end(),b.data,b.data+b.size);
}
inline void number(std::vector<uint8_t> &out,uint8_t tag,uint32_t x)
{
    const uint8_t b[]={static_cast<uint8_t>(x>>24),static_cast<uint8_t>(x>>16),static_cast<uint8_t>(x>>8),static_cast<uint8_t>(x)}; tlv(out,tag,{b,4});
}
inline std::array<uint8_t,32> session_tag(Bytes info,Bytes uuid)
{
    const auto *md=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256); assert(md);
    std::array<uint8_t,32> subkey{},tag{};
    const uint8_t label[]="session info"; assert(!mbedtls_md_hmac(md,vectors::session_key,16,label,sizeof(label)-1,subkey.data()));
    std::vector<uint8_t> transcript; const uint8_t method=6;
    tlv(transcript,0,{&method,1}); tlv(transcript,2,{vectors::vin,17}); tlv(transcript,6,uuid); transcript.push_back(255);
    transcript.insert(transcript.end(),info.data,info.data+info.size);
    assert(!mbedtls_md_hmac(md,subkey.data(),32,transcript.data(),transcript.size(),tag.data())); return tag;
}
struct Sent {
    Frame frame;
    Domain domain=Domain::Vcsec;
    std::array<uint8_t,16> uuid{};
    std::array<uint8_t,17> identifier{};
    uint32_t counter=0;
    bool hello=false,pair=false;
    std::vector<uint8_t> plaintext;
};
inline Sent decode(const Frame &f)
{
    Sent sent; sent.frame=f; Bytes wire{f.data.data(),f.length}; auto to=field(wire,6);
    if(!to.data) { sent.pair=true; return sent; }
    sent.domain=static_cast<Domain>(integer(to,1)); auto uuid=field(wire,51); assert(uuid.size==16); std::memcpy(sent.uuid.data(),uuid.data,16);
    auto hello=field(wire,14); sent.hello=hello.data!=nullptr;
    if(sent.hello) { assert(field(hello,1).size==65 && !field(hello,2).data); return sent; }
    auto gcm=field(field(wire,13),5); auto tag=field(gcm,5); auto nonce=field(gcm,2); auto epoch=field(gcm,1);
    assert(tag.size==16 && nonce.size==12 && epoch.size==16); sent.counter=integer(gcm,3);
    sent.identifier[0]=5; std::memcpy(sent.identifier.data()+1,tag.data,16);
    std::vector<uint8_t> transcript; const uint8_t method=5,domain=static_cast<uint8_t>(sent.domain);
    tlv(transcript,0,{&method,1}); tlv(transcript,1,{&domain,1}); tlv(transcript,2,{vectors::vin,17}); tlv(transcript,3,epoch);
    number(transcript,4,integer(gcm,4)); number(transcript,5,sent.counter); number(transcript,7,integer(wire,52)); transcript.push_back(255);
    std::array<uint8_t,32> aad{}; assert(!mbedtls_sha256(transcript.data(),transcript.size(),aad.data(),0));
    auto cipher=field(wire,10); sent.plaintext.resize(cipher.size);
    mbedtls_gcm_context ctx; mbedtls_gcm_init(&ctx); assert(!mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,vectors::session_key,128));
    assert(!mbedtls_gcm_auth_decrypt(&ctx,cipher.size,nonce.data,12,aad.data(),32,tag.data,16,cipher.data,sent.plaintext.data()));
    mbedtls_gcm_free(&ctx); return sent;
}
struct Harness {
    Rng rng;
    IdentityStore keys;
    std::array<uint8_t,16> address{};
    uint32_t clock=100;
    uint64_t link=5;
    bool accepted=true;
    bool accept_final=true,recoverable=true;
    unsigned recoveries=0;
    unsigned writes=0;
    std::vector<uint8_t> pending;
    std::vector<Sent> sent;
    void (*hook)(Harness &)=nullptr;
    void (*on_frame)(Harness &)=nullptr;
    Vehicle vehicle;
    static uint32_t now(void *p) { return static_cast<Harness *>(p)->clock; }
    static bool owns(void *p,uint64_t g) { return static_cast<Harness *>(p)->link==g; }
    static bool write(void *p,Bytes b,uint64_t g) {
        auto &h=*static_cast<Harness *>(p); assert(g==h.link); ++h.writes;
        h.pending.insert(h.pending.end(),b.data,b.data+b.size);
        if(h.pending.size()>=2) {
            const size_t wanted=size_t{h.pending[0]}*256+h.pending[1]+2;
            if(h.pending.size()==wanted) {
                Frame f; f.length=wanted-2; std::memcpy(f.data.data(),h.pending.data()+2,f.length); h.pending.clear(); h.sent.push_back(decode(f));
                if(h.on_frame) { auto hook=h.on_frame; h.on_frame=nullptr; hook(h); }
            } else { assert(h.pending.size()<wanted); }
        }
        if(h.hook) { auto hook=h.hook; h.hook=nullptr; hook(h); }
        return h.accepted;
    }
    static bool recover(void *p,uint64_t generation) {
        auto &h=*static_cast<Harness *>(p); assert(generation==h.link); ++h.recoveries;
        if(!h.recoverable) { return false; }
        // This mock explicitly resets its receiver's partial frame. A real backend
        // cannot make this claim without retiring/resetting the peer transport.
        h.pending.clear(); return true;
    }
    VehiclePorts ports() { return {rng.get(),now,write,owns,this,20,recover}; }
    Harness() : vehicle(keys,{vectors::vin,17},{address.data(),16},ports()) { seed(keys,rng); }
    void inject(const Frame &f,uint64_t generation,size_t fragment=7) {
        std::vector<uint8_t> bytes{static_cast<uint8_t>(f.length>>8),static_cast<uint8_t>(f.length)};
        bytes.insert(bytes.end(),f.data.begin(),f.data.begin()+f.length);
        for(size_t offset=0;offset<bytes.size();) { const size_t n=std::min(fragment,bytes.size()-offset); vehicle.receive({bytes.data()+offset,n},generation); offset+=n; }
    }
    void inject(const Frame &f,size_t fragment=7) { inject(f,link,fragment); }
    Frame hello(const Sent &s,bool denied=false,uint32_t counter=7,uint32_t time=1234,bool bad=false) {
        std::array<uint8_t,sizeof(vectors::denied_info)> body{};
        std::memcpy(body.data(),denied?vectors::denied_info:vectors::encoded_info,denied?sizeof(vectors::denied_info):sizeof(vectors::encoded_info));
        assert(counter<128); body[1]=static_cast<uint8_t>(counter);
        const size_t clock_offset=sizeof(vectors::encoded_info)-4;
        for(unsigned i=0;i<4;++i) { body[clock_offset+i]=static_cast<uint8_t>(time>>(8*i)); }
        auto tag=session_tag({body.data(),denied?sizeof(vectors::denied_info):sizeof(vectors::encoded_info)},{s.uuid.data(),16}); if(bad) { tag[0]^=1; }
        ProtoWriter<24> to,from; ProtoWriter<40> hmac; ProtoWriter<48> sig; ProtoWriter<1024> out;
        assert(to.block(2,{address.data(),16}) && from.integer(1,static_cast<uint32_t>(s.domain)) && hmac.block(1,{tag.data(),32}) && sig.block(6,hmac.bytes()));
        assert(out.block(6,to.bytes()) && out.block(7,from.bytes()) && out.block(15,{body.data(),denied?sizeof(vectors::denied_info):sizeof(vectors::encoded_info)}) && out.block(13,sig.bytes()) && out.block(50,{s.uuid.data(),16}));
        Frame f; auto wire=out.bytes(); f.length=wire.size; std::memcpy(f.data.data(),wire.data,wire.size); return f;
    }
    Frame response(const Sent &s,Bytes payload,uint32_t counter=1,uint32_t fault=0,bool bad=false) {
        std::vector<uint8_t> transcript; const uint8_t method=9,domain=static_cast<uint8_t>(s.domain);
        tlv(transcript,0,{&method,1}); tlv(transcript,1,{&domain,1}); tlv(transcript,2,{vectors::vin,17});
        number(transcript,5,counter); number(transcript,7,1); tlv(transcript,8,{s.identifier.data(),17}); number(transcript,9,fault); transcript.push_back(255);
        std::array<uint8_t,32> aad{}; assert(!mbedtls_sha256(transcript.data(),transcript.size(),aad.data(),0));
        std::array<uint8_t,1024> cipher{}; std::array<uint8_t,16> tag{};
        mbedtls_gcm_context ctx; mbedtls_gcm_init(&ctx); assert(!mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,vectors::session_key,128));
        assert(!mbedtls_gcm_crypt_and_tag(&ctx,MBEDTLS_GCM_ENCRYPT,payload.size,vectors::nonce,12,aad.data(),32,payload.data,cipher.data(),16,tag.data())); mbedtls_gcm_free(&ctx);
        if(bad) { tag[0]^=1; }
        ProtoWriter<24> to,from; ProtoWriter<64> gcm; ProtoWriter<72> signature; ProtoWriter<8> status; ProtoWriter<1024> envelope;
        assert(to.block(2,{address.data(),16}) && from.integer(1,static_cast<uint32_t>(s.domain)) && gcm.block(1,{vectors::nonce,12}) && gcm.integer(2,counter) && gcm.block(3,{tag.data(),16}) && signature.block(9,gcm.bytes()));
        assert(status.integer(2,fault));
        assert(envelope.block(6,to.bytes()) && envelope.block(7,from.bytes()) && envelope.block(10,{cipher.data(),payload.size}) && envelope.block(12,status.bytes()) && envelope.block(13,signature.bytes()) && envelope.block(50,{s.uuid.data(),16}) && envelope.integer(52,1));
        Frame f; auto wire=envelope.bytes(); f.length=wire.size; std::memcpy(f.data.data(),wire.data,wire.size); return f;
    }
    Sent last() const { assert(!sent.empty()); return sent.back(); }
    VehicleCompletion callback(std::vector<VehicleResult> &results) { VehicleCompletion cb; assert(cb.assign([&results](const VehicleResult &r) { results.push_back(r); })); return cb; }
    void establish() { auto s=last(); assert(s.hello); inject(hello(s)); vehicle.tick(); }
};
inline constexpr uint8_t awake[]={0x0a,2,0x18,1};
inline constexpr uint8_t asleep[]={0x0a,2,0x18,2};
inline constexpr uint8_t soc[]={0x0a,0,0x12,5,0x1a,3,0x90,7,72};
}
