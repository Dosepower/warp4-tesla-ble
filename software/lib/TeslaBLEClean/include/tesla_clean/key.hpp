// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/metadata.hpp>
#include <mbedtls/pk.h>
namespace tesla_clean {
struct Random {
    int (*fill)(void *, unsigned char *, size_t) = nullptr;
    void *context = nullptr;
};
class P256Key {
public:
    P256Key() { mbedtls_pk_init(&pk_); }
    ~P256Key() { mbedtls_pk_free(&pk_); }
    P256Key(const P256Key &) = delete;
    P256Key &operator=(const P256Key &) = delete;
    bool generate(Random random);
    bool import_scalar(Bytes scalar, Random random);
    bool import_pem(Bytes pem, Random random);
    bool public_point(std::array<uint8_t,65> &output) const;
    bool export_pem(uint8_t *output, size_t capacity, size_t &length) const;
    bool exchange(Bytes remote, Random random, std::array<uint8_t,16> &output);
private:
    mbedtls_pk_context pk_;
};
}
