#pragma once
// HOST TEST ONLY: simulated PSRAM heap. Proves accounting/ownership/zeroize logic, NOT ESP32 placement.
// Simulates IDF heap poisoning (B1 regression): requests >=128 B are rounded to a size class, a 4 B tail canary sits at
// data+requested, and free_ext() aborts if a wipe/overrun destroyed it (as heap_caps_free asserts on target).
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <mutex>
namespace tesla_psram { namespace plat {
struct Block { size_t n; size_t rounded; size_t pad; };
constexpr unsigned char CANARY[4] = {0xBA, 0xAD, 0xCA, 0xFE};
inline size_t rounded_size(size_t n) { return n >= 128 ? ((n + 4 + 63) & ~size_t(63)) : n; }
inline std::atomic<unsigned> canary_failures{0};
inline std::set<const void *> &ext() { static std::set<const void *> s; return s; }
inline std::mutex &mu() { static std::mutex m; return m; }
inline std::atomic<unsigned> zeroized_nonzero{0}; // bytes found non-zero right after zeroize (must stay 0)
inline void *calloc_ext(size_t n) { const size_t r = rounded_size(n); auto *b = static_cast<Block *>(std::calloc(1, sizeof(Block) + r + 4)); if (!b) return nullptr;
    b->n = n; b->rounded = r; std::memcpy(reinterpret_cast<unsigned char *>(b + 1) + n, CANARY, 4); std::lock_guard<std::mutex> g(mu()); ext().insert(b + 1); return b + 1; }
inline size_t block_size(void *p) { return (static_cast<Block *>(p) - 1)->rounded; } // like poisoned heap_caps_get_allocated_size: >= requested
inline bool is_external(const void *p) { std::lock_guard<std::mutex> g(mu()); return p && ext().count(p); }
inline void zeroize(void *p, size_t n) { volatile unsigned char *v = static_cast<unsigned char *>(p); for (size_t i = 0; i < n; ++i) v[i] = 0;
    for (size_t i = 0; i < n; ++i) if (static_cast<unsigned char *>(p)[i]) zeroized_nonzero++; }
inline void free_ext(void *p) { Block *b = static_cast<Block *>(p) - 1;
    if (std::memcmp(static_cast<unsigned char *>(p) + b->n, CANARY, 4) != 0) { canary_failures++; std::fprintf(stderr, "CORRUPT HEAP: tail canary (n=%zu)\n", b->n); std::abort(); }
    { std::lock_guard<std::mutex> g(mu()); ext().erase(p); } std::free(b); }
}}
