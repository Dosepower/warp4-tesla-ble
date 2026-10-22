// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/key.hpp>
#include <tesla_clean/session_crypto.hpp>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include "vectors.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
using namespace tesla_clean;
static bool watching=false;
static size_t attempts=0, fail_at=0, hits=0, throwing_new=0;
static std::array<void *,8192> live{};
extern "C" void *__real_calloc(size_t,size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_calloc(size_t n,size_t s)
{
    if (watching && ++attempts==fail_at) { ++hits; return nullptr; }
    void *p=__real_calloc(n,s);
    if (p) { bool added=false; for (auto &slot:live) { if (!slot) { slot=p; added=true; break; } } assert(added); }
    return p;
}
extern "C" void __wrap_free(void *p)
{
    if (p) { for (auto &slot:live) { if (slot==p) { slot=nullptr; break; } } }
    __real_free(p);
}
void *operator new(size_t n) { if (watching) { ++throwing_new; } if (void *p=std::malloc(n)) { return p; } std::abort(); }
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p,size_t) noexcept { std::free(p); }
void operator delete[](void *p,size_t) noexcept { std::free(p); }
static int nonce(void *,unsigned char *p,size_t n) { if (n!=12) { return -1; } std::memcpy(p,vectors::nonce,12); return 0; }
struct Rng {
    mbedtls_entropy_context entropy; mbedtls_ctr_drbg_context drbg;
    // Reproducible PUBLIC host fixture only; never a production entropy source.
    static int test_entropy(void *,unsigned char *p,size_t n) { for (size_t i=0; i<n; ++i) { p[i]=static_cast<unsigned char>(i*17+31); } return 0; }
    Rng() { mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&drbg); assert(mbedtls_ctr_drbg_seed(&drbg,test_entropy,nullptr,nullptr,0)==0); }
    ~Rng() { mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&entropy); }
    Random get() { return {mbedtls_ctr_drbg_random,&drbg}; }
};
static bool exercise(unsigned operation,size_t fault)
{
    Rng rng; P256Key key; std::array<uint8_t,32> scalar{}; scalar[31]=1;
    assert(key.import_scalar({scalar.data(),32},rng.get()));
    std::array<uint8_t,65> before{},after{}; assert(key.public_point(before));
    std::array<uint8_t,512> pem{}; size_t pem_length=0; assert(key.export_pem(pem.data(),pem.size(),pem_length));
    SessionCrypto crypto; assert(crypto.set_key({vectors::key,16}));
    GcmPacket packet; packet.length=sizeof(vectors::ciphertext);
    std::memcpy(packet.data.data(),vectors::ciphertext,packet.length); std::memcpy(packet.nonce.data(),vectors::nonce,12); std::memcpy(packet.tag.data(),vectors::tag,16);
    std::array<uint8_t,1024> plaintext{}; size_t length=0; std::array<uint8_t,16> shared{}; std::array<uint8_t,32> hmac{};
    scalar[31]=2;
    attempts=hits=throwing_new=0; fail_at=fault; watching=true;
    bool ok=false;
    switch (operation) {
    case 0: ok=key.import_scalar({scalar.data(),32},rng.get()); break;
    case 1: ok=key.generate(rng.get()); break;
    case 2: ok=key.import_pem({pem.data(),pem_length},rng.get()); break;
    case 3: ok=key.exchange({vectors::encoded_info+4,65},rng.get(),shared); break;
    case 4: ok=crypto.session_info_tag({vectors::vin,17},{vectors::challenge,16},{vectors::info,sizeof(vectors::info)},hmac); break;
    case 5: ok=crypto.encrypt({vectors::payload,sizeof(vectors::payload)},{vectors::aad,32},{nonce,nullptr},packet); break;
    case 6: ok=crypto.decrypt(packet,{vectors::aad,32},plaintext,length); break;
    default: std::abort();
    }
    watching=false;
    assert(throwing_new==0);
    if (fault!=0) {
        if (hits!=1 || ok) { std::fprintf(stderr,"fault diagnostic: operation=%u fault=%zu attempts=%zu hits=%zu ok=%d\n",operation,fault,attempts,hits,ok); }
        assert(hits==1 && !ok); assert(key.public_point(after) && after==before);
        if (operation==3) { for (auto byte:shared) { assert(byte==0); } }
        if (operation==4) { for (auto byte:hmac) { assert(byte==0); } }
        if (operation==5) { assert(packet.length==0); }
        if (operation==6) { assert(length==0); for (auto byte:plaintext) { assert(byte==0); } }
        // Retry on the SAME retained key/crypto, not merely a fresh fixture.
        switch (operation) {
        case 0: assert(key.import_scalar({scalar.data(),32},rng.get())); break;
        case 1: assert(key.generate(rng.get())); break;
        case 2: assert(key.import_pem({pem.data(),pem_length},rng.get())); break;
        case 3: assert(key.exchange({vectors::encoded_info+4,65},rng.get(),shared)); break;
        case 4: assert(crypto.session_info_tag({vectors::vin,17},{vectors::challenge,16},{vectors::info,sizeof(vectors::info)},hmac)); break;
        case 5: assert(crypto.encrypt({vectors::payload,sizeof(vectors::payload)},{vectors::aad,32},{nonce,nullptr},packet)); break;
        case 6: assert(crypto.decrypt(packet,{vectors::aad,32},plaintext,length)); break;
        }
    } else { assert(ok); }
    return ok;
}
int main()
{
    size_t total=0;
    for (unsigned operation=0; operation<7; ++operation) {
        exercise(operation,0); const size_t boundaries=attempts;
        for (size_t fault=1; fault<=boundaries; ++fault) {
            exercise(operation,fault); for (auto pointer:live) { assert(pointer==nullptr); }
        }
        total+=boundaries;
        std::printf("Clean allocation operation %u: %zu calloc boundaries, retained identity/output refusal and same-object retry\n",operation,boundaries);
    }
    std::printf("Clean allocation fault matrix: %zu injected failures, zero feature-owned throwing new, no tracked crypto resource leaks (-fno-exceptions)\n",total);
}
