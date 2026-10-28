// SPDX-License-Identifier: Apache-2.0
// Official-semantics checks (teslamotors/vehicle-command a4b43c1e):
//  - pkg/protocol/protocol.md:62-65,596-691,751-758 and pkg/vehicle/vehicle.go:22:
//    requests set flags = 1<<FLAG_ENCRYPT_RESPONSE = 2 (universal_message.proto:75-78);
//    vehicles encrypt the reply only when that bit is set, otherwise plaintext.
//  - protocol.md:52-57 and internal/dispatcher/dispatcher.go:259-261: VCSEC replies
//    typically omit request_uuid; correlation is domain + routing address, while
//    peer.go responseMetadata binds the reply to TAG_REQUEST_HASH (request tag).
#include "controller_fixture.hpp"
#include <tesla_clean/messages.hpp>
#include <cstdio>
using namespace controller_fixture;
namespace {
std::vector<uint8_t> hex(const char *s)
{
    std::vector<uint8_t> out; auto v=[](char c){ return c<='9'?c-'0':c-'a'+10; };
    for(; s[0] && s[1]; s+=2) { out.push_back(static_cast<uint8_t>(v(s[0])*16+v(s[1]))); }
    return out;
}
std::vector<uint8_t> official_nonce;
int nonce_fill(void *,unsigned char *p,size_t n) { if(n!=12) { return -1; } std::memcpy(p,official_nonce.data(),12); return 0; }
// Vehicle model: encrypts only if the request carried FLAG_ENCRYPT_RESPONSE.
Frame vcsec_reply(Harness &h,const Sent &s,uint32_t request_flags,Bytes payload,bool with_uuid,uint32_t counter=1,
                  const std::array<uint8_t,17> *identifier=nullptr,const std::array<uint8_t,16> *uuid=nullptr)
{
    ProtoWriter<24> to,from; ProtoWriter<64> gcm; ProtoWriter<72> signature; ProtoWriter<1024> envelope;
    assert(to.block(2,{h.address.data(),16}) && from.integer(1,static_cast<uint32_t>(s.domain)));
    assert(envelope.block(6,to.bytes()) && envelope.block(7,from.bytes()));
    if(request_flags&2) {
        const uint32_t response_flags=0; // response's own flags field (protocol.md:800)
        const auto &id=identifier?*identifier:s.identifier;
        std::vector<uint8_t> transcript; const uint8_t method=9,domain=static_cast<uint8_t>(s.domain);
        tlv(transcript,0,{&method,1}); tlv(transcript,1,{&domain,1}); tlv(transcript,2,{vectors::vin,17});
        number(transcript,5,counter); number(transcript,7,response_flags); tlv(transcript,8,{id.data(),17}); number(transcript,9,0); transcript.push_back(255);
        std::array<uint8_t,32> aad{}; assert(!mbedtls_sha256(transcript.data(),transcript.size(),aad.data(),0));
        std::array<uint8_t,1024> cipher{}; std::array<uint8_t,16> tag{};
        mbedtls_gcm_context ctx; mbedtls_gcm_init(&ctx); assert(!mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,vectors::session_key,128));
        assert(!mbedtls_gcm_crypt_and_tag(&ctx,MBEDTLS_GCM_ENCRYPT,payload.size,vectors::nonce,12,aad.data(),32,payload.data,cipher.data(),16,tag.data())); mbedtls_gcm_free(&ctx);
        assert(gcm.block(1,{vectors::nonce,12}) && gcm.integer(2,counter) && gcm.block(3,{tag.data(),16}) && signature.block(9,gcm.bytes()));
        assert(envelope.block(10,{cipher.data(),payload.size}) && envelope.block(13,signature.bytes()));
    } else {
        assert(envelope.block(10,payload)); // plaintext reply, no AES_GCM_Response data
    }
    if(with_uuid) { assert(envelope.block(50,{(uuid?*uuid:s.uuid).data(),16})); }
    Frame f; auto wire=envelope.bytes(); f.length=wire.size; std::memcpy(f.data.data(),wire.data,wire.size); return f;
}
}
int main()
{
    int red=0;
    // 1) Official protocol.md:596-691 AES-GCM request KAT (flags 2 in AAD and wire).
    {
        official_nonce=hex("dbf79447fa156674dae1caed");
        const auto key=hex("1b2fce19967b79db696f909cff89ea9a"), epoch=hex("4c463f9cc0d3d26906e982ed224adde6");
        const auto pub=hex("04b2b6bc68c2da0665ce656815594996c62394edd8bea905fe781a754fe6a845a714330902f225e9269d466e05b349981fda9d85cc23c6fb444aa73b629105dc6e");
        const auto uuid=hex("58406580528b6a5301391800b4fe9b99"), address=hex("2c907bd76c640d360b3027dc7404efde");
        const auto payload=hex("120452020801"), want_cipher=hex("38038e8c0f2e"), want_tag=hex("c228e0ff64991481db3a7bbc133696c5");
        const char vin[]="5YJ30123456789ABC";
        SessionCrypto crypto; assert(crypto.set_key({key.data(),16})); SessionInfo session;
        session.counter=6; session.clock=2650; std::memcpy(session.epoch.data(),epoch.data(),16);
        Route route; route.domain=Domain::Infotainment; std::memcpy(route.uuid.data(),uuid.data(),16); std::memcpy(route.address.data(),address.data(),16);
        OutgoingMessages messages(crypto,session,{reinterpret_cast<const uint8_t *>(vin),17},{pub.data(),65});
        EncodedRequest out; assert(messages.build({payload.data(),payload.size()},route,0,5,{nonce_fill,nullptr},out) && out.counter==7);
        Bytes wire{out.bytes.data(),out.length};
        const auto cipher=field(wire,10); const auto gcm=field(field(wire,13),5); const auto tag=field(gcm,5);
        const bool flags_ok=integer(wire,52)==2, tag_ok=tag.size==16 && !std::memcmp(tag.data,want_tag.data(),16);
        const bool cipher_ok=cipher.size==want_cipher.size() && !std::memcmp(cipher.data,want_cipher.data(),cipher.size);
        std::printf("official KAT: flags=%u cipher=%s tag=%s expires=%u\n",integer(wire,52),cipher_ok?"match":"MISMATCH",tag_ok?"match":"MISMATCH",integer(gcm,4));
        if(!flags_ok || !tag_ok || !cipher_ok) { std::puts("RED: request flags/AAD differ from official FLAG_ENCRYPT_RESPONSE KAT"); red=1; }
    }
    // 2) Production pair-verification poll against the official vehicle model.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.status(21,h.callback(results),{5000,1000,10000})); h.vehicle.tick(); h.establish();
        const auto s=h.last(); assert(!s.hello && s.domain==Domain::Vcsec);
        const uint32_t flags=integer({s.frame.data.data(),s.frame.length},52);
        h.inject(vcsec_reply(h,s,flags,{awake,sizeof(awake)},false)); h.vehicle.tick();
        if(results.size()!=1 || results[0].code!=VehicleResultCode::Status || !results[0].authenticated) {
            std::printf("RED: no authenticated status (request flags=%u => vehicle reply %s, VCSEC request_uuid omitted)\n",flags,(flags&2)?"encrypted":"plaintext");
            red=1;
        }
    }
    if(red) { return 1; }
    // 3) Controls: authentication/correlation are not weakened.
    {
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.status(22,h.callback(results),{5000,1000,10000})); h.vehicle.tick(); h.establish();
        const auto s=h.last();
        h.inject(vcsec_reply(h,s,0,{awake,sizeof(awake)},false)); h.vehicle.tick();            // plaintext never authenticates
        assert(results.empty());
        std::array<uint8_t,16> other_uuid=s.uuid; other_uuid[0]^=1;
        h.inject(vcsec_reply(h,s,2,{awake,sizeof(awake)},true,1,nullptr,&other_uuid)); h.vehicle.tick(); // present but wrong UUID
        assert(results.empty());
        std::array<uint8_t,17> other_id=s.identifier; other_id[5]^=1;
        h.inject(vcsec_reply(h,s,2,{awake,sizeof(awake)},false,1,&other_id)); h.vehicle.tick();  // bound to another request hash
        assert(results.empty());
        h.address[0]^=1; Frame rerouted=vcsec_reply(h,s,2,{awake,sizeof(awake)},false); h.address[0]^=1; // other routing address
        h.inject(rerouted); h.vehicle.tick(); assert(results.empty());
        h.inject(vcsec_reply(h,s,2,{awake,sizeof(awake)},true)); h.vehicle.tick();             // exact UUID still accepted
        assert(results.size()==1 && results[0].code==VehicleResultCode::Status && results[0].authenticated && results[0].context==22);
    }
    {
        // Infotainment replies still require the exact request_uuid.
        Harness h; Sent s; s.domain=Domain::Infotainment; s.identifier[0]=5;
        Response r; const Frame f=vcsec_reply(h,s,2,{soc,sizeof(soc)},false);
        assert(!decode_encrypted_response({f.data.data(),f.length},{h.address.data(),16},r));
        const Frame g=vcsec_reply(h,s,2,{soc,sizeof(soc)},true);
        assert(decode_encrypted_response({g.data.data(),g.length},{h.address.data(),16},r) && r.domain==Domain::Infotainment);
    }
    {
        // Replay of an accepted VCSEC reply counter is refused by the guard.
        Harness h; std::vector<VehicleResult> results; assert(h.vehicle.open(h.link));
        assert(h.vehicle.status(23,h.callback(results),{5000,1000,10000})); h.vehicle.tick(); h.establish();
        const auto s=h.last(); SessionCrypto crypto; assert(crypto.set_key({vectors::session_key,16}));
        ResponseGuard guard(crypto,Domain::Vcsec,{vectors::vin,17}); Request q; q.uuid=s.uuid; q.context=1; q.generation=h.link;
        q.identifier_length=17; std::memcpy(q.identifier.data(),s.identifier.data(),17); q.total_ms=5000; q.response_ms=1000;
        const Frame f=vcsec_reply(h,s,2,{awake,sizeof(awake)},false,3); Response r; std::array<uint8_t,1024> pt{}; VerifiedReply v;
        assert(decode_encrypted_response({f.data.data(),f.length},{h.address.data(),16},r));
        assert(guard.arm(q) && guard.accept(r,0,h.link,pt,v) && v.counter==3);
        assert(guard.arm(q) && !guard.accept(r,0,h.link,pt,v));
    }
    std::puts("Official FLAG_ENCRYPT_RESPONSE KAT and VCSEC reply without request_uuid authenticated; plaintext/wrong UUID/wrong request hash/wrong route/Infotainment-without-UUID/replay refused");
}
