#include <cstddef>
#include <cstdlib>
#include <mbedtls/pk.h>
extern "C" void mbedtls_platform_zeroize(void *p, size_t n) { volatile unsigned char *b = static_cast<volatile unsigned char *>(p); while (n--) *b++ = 0; }
extern "C" void mbedtls_pk_init(mbedtls_pk_context *ctx) { *ctx = mbedtls_pk_context{}; }
extern "C" void mbedtls_pk_free(mbedtls_pk_context *) {}
#include <tesla_clean/storage.hpp>
bool tesla_clean::IdentityStore::import_pem(tesla_clean::Bytes, tesla_clean::Random) { std::abort(); }
tesla_clean::IdentityOutcome tesla_clean::IdentityStore::rotate(tesla_clean::KeyStorage &, tesla_clean::Random) { std::abort(); }
