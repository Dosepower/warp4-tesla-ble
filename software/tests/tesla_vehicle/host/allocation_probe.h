#pragma once
#define TESLA_BLE_ALLOCATION_PROBE
// Linked ONLY into host test executables, never firmware.
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <mbedtls/pk.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
namespace AllocationProbe {
inline bool watching = false;
inline unsigned throwing = 0, attempts = 0, fail_at = 0, failed = 0;
inline void *live[8192]{};
inline void *contexts[4][128]{};
inline unsigned initialized[4]{}, freed[4]{};
inline void add(void *p) {
    if (!p || !watching) { return; }
    for (auto &slot : live) { if (!slot) { slot = p; return; } } std::abort();
}
inline void remove(void *p) { for (auto &slot : live) { if (slot == p && p) { slot = nullptr; return; } } }
inline bool fail() { if (!watching) { return false; } ++attempts; if (attempts == fail_at) { ++failed; return true; } return false; }
inline void context_init(unsigned kind, void *p) {
    if (!watching) { return; }
    for (auto &slot : contexts[kind]) { assert(slot != p); }
    for (auto &slot : contexts[kind]) { if (!slot) { slot = p; ++initialized[kind]; return; } } std::abort();
}
inline void context_free(unsigned kind, void *p) {
    for (auto &slot : contexts[kind]) { if (slot == p) { slot = nullptr; ++freed[kind]; return; } }
}
inline void clean() {
    for (auto p : live) { assert(p == nullptr); }
    for (unsigned k = 0; k < 4; ++k) {
        for (auto p : contexts[k]) { assert(p == nullptr); }
        assert(initialized[k] == freed[k]);
    }
}
inline void begin(unsigned failure = 0) {
    clean(); throwing = attempts = failed = 0; fail_at = failure;
    for (unsigned k = 0; k < 4; ++k) { initialized[k] = freed[k] = 0; }
    watching = true;
}
inline void end() { watching = false; assert(throwing == 0); }
struct PlatformBoundary {
    bool old = watching;
    PlatformBoundary() { watching = false; }
    ~PlatformBoundary() { watching = old; }
};
}
extern "C" void *__real_calloc(size_t, size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_calloc(size_t n, size_t size) {
    if (AllocationProbe::fail()) { return nullptr; }
    void *p = __real_calloc(n, size); AllocationProbe::add(p); return p;
}
extern "C" void __wrap_free(void *p) { AllocationProbe::remove(p); __real_free(p); }
void *operator new(std::size_t size) {
    if (AllocationProbe::watching) { ++AllocationProbe::throwing; }
    void *p = std::malloc(size); if (!p) { std::abort(); } return p;
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
    if (AllocationProbe::fail()) { return nullptr; }
    void *p = std::malloc(size); AllocationProbe::add(p); return p;
}
void operator delete(void *p) noexcept { AllocationProbe::remove(p); __real_free(p); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete(void *p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { ::operator delete(p); }
#define CONTEXT_PROBE(name, type, kind) \
extern "C" void __real_mbedtls_##name##_init(type *); \
extern "C" void __real_mbedtls_##name##_free(type *); \
extern "C" void __wrap_mbedtls_##name##_init(type *p) { __real_mbedtls_##name##_init(p); AllocationProbe::context_init(kind, p); } \
extern "C" void __wrap_mbedtls_##name##_free(type *p) { __real_mbedtls_##name##_free(p); AllocationProbe::context_free(kind, p); }
CONTEXT_PROBE(pk, mbedtls_pk_context, 0)
CONTEXT_PROBE(ecdh, mbedtls_ecdh_context, 1)
CONTEXT_PROBE(ctr_drbg, mbedtls_ctr_drbg_context, 2)
CONTEXT_PROBE(entropy, mbedtls_entropy_context, 3)
#undef CONTEXT_PROBE
// Reproducible DRBG stream keeps ECP blinding allocation order deterministic.
// This is a host-test entropy boundary, not a firmware RNG replacement.
extern "C" int __wrap_mbedtls_entropy_func(void *, unsigned char *out, size_t size) {
    for (size_t i = 0; i < size; ++i) { out[i] = static_cast<unsigned char>(i + 37); }
    return 0;
}
