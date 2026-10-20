// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/response.hpp>
#include "vectors.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tesla_clean;
int main()
{
    SessionCrypto crypto; assert(crypto.set_key({vectors::key,16}));
    for (Domain domain : {Domain::Vcsec,Domain::Infotainment}) {
        ResponseGuard guard(crypto,domain,{vectors::vin,17});
        Request request;
        std::memcpy(request.uuid.data(),vectors::challenge,16);
        std::memcpy(request.identifier.data(),vectors::request_hash,17);
        request.identifier_length=17; request.context=777;
        request.generation=0x100000000ULL; request.started=UINT32_MAX-50; request.transmitted=10;
        request.total_ms=2000; request.response_ms=1000;
        Response response; response.domain=domain; response.uuid=request.uuid;
        response.counter=1; response.flags=1;
        response.packet.length=sizeof(vectors::payload);
        std::memcpy(response.packet.nonce.data(),vectors::nonce,12);
        std::memcpy(response.packet.data.data(),domain==Domain::Vcsec?vectors::vcsec_ciphertext:vectors::ciphertext,response.packet.length);
        std::memcpy(response.packet.tag.data(),domain==Domain::Vcsec?vectors::vcsec_tag:vectors::tag,16);
        std::array<uint8_t,32> aad{};
        assert(response_aad(domain,{vectors::vin,17},{request.identifier.data(),17},1,1,0,aad));
        assert(std::memcmp(aad.data(),domain==Domain::Vcsec?vectors::vcsec_aad:vectors::aad,32)==0);
        std::array<uint8_t,1024> plaintext{}; VerifiedReply reply;
        assert(!guard.accept(response,100,request.generation,plaintext,reply)); // no active request
        assert(guard.arm(request));
        for (unsigned tamper=0; tamper<9; ++tamper) {
            auto bad=response; uint64_t generation=request.generation; uint32_t now=100;
            if (tamper==0) { bad.uuid[0]^=1; }
            if (tamper==1) { bad.domain=domain==Domain::Vcsec?Domain::Infotainment:Domain::Vcsec; }
            if (tamper==2) { bad.counter=2; }
            if (tamper==3) { bad.flags=0; }
            if (tamper==4) { bad.fault=3; }
            if (tamper==5) { bad.packet.tag[0]^=1; }
            if (tamper==6) { bad.packet.data[0]^=1; }
            if (tamper==7) { generation=0; } // same low word must not correlate
            if (tamper==8) { now=1010; } // exact transmission deadline
            plaintext.fill(255); reply.context=123;
            assert(!guard.accept(bad,now,generation,plaintext,reply));
            assert(reply.context==0 && reply.length==0); for (auto byte:plaintext) { assert(byte==0); }
        }
        assert(guard.accept(response,100,request.generation,plaintext,reply));
        assert(reply.context==777 && reply.counter==1 && reply.fault==0 && reply.length==sizeof(vectors::payload));
        assert(std::memcmp(plaintext.data(),vectors::payload,reply.length)==0);
        assert(!guard.accept(response,100,request.generation,plaintext,reply)); // once-only completion
        assert(guard.arm(request));
        assert(!guard.accept(response,100,request.generation,plaintext,reply)); // replay even with eligible request
        guard.cancel();
        request.context=0; assert(!guard.arm(request)); request.context=888;
        request.identifier_length=34; assert(!guard.arm(request)); request.identifier_length=17;
        assert(guard.arm(request));
        assert(!guard.arm(request)); // cannot overwrite accepted outstanding work
        guard.cancel();
    }
    std::puts("Clean response guard: independent VCSEC/Infotainment GCM fixtures, received-counter AAD, UUID/domain/hash/flags/fault/generation/deadline negatives, once-only admission and eligible-command replay passed");
}
