#ifndef WARP_REMOTE_CRYPTO_H
#define WARP_REMOTE_CRYPTO_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Standard P-256 big-endian scalar / X||Y, HKDF-SHA256, AES-256-CCM.
 * Failed authentication/point validation zeroes output. No transport trust,
 * consent, persistence or nonce allocation is implied by these primitives. */
bool wr_crypto_public(uint8_t out[64],const uint8_t secret[32]);
bool wr_crypto_shared(uint8_t out[32],const uint8_t secret[32],const uint8_t peer[64]);
bool wr_crypto_hkdf(uint8_t *out,size_t size,const uint8_t *key,size_t key_size,const uint8_t *salt,size_t salt_size,const uint8_t *info,size_t info_size);
bool wr_crypto_seal(uint8_t *cipher,uint8_t tag[16],const uint8_t key[32],const uint8_t nonce[13],const uint8_t *aad,size_t aad_size,const uint8_t *plain,size_t size);
bool wr_crypto_open(uint8_t *plain,const uint8_t key[32],const uint8_t nonce[13],const uint8_t *aad,size_t aad_size,const uint8_t *cipher,size_t size,const uint8_t tag[16]);
#ifdef __cplusplus
}
#endif
#endif
