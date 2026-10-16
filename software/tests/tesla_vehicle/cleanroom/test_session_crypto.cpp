// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/session_crypto.hpp>
#include "vectors.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tesla_clean;
static int fixed_nonce(void *, unsigned char *out, size_t size) {
    if (size!=sizeof(vectors::nonce)) { return -1; }
    std::memcpy(out,vectors::nonce,size); return 0;
}
static int refuse(void *, unsigned char *, size_t) { return -1; }
int main()
{
    SessionCrypto crypto;
    std::array<uint8_t,32> tag{};
    assert(!crypto.session_info_tag({vectors::vin,sizeof(vectors::vin)}, {vectors::challenge,16}, {vectors::info,sizeof(vectors::info)}, tag));
    assert(crypto.set_key({vectors::key,16}));
    assert(crypto.session_info_tag({vectors::vin,sizeof(vectors::vin)}, {vectors::challenge,16}, {vectors::info,sizeof(vectors::info)}, tag));
    assert(std::memcmp(tag.data(),vectors::session_hmac,32)==0);
    GcmPacket encrypted;
    assert(crypto.encrypt({vectors::payload,sizeof(vectors::payload)}, {vectors::aad,32}, {fixed_nonce,nullptr}, encrypted));
    assert(encrypted.length==sizeof(vectors::payload));
    assert(std::memcmp(encrypted.data.data(),vectors::ciphertext,encrypted.length)==0);
    assert(std::memcmp(encrypted.tag.data(),vectors::tag,16)==0);
    assert(std::memcmp(encrypted.nonce.data(),vectors::nonce,12)==0);
    std::array<uint8_t,1024> output{}; size_t length=0;
    assert(crypto.decrypt(encrypted,{vectors::aad,32},output,length));
    assert(length==sizeof(vectors::payload) && std::memcmp(output.data(),vectors::payload,length)==0);
    for (int tamper=0; tamper<4; ++tamper) {
        auto bad=encrypted; std::array<uint8_t,32> aad{}; std::memcpy(aad.data(),vectors::aad,32);
        if (tamper==0) { bad.tag[0]^=1; }
        if (tamper==1) { bad.data[0]^=1; }
        if (tamper==2) { bad.nonce[0]^=1; }
        if (tamper==3) { aad[0]^=1; }
        output.fill(255); length=42;
        assert(!crypto.decrypt(bad,{aad.data(),32},output,length));
        assert(length==0); for (auto byte:output) { assert(byte==0); }
    }
    assert(!crypto.set_key({vectors::key,15})); // refusal retains valid key
    assert(crypto.decrypt(encrypted,{vectors::aad,32},output,length));
    assert(!crypto.encrypt({vectors::payload,sizeof(vectors::payload)},{vectors::aad,32},{refuse,nullptr},encrypted));
    assert(encrypted.length==0); for (auto byte:encrypted.data) { assert(byte==0); }
    std::array<uint8_t,1025> huge{};
    assert(!crypto.encrypt({huge.data(),huge.size()},{vectors::aad,32},{fixed_nonce,nullptr},encrypted));
    assert(!crypto.encrypt({nullptr,1},{vectors::aad,32},{fixed_nonce,nullptr},encrypted));
    assert(!crypto.encrypt({vectors::payload,1},{nullptr,1},{fixed_nonce,nullptr},encrypted));
    // In-place/overlapping calls are outside the bounded out-of-place API, not silent encryption of erased input.
    encrypted.data[0]=7;
    assert(!crypto.encrypt({encrypted.data.data(),1},{vectors::aad,32},{fixed_nonce,nullptr},encrypted));
    assert(crypto.encrypt({vectors::payload,sizeof(vectors::payload)}, {vectors::aad,32}, {fixed_nonce,nullptr}, encrypted));
    assert(!crypto.decrypt(encrypted,{vectors::aad,32},encrypted.data,length));
    std::puts("Clean session crypto: independent labeled HMAC/AES-GCM KATs, corrupt tag/ciphertext/nonce/AAD, RNG/capacity/null refusal and plaintext erasure passed");
}
