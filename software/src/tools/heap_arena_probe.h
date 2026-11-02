/* HAP1 heap/arena probe core (DIAGNOSTIC ONLY, no allocation/policy change).
 *
 * Walks the native INTERNAL|8BIT heaps with the public ESP-IDF 5.5.6 API heap_caps_walk() (multi_heap_walk ->
 * tlsf_walk_pool, compiled in the normal SDK configuration, no debug flag) and records, per heap region, the free/used
 * sums, the largest free block and the block counts, plus the global top-8 free blocks with their immediately adjacent
 * (address-order) neighbour blocks. multi_heap_walk() holds the heap's internal lock while the callback runs, therefore
 * hap_walker_cb(): no allocation, no formatting, no logging, no blocking, fixed bounded writes into caller memory only.
 * Blocks/regions outside SOC_DRAM [0x3FFAE000, 0x40000000) are rejected (PSRAM/IRAM can never enter the statistics).
 *
 * Formatting/owner attribution happen later, outside the walker, from the main thread.
 * This header is self-contained (only esp_heap_caps.h types) so the production code is host-testable.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <esp_heap_caps.h>

#define HAP_CAPS         (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define HAP_TOPN         8U
#define HAP_MAX_REGIONS  12U
#define HAP_REGION_NONE  0xFFU
#define HAP_DRAM_LOW     0x3FFAE000UL
#define HAP_DRAM_HIGH    0x40000000UL
#define HAP_DIRECT_MAX   8U
#define HAP_ARENA_CHAIN_GUARD 64U

struct HapBlk {
    uint32_t addr;
    uint32_t size;
    uint8_t used;
    uint8_t valid;
};

struct HapTop {
    HapBlk blk;
    HapBlk prev;          // block immediately below blk in the same region (valid=0: region start / rejected)
    HapBlk next;          // block immediately above blk in the same region (valid=0: region end)
    uint8_t region;
    uint8_t awaiting_next;
};

struct HapRegion {
    uint32_t start;
    uint32_t end;
    uint32_t free_sum;
    uint32_t used_sum;
    uint32_t largest_free;
    uint32_t nfree;
    uint32_t nused;
};

struct HapWalk {
    HapRegion regions[HAP_MAX_REGIONS];
    HapTop top[HAP_TOPN];
    HapBlk last;
    uint32_t nregions;
    uint32_t regions_dropped;
    uint32_t ntop;
    uint32_t blocks;
    uint32_t rejected_external;
    uint32_t cur_start;
    uint32_t cur_end;
    uint32_t cur_region;   // index into regions or HAP_REGION_NONE
    uint32_t started;
};

static inline void hap_walk_reset(HapWalk *w)
{
    memset(w, 0, sizeof(*w));
    w->cur_region = HAP_REGION_NONE;
}

static inline void hap_top_insert(HapWalk *w, const HapBlk &b)
{
    uint32_t pos = w->ntop;
    while (pos > 0 && w->top[pos - 1].blk.size < b.size) {
        --pos;  // equal sizes keep address (walk) order
    }
    if (pos >= HAP_TOPN) {
        return;
    }
    const uint32_t last = w->ntop < HAP_TOPN ? w->ntop : HAP_TOPN - 1;
    for (uint32_t j = last; j > pos; --j) {
        w->top[j] = w->top[j - 1];
    }
    if (w->ntop < HAP_TOPN) {
        ++w->ntop;
    }
    HapTop &t = w->top[pos];
    t.blk = b;
    t.prev = w->last;     // last.valid==0 at region start or after a rejected block
    t.next = HapBlk{0, 0, 0, 0};
    t.region = static_cast<uint8_t>(w->cur_region);
    t.awaiting_next = 1;
}

// Runs under the multi_heap internal lock: bounded fixed writes only.
[[maybe_unused]] static bool hap_walker_cb(walker_heap_into_t heap, walker_block_info_t block, void *user_data)
{
    HapWalk *w = static_cast<HapWalk *>(user_data);
    const uint32_t addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block.ptr));
    const uint32_t size = static_cast<uint32_t>(block.size);
    const uint8_t used = block.used ? 1U : 0U;
    const uint32_t hs = static_cast<uint32_t>(heap.start);
    const uint32_t he = static_cast<uint32_t>(heap.end);

    ++w->blocks;

    if (!w->started || hs != w->cur_start || he != w->cur_end) {
        // Region boundary: a pending top entry was the last block of the previous region.
        for (uint32_t i = 0; i < w->ntop; ++i) {
            w->top[i].awaiting_next = 0;
        }
        w->last.valid = 0;
        w->started = 1;
        w->cur_start = hs;
        w->cur_end = he;
        if (w->nregions < HAP_MAX_REGIONS) {
            w->cur_region = w->nregions++;
            HapRegion &r = w->regions[w->cur_region];
            r.start = hs;
            r.end = he;
        } else {
            ++w->regions_dropped;
            w->cur_region = HAP_REGION_NONE;
        }
    }

    const uint64_t blk_end = static_cast<uint64_t>(addr) + size;
    if (addr < HAP_DRAM_LOW || blk_end > HAP_DRAM_HIGH || hs < HAP_DRAM_LOW || he > HAP_DRAM_HIGH) {
        ++w->rejected_external;
        for (uint32_t i = 0; i < w->ntop; ++i) {
            w->top[i].awaiting_next = 0;
        }
        w->last.valid = 0;
        return true;
    }

    const HapBlk b{addr, size, used, 1};

    for (uint32_t i = 0; i < w->ntop; ++i) {
        if (w->top[i].awaiting_next) {
            w->top[i].next = b;
            w->top[i].awaiting_next = 0;
        }
    }

    if (w->cur_region != HAP_REGION_NONE) {
        HapRegion &r = w->regions[w->cur_region];
        if (used) {
            r.used_sum += size;
            ++r.nused;
        } else {
            r.free_sum += size;
            ++r.nfree;
            if (size > r.largest_free) {
                r.largest_free = size;
            }
        }
        if (!used) {
            hap_top_insert(w, b);
        }
    }

    w->last = b;
    return true;
}

// ---- DRAM arena occupancy (main thread only; arena has a single owner) ----

struct HapArenaCounters {
    uint32_t armed;
    uint32_t direct_n;       // allocations > block_capacity served directly from the heap (no arena header)
    uint32_t direct_bytes;   // requested bytes incl. alignment padding passed to the heap allocator
    uint32_t direct_fail;
    uint32_t direct_addr[HAP_DIRECT_MAX];
    uint32_t direct_size[HAP_DIRECT_MAX];
};

struct HapArenaStats {
    uint32_t res_n, res_cap, res_used;      // preallocated blocks inside reserved (non-heap) ranges
    uint32_t heap_n, heap_cap, heap_used;   // arena blocks allocated from the heap (header + capacity)
    uint32_t allocs, padding, allocated_blocks;
    uint32_t chain_truncated;
};

template<typename ArenaT>
static inline void hap_arena_stats(const ArenaT &a, const uint32_t *res_lo, const uint32_t *res_hi, size_t nres, HapArenaStats *s)
{
    memset(s, 0, sizeof(*s));
    s->allocs = static_cast<uint32_t>(a.allocs);
    s->padding = static_cast<uint32_t>(a.padding_overhead);
    s->allocated_blocks = static_cast<uint32_t>(a.allocated_blocks);
    uint32_t guard = 0;
    for (auto *b = a.head; b != nullptr; b = b->next) {
        if (++guard > HAP_ARENA_CHAIN_GUARD) {
            s->chain_truncated = 1;
            break;
        }
        const uint32_t addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b));
        bool reserved = false;
        for (size_t i = 0; i < nres; ++i) {
            if (addr >= res_lo[i] && addr < res_hi[i]) {
                reserved = true;
            }
        }
        if (reserved) {
            ++s->res_n;
            s->res_cap += static_cast<uint32_t>(b->capacity);
            s->res_used += static_cast<uint32_t>(b->used);
        } else {
            ++s->heap_n;
            s->heap_cap += static_cast<uint32_t>(b->capacity);
            s->heap_used += static_cast<uint32_t>(b->used);
        }
    }
}

static inline void hap_counters_note_direct(HapArenaCounters *c, const void *p, size_t size)
{
    if (c == nullptr || !c->armed) {
        return;
    }
    if (p == nullptr) {
        ++c->direct_fail;
        return;
    }
    if (c->direct_n < HAP_DIRECT_MAX) {
        c->direct_addr[c->direct_n] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
        c->direct_size[c->direct_n] = static_cast<uint32_t>(size);
    }
    ++c->direct_n;
    c->direct_bytes += static_cast<uint32_t>(size);
}

// ---- neighbour ownership (addresses only, filled outside the walker) ----

enum HapOwnerKind : uint8_t { HAP_OWN_NONE = 0, HAP_OWN_TCB, HAP_OWN_STACK, HAP_OWN_ARENA_BLK, HAP_OWN_ARENA_DIRECT };

struct HapOwner {
    uint32_t addr;
    uint8_t kind;
    char name[16];
};

static inline const HapOwner *hap_owner_of(const HapBlk &b, const HapOwner *owners, size_t n)
{
    if (!b.valid || !b.used) {
        return nullptr;
    }
    const uint64_t end = static_cast<uint64_t>(b.addr) + b.size;
    for (size_t i = 0; i < n; ++i) {
        if (owners[i].addr >= b.addr && owners[i].addr < end) {
            return &owners[i];
        }
    }
    return nullptr;
}

static inline const char *hap_kind_name(uint8_t k)
{
    switch (k) {
        case HAP_OWN_TCB:          return "tcb";
        case HAP_OWN_STACK:        return "stk";
        case HAP_OWN_ARENA_BLK:    return "arena_blk";
        case HAP_OWN_ARENA_DIRECT: return "arena_dir";
        default:                   return "?";
    }
}

// ---- firmware hooks (implemented in heap_arena_probe.cpp, probe build only) ----
#if defined(WARP_HEAP_ARENA_PROBE)
struct Arena;
void hap_probe_pre_setup();
void hap_arena_note_direct(const Arena *a, const void *p, size_t size);
bool hap_probe_capture(const char *tag);
void hap_probe_emit();
#endif

// ---- bounded formatting (outside the walker) ----

static inline int hap_format_region(char *buf, size_t len, uint32_t idx, const HapRegion *r)
{
    return snprintf(buf, len, "HAP1 r%u 0x%08x-0x%08x free=%u largest=%u nfree=%u used=%u nused=%u",
                    static_cast<unsigned>(idx), static_cast<unsigned>(r->start), static_cast<unsigned>(r->end),
                    static_cast<unsigned>(r->free_sum), static_cast<unsigned>(r->largest_free), static_cast<unsigned>(r->nfree),
                    static_cast<unsigned>(r->used_sum), static_cast<unsigned>(r->nused));
}

static inline int hap_format_nb(char *buf, size_t len, const HapBlk &b, const HapOwner *o)
{
    if (!b.valid) {
        return snprintf(buf, len, "-");
    }
    if (!b.used) {
        return snprintf(buf, len, "0x%08x+%u:F", static_cast<unsigned>(b.addr), static_cast<unsigned>(b.size));
    }
    return snprintf(buf, len, "0x%08x+%u:U:%s%s%s", static_cast<unsigned>(b.addr), static_cast<unsigned>(b.size),
                    o != nullptr ? hap_kind_name(o->kind) : "?", (o != nullptr && o->name[0] != '\0') ? ":" : "",
                    o != nullptr ? o->name : "");
}

static inline int hap_format_top(char *buf, size_t len, uint32_t idx, const HapTop *t, const HapOwner *owners, size_t nowners)
{
    char p[56];
    char n[56];
    hap_format_nb(p, sizeof(p), t->prev, hap_owner_of(t->prev, owners, nowners));
    hap_format_nb(n, sizeof(n), t->next, hap_owner_of(t->next, owners, nowners));
    return snprintf(buf, len, "HAP1 top%u r%u 0x%08x+%u prev=%s next=%s",
                    static_cast<unsigned>(idx), static_cast<unsigned>(t->region),
                    static_cast<unsigned>(t->blk.addr), static_cast<unsigned>(t->blk.size), p, n);
}

static inline int hap_format_arena(char *buf, size_t len, const HapArenaStats *s, const HapArenaCounters *c)
{
    return snprintf(buf, len,
                    "HAP1 arena dram res_n=%u res_cap=%u res_used=%u heap_n=%u heap_cap=%u heap_used=%u allocs=%u pad=%u "
                    "blocks=%u trunc=%u direct_n=%u direct_bytes=%u direct_fail=%u armed=%u",
                    static_cast<unsigned>(s->res_n), static_cast<unsigned>(s->res_cap), static_cast<unsigned>(s->res_used),
                    static_cast<unsigned>(s->heap_n), static_cast<unsigned>(s->heap_cap), static_cast<unsigned>(s->heap_used),
                    static_cast<unsigned>(s->allocs), static_cast<unsigned>(s->padding), static_cast<unsigned>(s->allocated_blocks),
                    static_cast<unsigned>(s->chain_truncated), static_cast<unsigned>(c->direct_n), static_cast<unsigned>(c->direct_bytes),
                    static_cast<unsigned>(c->direct_fail), static_cast<unsigned>(c->armed));
}
