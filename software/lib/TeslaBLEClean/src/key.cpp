// SPDX-License-Identifier: Apache-2.0
// Protocol source: official native.go; cryptographic operations delegated to mbedTLS.
#include <tesla_clean/key.hpp>
#include <mbedtls/ecdh.h>
#include <mbedtls/sha1.h>
#include <mbedtls/platform_util.h>
#include <cstring>
#include <utility>
namespace tesla_clean {
namespace {
bool checked_key(const mbedtls_pk_context &pk)
{
    const auto *ec = mbedtls_pk_ec(pk);
    return ec != nullptr && ec->MBEDTLS_PRIVATE(grp).id == MBEDTLS_ECP_DP_SECP256R1 &&
           mbedtls_ecp_check_privkey(&ec->MBEDTLS_PRIVATE(grp), &ec->MBEDTLS_PRIVATE(d)) == 0 &&
           mbedtls_ecp_check_pubkey(&ec->MBEDTLS_PRIVATE(grp), &ec->MBEDTLS_PRIVATE(Q)) == 0;
}
}
bool P256Key::generate(Random random)
{
    if (random.fill == nullptr) { return false; }
    P256Key candidate;
    if (mbedtls_pk_setup(&candidate.pk_, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0 ||
        mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(candidate.pk_), random.fill, random.context) != 0 ||
        !checked_key(candidate.pk_)) { return false; }
    // Transfer ownership of complete PK handles; neither underlying EC context is copied.
    std::swap(pk_, candidate.pk_);
    return true;
}
bool P256Key::import_scalar(Bytes scalar, Random random)
{
    if (!scalar.valid() || scalar.size != 32 || random.fill == nullptr) { return false; }
    P256Key candidate;
    if (mbedtls_pk_setup(&candidate.pk_, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0) { return false; }
    auto *ec = mbedtls_pk_ec(candidate.pk_);
    auto &group = ec->MBEDTLS_PRIVATE(grp);
    auto &secret = ec->MBEDTLS_PRIVATE(d);
    if (mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) != 0 ||
        mbedtls_mpi_read_binary(&secret, scalar.data, scalar.size) != 0 ||
        mbedtls_ecp_check_privkey(&group, &secret) != 0 ||
        mbedtls_ecp_mul(&group, &ec->MBEDTLS_PRIVATE(Q), &secret, &group.G, random.fill, random.context) != 0 ||
        !checked_key(candidate.pk_)) { return false; }
    std::swap(pk_, candidate.pk_);
    return true;
}
bool P256Key::import_pem(Bytes pem, Random random)
{
    if (!pem.valid() || pem.size == 0 || pem.size >= 512 || random.fill == nullptr) { return false; }
    // Only a single non-encrypted PEM key, not arbitrary DER or trailing embedded NUL data.
    std::array<uint8_t,512> terminated{};
    if (std::memchr(pem.data, 0, pem.size) != nullptr) { return false; }
    std::memcpy(terminated.data(), pem.data, pem.size);
    P256Key candidate;
    const bool valid = mbedtls_pk_parse_key(&candidate.pk_, terminated.data(), pem.size + 1,
                                          nullptr, 0, random.fill, random.context) == 0 && checked_key(candidate.pk_);
    mbedtls_platform_zeroize(terminated.data(), terminated.size());
    if (!valid) { return false; }
    std::swap(pk_, candidate.pk_);
    return true;
}
bool P256Key::public_point(std::array<uint8_t,65> &output) const
{
    output.fill(0);
    const auto *ec = mbedtls_pk_ec(pk_);
    if (ec == nullptr || ec->MBEDTLS_PRIVATE(grp).id != MBEDTLS_ECP_DP_SECP256R1) { return false; }
    size_t size = 0;
    const bool ok = mbedtls_ecp_point_write_binary(&ec->MBEDTLS_PRIVATE(grp), &ec->MBEDTLS_PRIVATE(Q),
                        MBEDTLS_ECP_PF_UNCOMPRESSED, &size, output.data(), output.size()) == 0 && size == output.size();
    if (!ok) { output.fill(0); }
    return ok;
}
bool P256Key::export_pem(uint8_t *output, size_t capacity, size_t &length) const
{
    length = 0;
    if (output == nullptr || capacity == 0) { return false; }
    std::memset(output, 0, capacity);
    if (!checked_key(pk_) || mbedtls_pk_write_key_pem(&pk_, output, capacity) != 0) {
        mbedtls_platform_zeroize(output, capacity);
        return false;
    }
    length = std::strlen(reinterpret_cast<const char *>(output));
    return true;
}
bool P256Key::exchange(Bytes remote, Random random, std::array<uint8_t,16> &output)
{
    output.fill(0);
    auto *ec = mbedtls_pk_ec(pk_);
    if (!checked_key(pk_) || !remote.valid() || remote.size != 65 || remote.data[0] != 4 || random.fill == nullptr) { return false; }
    mbedtls_ecp_point point; mbedtls_ecp_point_init(&point);
    mbedtls_mpi x; mbedtls_mpi_init(&x);
    uint8_t shared_x[32]{}, digest[20]{};
    const bool ok = mbedtls_ecp_point_read_binary(&ec->MBEDTLS_PRIVATE(grp), &point, remote.data, remote.size) == 0 &&
                   mbedtls_ecp_check_pubkey(&ec->MBEDTLS_PRIVATE(grp), &point) == 0 &&
                   mbedtls_ecdh_compute_shared(&ec->MBEDTLS_PRIVATE(grp), &x, &point, &ec->MBEDTLS_PRIVATE(d),
                                               random.fill, random.context) == 0 &&
                   mbedtls_mpi_write_binary(&x, shared_x, sizeof(shared_x)) == 0 &&
                   mbedtls_sha1(shared_x, sizeof(shared_x), digest) == 0;
    if (ok) { std::memcpy(output.data(), digest, output.size()); }
    mbedtls_platform_zeroize(shared_x, sizeof(shared_x)); mbedtls_platform_zeroize(digest, sizeof(digest));
    mbedtls_mpi_free(&x); mbedtls_ecp_point_free(&point);
    return ok;
}
}
