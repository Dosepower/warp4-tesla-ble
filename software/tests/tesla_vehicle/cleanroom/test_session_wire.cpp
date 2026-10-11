// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/session_wire.hpp>
#include <tesla_clean/protobuf.hpp>
#include "vectors.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tesla_clean;
static Frame response(const ChallengeView &v,Bytes address,unsigned mode=0)
{
    ProtoWriter<32> to,from; ProtoWriter<40> tag; ProtoWriter<96> sig; ProtoWriter<1024> outer;
    assert(to.block(2,address)); assert(from.integer(1,static_cast<uint32_t>(v.domain)));
    assert(tag.block(1,{vectors::info_tag,32})); assert(sig.block(6,tag.bytes()));
    if(mode==1) { assert(sig.block(9,{})); }
    assert(outer.block(6,to.bytes()) && outer.block(7,from.bytes()));
    assert(outer.block(15,{vectors::encoded_info,sizeof(vectors::encoded_info)}));
    assert(outer.block(13,sig.bytes()));
    if(mode!=2) { assert(outer.block(50,{v.uuid.data(),16})); }
    if(mode==3) { assert(outer.block(10,{})); }
    if(mode==4) { assert(outer.block(15,{vectors::encoded_info,sizeof(vectors::encoded_info)})); }
    if(mode==5) { assert(outer.block(50,{v.uuid.data(),16})); }
    if(mode==6) { assert(outer.integer(100,19)); } // unknown fields do not replace authentication
    Frame f; auto b=outer.bytes(); std::memcpy(f.data.data(),b.data,b.size); f.length=b.size; return f;
}
int main()
{
    ChallengeView v; v.domain=Domain::Infotainment; v.public_point[0]=4;
    std::array<uint8_t,16> address{};
    for(unsigned i=0;i<16;++i) { address[i]=static_cast<uint8_t>(i); v.uuid[i]=static_cast<uint8_t>(16+i); }
    for(unsigned i=0;i<64;++i) { v.public_point[i+1]=static_cast<uint8_t>(i); }
    Frame request; assert(encode_session_request(v,{address.data(),16},request));
    // Independently generated Python ordinary-protobuf KAT, not encoder round-trip.
    const char *hex="320208033a121210000102030405060708090a0b0c0d0e0f72430a4104000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f9a0310101112131415161718191a1b1c1d1e1f";
    assert(request.length==std::strlen(hex)/2);
    for(size_t i=0;i<request.length;++i) { unsigned value=0; assert(std::sscanf(hex+i*2,"%2x",&value)==1); assert(request.data[i]==value); }
    for(auto domain:{Domain::Vcsec,Domain::Infotainment}) {
        v.domain=domain;
        auto f=response(v,{address.data(),16}); SessionWireReply decoded;
        assert(decode_session_reply({f.data.data(),f.length},{address.data(),16},decoded));
        assert(decoded.domain==domain && decoded.uuid==v.uuid && decoded.length==sizeof(vectors::encoded_info));
        assert(!std::memcmp(decoded.encoded.data(),vectors::encoded_info,decoded.length));
        assert(!std::memcmp(decoded.tag.data(),vectors::info_tag,32));
        auto view=decoded.view((uint64_t{1}<<32)+9); assert(view.generation==(uint64_t{1}<<32)+9);
        for(size_t n=0;n<f.length;++n) { assert(!decode_session_reply({f.data.data(),n},{address.data(),16},decoded)); assert(decoded.length==0); }
        auto wrong=address; wrong[0]^=1; assert(!decode_session_reply({f.data.data(),f.length},{wrong.data(),16},decoded));
        for(unsigned mode=1;mode<6;++mode) { auto bad=response(v,{address.data(),16},mode); assert(!decode_session_reply({bad.data.data(),bad.length},{address.data(),16},decoded)); }
        auto unknown=response(v,{address.data(),16},6); assert(decode_session_reply({unknown.data.data(),unknown.length},{address.data(),16},decoded));
    }
    v.public_point[0]=3; assert(!encode_session_request(v,{address.data(),16},request) && request.length==0);
    v.public_point[0]=4; v.domain=static_cast<Domain>(0); assert(!encode_session_request(v,{address.data(),16},request));
    SessionWireReply invalid; assert(!decode_session_reply({nullptr,2},{address.data(),16},invalid));
    std::puts("Clean SessionInfo wire: exact independent request KAT; routed both-domain bounded replies; malformed/truncated/duplicate/oneof/UUID refusal passed");
}
