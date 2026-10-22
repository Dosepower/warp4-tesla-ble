// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/key.hpp>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/sha1.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tesla_clean;
static int refuse(void *, unsigned char *, size_t) { return -1; }
int main()
{
    mbedtls_entropy_context entropy; mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&drbg);
    assert(mbedtls_ctr_drbg_seed(&drbg,mbedtls_entropy_func,&entropy,nullptr,0)==0);
    Random random{mbedtls_ctr_drbg_random,&drbg};
    P256Key key;
    std::array<uint8_t,65> public_key{};
    assert(!key.public_point(public_key));
    // Official ecdh_test.go:46-73: leading-zero shared x-coordinate.
    const uint8_t scalar[]{0x52,0x60,0xf8,0xd6,0x11,0x38,0x75,0xd8,0x6f,0x8e,0xe8,0xfe,0xa3,0x40,0xdf,0x1f,0xfb,0x40,0xc6,0x58,0xb5,0x45,0x5e,0x8c,0x33,0xd7,0x97,0xc5,0x3a,0x41,0xaf,0xd3};
    const uint8_t remote[]{0x04,0x07,0xfb,0x60,0xb6,0x5b,0x94,0xe0,0xde,0x4a,0x95,0x4c,0x53,0xbe,0x10,0x00,0x3d,0x9e,0x69,0x91,0x8d,0xed,0xfd,0xa5,0xf4,0xe9,0xef,0xb9,0xeb,0xd8,0xc5,0xbd,0x67,0x2a,0x53,0x99,0x1c,0x40,0x68,0x86,0x5d,0x5f,0xb4,0x4f,0x97,0xf6,0xce,0xcf,0x83,0x98,0xf2,0x61,0xdd,0x1d,0x7b,0xc6,0x9b,0xe6,0x76,0xaf,0xdc,0x8f,0xfa,0xcb,0xcc};
    const uint8_t shared_x[]{0x00,0x72,0xd5,0xb8,0x15,0x20,0x7a,0x04,0xf0,0xc7,0x95,0xfb,0xa0,0xba,0x9e,0x8a,0xdd,0x3f,0x1f,0x57,0x14,0x8c,0x51,0xff,0xac,0xe2,0x2c,0xa1,0x5e,0x6f,0xd8,0x45};
    assert(key.import_scalar({scalar,32},random)); assert(key.public_point(public_key));
    std::array<uint8_t,16> shared{}; uint8_t digest[20]{};
    assert(mbedtls_sha1(shared_x,32,digest)==0);
    assert(key.exchange({remote,65},random,shared)); assert(std::memcmp(shared.data(),digest,16)==0);
    const auto original=public_key;
    std::array<uint8_t,32> zero{}, out_of_range{}; out_of_range.fill(255);
    assert(!key.import_scalar({zero.data(),32},random));
    assert(!key.import_scalar({out_of_range.data(),32},random));
    assert(!key.import_scalar({scalar,31},random));
    assert(!key.generate({refuse,nullptr})); assert(!key.generate({}));
    assert(key.public_point(public_key) && public_key==original);
    std::array<uint8_t,65> invalid{}; invalid[0]=4;
    shared.fill(255); assert(!key.exchange({invalid.data(),65},random,shared));
    assert((shared==std::array<uint8_t,16>{}));
    assert(!key.exchange({remote,64},random,shared));
    assert(!key.exchange({remote,65},{refuse,nullptr},shared));
    std::array<uint8_t,512> pem{}; size_t length=99;
    assert(key.export_pem(pem.data(),pem.size(),length) && length>0 && pem[length]==0);
    P256Key imported;
    assert(imported.import_pem({pem.data(),length},random));
    assert(imported.public_point(public_key) && public_key==original);
    length=99; assert(!key.export_pem(pem.data(),8,length) && length==0);
    assert(!key.export_pem(nullptr,512,length) && length==0);
    const uint8_t bad[]{'n','o','t',' ','P','E','M'};
    assert(!key.import_pem({bad,sizeof(bad)},random));
    assert(key.public_point(public_key) && public_key==original);
    assert(key.generate(random)); assert(key.public_point(public_key) && public_key!=original);
    std::puts("Clean P256 key: official padded ECDH vector, PEM roundtrip, generation, RNG/scalar/point/capacity refusal and prior identity retention passed");
    mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&entropy);
}
