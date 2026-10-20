// SPDX-License-Identifier: Apache-2.0
// Protocol source: official native.go:38-88. See PROVENANCE.md.
#include <tesla_clean/session_crypto.hpp>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <cstring>
namespace tesla_clean {
namespace {
bool overlaps(Bytes input, const void *object, size_t size)
{
    if (input.size == 0 || input.data == nullptr) { return false; }
    const uintptr_t first = reinterpret_cast<uintptr_t>(input.data);
    const uintptr_t second = reinterpret_cast<uintptr_t>(object);
    return first <= second ? second - first < input.size : first - second < size;
}
struct GcmContext {
    mbedtls_gcm_context value;
    GcmContext() { mbedtls_gcm_init(&value); }
    ~GcmContext() { mbedtls_gcm_free(&value); }
};
}
SessionCrypto::~SessionCrypto() { mbedtls_platform_zeroize(key_.data(), key_.size()); }
bool SessionCrypto::set_key(Bytes key)
{
    if (!key.valid() || key.size != key_.size()) { return false; }
    std::memcpy(key_.data(), key.data, key_.size());
    ready_ = true;
    return true;
}
bool SessionCrypto::session_info_tag(Bytes vin, Bytes challenge, Bytes encoded, std::array<uint8_t,32> &output) const
{
    output.fill(0);
    Metadata transcript;
    const uint8_t method = 6;
    if (!ready_ || !transcript.add(0, {&method,1}) || !transcript.add(2, vin) ||
        !transcript.add(6, challenge) || !transcript.finish(encoded)) { return false; }
    const auto *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md == nullptr) { return false; }
    std::array<uint8_t,32> subkey{};
    const uint8_t label[]{'s','e','s','s','i','o','n',' ','i','n','f','o'};
    const auto bytes = transcript.bytes();
    const bool ok = mbedtls_md_hmac(md, key_.data(), key_.size(), label, sizeof(label), subkey.data()) == 0 &&
                   mbedtls_md_hmac(md, subkey.data(), subkey.size(), bytes.data, bytes.size, output.data()) == 0;
    mbedtls_platform_zeroize(subkey.data(), subkey.size());
    if (!ok) { output.fill(0); }
    return ok;
}
bool SessionCrypto::encrypt(Bytes plaintext, Bytes aad, Random random, GcmPacket &output) const
{
    const bool alias = overlaps(plaintext, &output, sizeof(output)) || overlaps(aad, &output, sizeof(output));
    output = {};
    if (alias || !ready_ || !plaintext.valid() || !aad.valid() || plaintext.size > output.data.size() || random.fill == nullptr) { return false; }
    GcmContext gcm;
    const bool ok = random.fill(random.context, output.nonce.data(), output.nonce.size()) == 0 &&
                   mbedtls_gcm_setkey(&gcm.value, MBEDTLS_CIPHER_ID_AES, key_.data(), 128) == 0 &&
                   mbedtls_gcm_crypt_and_tag(&gcm.value, MBEDTLS_GCM_ENCRYPT, plaintext.size,
                       output.nonce.data(), output.nonce.size(), aad.data, aad.size, plaintext.data,
                       output.data.data(), output.tag.size(), output.tag.data()) == 0;
    if (!ok) { output = {}; return false; }
    output.length = plaintext.size;
    return true;
}
bool SessionCrypto::decrypt(const GcmPacket &packet, Bytes aad, std::array<uint8_t,1024> &output, size_t &length) const
{
    const bool alias = overlaps({output.data(),output.size()}, &packet, sizeof(packet)) ||
                       overlaps(aad, output.data(), output.size());
    output.fill(0); length = 0;
    if (alias || !ready_ || !aad.valid() || packet.length > output.size()) { return false; }
    GcmContext gcm;
    const bool ok = mbedtls_gcm_setkey(&gcm.value, MBEDTLS_CIPHER_ID_AES, key_.data(), 128) == 0 &&
                   mbedtls_gcm_auth_decrypt(&gcm.value, packet.length, packet.nonce.data(), packet.nonce.size(),
                        aad.data, aad.size, packet.tag.data(), packet.tag.size(), packet.data.data(), output.data()) == 0;
    if (!ok) { output.fill(0); return false; }
    length = packet.length;
    return true;
}
}
