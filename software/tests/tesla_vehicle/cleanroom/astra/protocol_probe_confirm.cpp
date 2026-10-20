// Review-only confirmations. Current public API/fixtures and pinned Apache wire contract.
#include "controller_fixture.hpp"
#include <algorithm>
#include <cstdio>
#include <string>
using namespace controller_fixture;

// Unlike the original fixture, use a distinct nonce for every request/counter.
static Frame reply(Harness &h, const Sent &s, Bytes payload, uint32_t counter) {
    std::array<uint8_t,12> nonce{};
    std::copy_n(s.uuid.begin(),8,nonce.begin());
    for(unsigned i=0;i<4;++i) nonce[8+i]=static_cast<uint8_t>(counter>>(24-8*i));
    std::vector<uint8_t> transcript; const uint8_t method=9,domain=static_cast<uint8_t>(s.domain);
    tlv(transcript,0,{&method,1});tlv(transcript,1,{&domain,1});tlv(transcript,2,{vectors::vin,17});
    number(transcript,5,counter);number(transcript,7,1);tlv(transcript,8,{s.identifier.data(),17});number(transcript,9,0);transcript.push_back(255);
    std::array<uint8_t,32> aad{};assert(!mbedtls_sha256(transcript.data(),transcript.size(),aad.data(),0));
    std::array<uint8_t,1024> cipher{};std::array<uint8_t,16> tag{};
    mbedtls_gcm_context ctx;mbedtls_gcm_init(&ctx);assert(!mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,vectors::session_key,128));
    assert(!mbedtls_gcm_crypt_and_tag(&ctx,MBEDTLS_GCM_ENCRYPT,payload.size,nonce.data(),12,aad.data(),32,payload.data,cipher.data(),16,tag.data()));mbedtls_gcm_free(&ctx);
    ProtoWriter<24> to,from;ProtoWriter<64> gcm;ProtoWriter<72> signature;ProtoWriter<1024> envelope;
    assert(to.block(2,{h.address.data(),16})&&from.integer(1,static_cast<uint32_t>(s.domain))&&gcm.block(1,{nonce.data(),12})&&gcm.integer(2,counter)&&gcm.block(3,{tag.data(),16})&&signature.block(9,gcm.bytes()));
    assert(envelope.block(6,to.bytes())&&envelope.block(7,from.bytes())&&envelope.block(10,{cipher.data(),payload.size})&&envelope.block(13,signature.bytes())&&envelope.block(50,{s.uuid.data(),16})&&envelope.integer(52,1));
    Frame out;auto b=envelope.bytes();out.length=b.size;std::memcpy(out.data.data(),b.data,b.size);return out;
}
static void start(Harness &h,std::vector<VehicleResult> &out,bool info,uint64_t context) {
    assert(info?h.vehicle.charge_state_poll(WakePolicy::NoWakeSkip,context,h.callback(out),{10000,500,500}):h.vehicle.status(context,h.callback(out),{10000,500,500}));
    h.vehicle.tick();if(h.last().hello) h.establish();
    if(info) {
        h.inject(reply(h,h.last(),{awake,sizeof(awake)},static_cast<uint32_t>(context)));
        h.vehicle.tick();if(h.last().hello) h.establish();
    }
    assert(!h.last().hello&&h.last().domain==(info?Domain::Infotainment:Domain::Vcsec));
}
static int counter_scope(bool info) {
    Harness h;assert(h.vehicle.open(h.link));std::vector<VehicleResult> out;
    const Bytes payload=info?Bytes{soc,sizeof(soc)}:Bytes{awake,sizeof(awake)};
    start(h,out,info,1);auto old=h.last();auto first=reply(h,old,payload,1);h.inject(first);assert(out.size()==1);
    start(h,out,info,2);auto fresh=h.last();assert(fresh.uuid!=old.uuid&&fresh.identifier!=old.identifier);
    h.inject(first);assert(out.size()==1); // Old request's packet is not admissible.
    h.inject(reply(h,fresh,payload,1));const bool accepted=out.size()==2;
    std::printf("%s new UUID/hash/nonce response counter=1: completions=%zu expected=2 queued=%zu\n",info?"INFO":"VCSEC",out.size(),h.vehicle.queued());
    if(!accepted) {
        h.inject(reply(h,fresh,payload,2));assert(out.size()==2&&h.vehicle.queued()==0);
        assert(out.back().authenticated&&out.back().code==(info?VehicleResultCode::Soc:VehicleResultCode::Status));
        std::puts("Positive control: identical request with response counter=2 succeeds; stale old response was rejected");
    }
    return accepted?0:1;
}
static Frame synchronization_error(Harness &h,const Sent &s,bool bad=false) {
    Frame frame=h.hello(s,false,100,2000,bad);
    ProtoWriter<8> status;ProtoWriter<16> field;
    assert(status.integer(1,2)&&status.integer(2,6)&&field.block(12,status.bytes()));
    auto b=field.bytes();assert(frame.length+b.size<frame.data.size());std::memcpy(frame.data.data()+frame.length,b.data,b.size);frame.length+=b.size;
    SessionWireReply decoded;assert(decode_session_reply({frame.data.data(),frame.length},{h.address.data(),16},decoded));
    assert(decoded.uuid==s.uuid&&decoded.domain==s.domain);
    SessionInfo data;assert(decode_session_info({decoded.encoded.data(),decoded.length},data)&&data.counter==100&&data.clock==2000);
    const auto tag=session_tag({decoded.encoded.data(),decoded.length},{s.uuid.data(),16});assert((tag==decoded.tag)==!bad);
    return frame;
}
static int session_refresh(bool info) {
    Harness h;assert(h.vehicle.open(h.link));std::vector<VehicleResult> out;start(h,out,info,1);
    const auto original=h.last();const auto before=h.vehicle.high_water(original.domain);
    h.inject(synchronization_error(h,original,true));assert(h.vehicle.high_water(original.domain)==before&&out.empty());
    auto wrong=original;wrong.uuid[0]^=0x80;h.inject(synchronization_error(h,wrong));assert(h.vehicle.high_water(original.domain)==before&&out.empty());
    unsigned updates=0;bool synchronized=false;
    for(unsigned n=0;n<10&&h.vehicle.queued();++n) {
        auto request=h.last();assert(!request.hello);
        if(request.counter>100) {
            synchronized=true;h.inject(reply(h,request,info?Bytes{soc,sizeof(soc)}:Bytes{awake,sizeof(awake)},1));break;
        }
        h.inject(synchronization_error(h,request));++updates;
        h.clock+=500;h.vehicle.tick();h.clock+=100;h.vehicle.tick();
    }
    std::printf("%s signed SessionInfo error fault=6, counter=100 clock=2000: updates=%u high_water=%u expected>=100 last_counter=%u expected>100 synchronized=%d completed=%zu failed=%d\n",info?"INFO":"VCSEC",updates,h.vehicle.high_water(original.domain),h.last().counter,synchronized,out.size(),!out.empty()&&out.back().code==VehicleResultCode::Failed);
    std::puts("Controls: valid SessionInfo decoder/HMAC; bad-tag and foreign-UUID updates rejected; full UUID, correct domain and fresh command age used");
    return synchronized?0:1;
}
int main(int argc,char **argv) {
    assert(argc==2);std::string mode=argv[1];
    if(mode=="replay-vcsec")return counter_scope(false);
    if(mode=="replay-info")return counter_scope(true);
    if(mode=="update-vcsec")return session_refresh(false);
    if(mode=="update-info")return session_refresh(true);
    return 2;
}
