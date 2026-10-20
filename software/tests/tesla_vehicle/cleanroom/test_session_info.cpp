// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/session_info.hpp>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include "vectors.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tesla_clean;
int main()
{
    mbedtls_entropy_context entropy; mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&drbg);
    assert(mbedtls_ctr_drbg_seed(&drbg,mbedtls_entropy_func,&entropy,nullptr,0)==0);
    Random rng{mbedtls_ctr_drbg_random,&drbg};
    std::array<uint8_t,32> scalar{}; scalar[31]=1; P256Key local;
    assert(local.import_scalar({scalar.data(),32},rng));
    SessionInfo info; std::array<uint8_t,16> key{};
    assert(authenticate_session_info(local,rng,{vectors::vin,17},{vectors::challenge,16},
        {vectors::encoded_info,sizeof(vectors::encoded_info)},{vectors::info_tag,32},info,key)==SessionInfoResult::Established);
    assert(std::memcmp(key.data(),vectors::session_key,16)==0);
    assert(info.counter==7 && info.clock==1234 && info.status==0 && info.remote_public[0]==4 && info.epoch[15]==15);
    for (unsigned tamper=0; tamper<5; ++tamper) {
        std::array<uint8_t,sizeof(vectors::encoded_info)> encoded{}; std::memcpy(encoded.data(),vectors::encoded_info,encoded.size());
        std::array<uint8_t,32> tag{}; std::memcpy(tag.data(),vectors::info_tag,32);
        std::array<uint8_t,16> challenge{}; std::memcpy(challenge.data(),vectors::challenge,16);
        std::array<uint8_t,17> vin{}; std::memcpy(vin.data(),vectors::vin,17);
        if (tamper==0) { tag[0]^=1; }
        if (tamper==1) { encoded[1]^=1; }
        if (tamper==2) { challenge[0]^=1; }
        if (tamper==3) { vin[0]^=1; }
        if (tamper==4) { encoded[4]=0; }
        key.fill(255); info.counter=123;
        assert(authenticate_session_info(local,rng,{vin.data(),17},{challenge.data(),16},{encoded.data(),encoded.size()},
            {tag.data(),32},info,key)==SessionInfoResult::Rejected);
        assert(info.counter==0); for (auto byte:key) { assert(byte==0); }
    }
    assert(authenticate_session_info(local,rng,{vectors::vin,17},{vectors::challenge,16},
        {vectors::denied_info,sizeof(vectors::denied_info)},{vectors::denied_tag,32},info,key)==SessionInfoResult::KeyDenied);
    assert(info.status==1); for (auto byte:key) { assert(byte==0); }
    assert(authenticate_session_info(local,rng,{vectors::vin,17},{vectors::challenge,16},
        {vectors::denied_info,sizeof(vectors::denied_info)},{vectors::info_tag,32},info,key)==SessionInfoResult::Rejected);
    // No incomplete prefix authenticates against the complete signed transcript.
    for (size_t n=0; n<sizeof(vectors::encoded_info); ++n) {
        assert(authenticate_session_info(local,rng,{vectors::vin,17},{vectors::challenge,16},
            {vectors::encoded_info,n},{vectors::info_tag,32},info,key)==SessionInfoResult::Rejected);
    }
    // Proto3 default scalar values may be omitted; point and epoch are still required by this subset.
    assert(decode_session_info({vectors::encoded_info+2,sizeof(vectors::encoded_info)-7},info));
    assert(info.counter==0 && info.clock==0);
    std::array<uint8_t,512> malformed{}; std::memcpy(malformed.data(),vectors::encoded_info,sizeof(vectors::encoded_info));
    malformed[sizeof(vectors::encoded_info)]=8; malformed[sizeof(vectors::encoded_info)+1]=9;
    assert(!decode_session_info({malformed.data(),sizeof(vectors::encoded_info)+2},info));
    assert(!decode_session_info({nullptr,1},info));
    const uint8_t overflow[]{8,0xff,0xff,0xff,0xff,0x1f};
    assert(!decode_session_info({overflow,sizeof(overflow)},info));
    malformed[sizeof(vectors::encoded_info)]=0x28; malformed[sizeof(vectors::encoded_info)+1]=2;
    assert(!decode_session_info({malformed.data(),sizeof(vectors::encoded_info)+2},info));
    std::puts("Clean session-info: independently signed protobuf/ECDH/HMAC fixture, authenticated denial, tag/blob/challenge/VIN/point tamper, truncation/overflow/duplicate/unknown status and no failed publication passed");
    mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&entropy);
}
