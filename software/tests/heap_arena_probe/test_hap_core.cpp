// Host regression test for the production HAP1 core (src/tools/heap_arena_probe.h): walker callback, top-8,
// neighbours, region bounds, PSRAM/external rejection, no allocation inside the callback, arena stats, counters,
// owner attribution and bounded formatting. The esp_heap_caps.h stub carries the SDK 5.5.6 walker typedefs verbatim
// (checked against the frozen SDK header by run_hap_tests.sh).
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#include "heap_arena_probe.h"
// arena.h asserts the 32-bit ESP header size (16 B); the template is exercised here with a field-identical
// host header, and against the real FakeHeader by the firmware build.
struct FakeHeader { FakeHeader *next; size_t capacity; size_t used; uint8_t *memory; };

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static size_t new_calls = 0;
void *operator new(size_t n) { ++new_calls; void *p = malloc(n); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }

static walker_heap_into_t H(uint32_t s, uint32_t e) { walker_heap_into_t h; h.start = (intptr_t)s; h.end = (intptr_t)e; return h; }
static walker_block_info_t B(uint32_t a, uint32_t sz, bool used) { walker_block_info_t b; b.ptr = (void *)(uintptr_t)a; b.size = sz; b.used = used; return b; }

struct Seq { walker_heap_into_t h; walker_block_info_t b; };

static void feed(HapWalk *w, const Seq *s, size_t n) { for (size_t i = 0; i < n; ++i) CHECK(hap_walker_cb(s[i].h, s[i].b, w)); }

static void test_walk_topn_neighbours()
{
    const walker_heap_into_t r0 = H(0x3FFB0000, 0x3FFB4000), r1 = H(0x3FFE4350, 0x40000000), ps = H(0x3F800000, 0x3FC00000);
    // r0: U100 F200 U300 F50 F(…) ; r1: F4852 U6144 F800 ...; plus a PSRAM region (must be rejected entirely)
    Seq s[] = {
        {r0, B(0x3FFB0000, 100, true)}, {r0, B(0x3FFB0064, 200, false)}, {r0, B(0x3FFB012C, 300, true)},
        {r0, B(0x3FFB0258, 50, false)}, {r0, B(0x3FFB028A, 1000, false)},                 // 1000 = last block of r0
        {ps, B(0x3F800000, 900000, false)},                                                 // PSRAM: rejected
        {r1, B(0x3FFE4350, 4852, false)}, {r1, B(0x3FFE5644, 6144, true)}, {r1, B(0x3FFE6E44, 800, false)},
        {r1, B(0x3FFE7164, 16, true)}, {r1, B(0x3FFE7174, 700, false)}, {r1, B(0x3FFE7430, 600, false)},
        {r1, B(0x3FFE7688, 500, false)}, {r1, B(0x3FFE787C, 400, false)}, {r1, B(0x3FFE7A0C, 800, false)},
    };
    HapWalk w; hap_walk_reset(&w);
    const size_t nb = new_calls;
    struct mallinfo2 m0 = mallinfo2();
    feed(&w, s, sizeof(s) / sizeof(s[0]));
    struct mallinfo2 m1 = mallinfo2();
    CHECK(new_calls == nb);
    CHECK(m0.uordblks == m1.uordblks);
    CHECK(w.blocks == 15);
    CHECK(w.nregions == 3 && w.regions_dropped == 0);
    CHECK(w.rejected_external == 1);
    CHECK(w.regions[0].free_sum == 1250 && w.regions[0].used_sum == 400 && w.regions[0].largest_free == 1000);
    CHECK(w.regions[0].nfree == 3 && w.regions[0].nused == 2);
    CHECK(w.regions[1].start == 0x3F800000 && w.regions[1].free_sum == 0 && w.regions[1].nfree == 0);  // ps: no stats
    CHECK(w.regions[2].largest_free == 4852 && w.regions[2].free_sum == 4852 + 800 + 700 + 600 + 500 + 400 + 800);
    CHECK(w.ntop == HAP_TOPN);
    const uint32_t exp[HAP_TOPN] = {4852, 1000, 800, 800, 700, 600, 500, 400};
    for (uint32_t i = 0; i < HAP_TOPN; ++i) CHECK(w.top[i].blk.size == exp[i]);
    CHECK(w.top[2].blk.addr == 0x3FFE6E44 && w.top[3].blk.addr == 0x3FFE7A0C);       // equal sizes: walk order
    // 4852 is the first block of r1: no prev (preceded by rejected PSRAM region), next = used 6144
    CHECK(!w.top[0].prev.valid && w.top[0].next.valid && w.top[0].next.used && w.top[0].next.size == 6144);
    CHECK(w.top[0].region == 2);
    // 1000 is the last block of r0: prev = free 50, no next (region boundary)
    CHECK(w.top[1].prev.valid && !w.top[1].prev.used && w.top[1].prev.size == 50 && !w.top[1].next.valid);
    // 800@0x3FFE6E44: prev used 6144, next used 16
    CHECK(w.top[2].prev.size == 6144 && w.top[2].prev.used && w.top[2].next.size == 16 && w.top[2].next.used);
    // last block of the whole walk stays without next
    CHECK(w.top[3].prev.size == 400 && !w.top[3].next.valid);
    for (uint32_t i = 0; i < w.ntop; ++i) CHECK(w.top[i].blk.addr < 0x3F800000u || w.top[i].blk.addr >= 0x3FFAE000u);
}

static void test_region_bound_and_external_block()
{
    HapWalk w; hap_walk_reset(&w);
    for (uint32_t i = 0; i < HAP_MAX_REGIONS + 3; ++i) {
        const uint32_t s = 0x3FFB0000 + i * 0x1000;
        CHECK(hap_walker_cb(H(s, s + 0x1000), B(s, 64 + i, false), &w));
    }
    CHECK(w.nregions == HAP_MAX_REGIONS && w.regions_dropped == 3);
    CHECK(w.ntop == HAP_TOPN);
    for (uint32_t i = 0; i < w.ntop; ++i) CHECK(w.top[i].region < HAP_MAX_REGIONS);  // dropped regions never ranked
    // Consecutive valid regions must not acquire cross-region neighbours.
    // Unlike the PSRAM rejection case above, no rejected block resets last here.
    HapWalk adjacent; hap_walk_reset(&adjacent);
    CHECK(hap_walker_cb(H(0x3FFB0000, 0x3FFB1000), B(0x3FFB0000, 400, false), &adjacent));
    CHECK(hap_walker_cb(H(0x3FFC0000, 0x3FFC1000), B(0x3FFC0000, 300, false), &adjacent));
    CHECK(adjacent.ntop == 2);
    CHECK(!adjacent.top[0].next.valid);
    CHECK(!adjacent.top[1].prev.valid);
    HapWalk x; hap_walk_reset(&x);
    // block crossing SOC_DRAM_HIGH inside a nominal DRAM region, and a block below SOC_DRAM_LOW
    CHECK(hap_walker_cb(H(0x3FFF0000, 0x40000000), B(0x3FFFFF00, 0x200, false), &x));
    CHECK(hap_walker_cb(H(0x3FFA0000, 0x3FFB0000), B(0x3FFA0000, 0x100, false), &x));
    CHECK(x.rejected_external == 2 && x.ntop == 0);
}

// Cross-region neighbour gap: DRAM region -> DRAM region directly (no rejected block in between, so only the
// region-boundary reset can clear pending next / last). rA and rB are address-adjacent (rA.end == rB.start):
// physically touching blocks of two separate TLSF pools must still never be reported as neighbours.
static void test_cross_region_neighbour_boundary()
{
    const walker_heap_into_t rA = H(0x3FFB0000, 0x3FFB1000), rB = H(0x3FFB1000, 0x3FFB3000), rC = H(0x3FFC0000, 0x3FFC1000);
    Seq s[] = {
        {rA, B(0x3FFB0000, 0x100, true)}, {rA, B(0x3FFB0100, 0xF00, false)},                                  // rA last = free
        {rB, B(0x3FFB1000, 0x1800, false)}, {rB, B(0x3FFB2800, 0x400, true)}, {rB, B(0x3FFB2C00, 0x400, false)}, // rB first+last free
        {rC, B(0x3FFC0000, 0x200, true)}, {rC, B(0x3FFC0200, 0xE00, false)},                                  // rC first = used
    };
    HapWalk w; hap_walk_reset(&w);
    feed(&w, s, sizeof(s) / sizeof(s[0]));
    CHECK(w.nregions == 3 && w.rejected_external == 0 && w.ntop == 4);
    CHECK(w.top[0].blk.addr == 0x3FFB1000 && w.top[0].region == 1);
    CHECK(w.top[1].blk.addr == 0x3FFB0100 && w.top[1].region == 0);
    CHECK(w.top[2].blk.addr == 0x3FFC0200 && w.top[2].region == 2);
    CHECK(w.top[3].blk.addr == 0x3FFB2C00 && w.top[3].region == 1);
    // first block of rB: prev must NOT be rA's last (free 0xF00 @0x3FFB0100, address-adjacent), next = same-region used
    CHECK(!w.top[0].prev.valid);
    CHECK(w.top[0].next.valid && w.top[0].next.addr == 0x3FFB2800 && w.top[0].next.used);
    // last block of rA: next must NOT be rB's first block
    CHECK(w.top[1].prev.valid && w.top[1].prev.addr == 0x3FFB0000 && w.top[1].prev.used);
    CHECK(!w.top[1].next.valid);
    // last block of rB: next must NOT be rC's first (used 0x200) block
    CHECK(w.top[3].prev.valid && w.top[3].prev.addr == 0x3FFB2800 && !w.top[3].next.valid);
    // first-in-region free after a used block keeps its in-region prev; last block of the walk has no next
    CHECK(w.top[2].prev.valid && w.top[2].prev.addr == 0x3FFC0000 && w.top[2].prev.used && !w.top[2].next.valid);
    for (uint32_t i = 0; i < w.ntop; ++i) {
        const HapRegion &r = w.regions[w.top[i].region];
        if (w.top[i].prev.valid) CHECK(w.top[i].prev.addr >= r.start && w.top[i].prev.addr < r.end);
        if (w.top[i].next.valid) CHECK(w.top[i].next.addr >= r.start && w.top[i].next.addr < r.end);
    }
}

// Arena reserved-vs-heap classification at exact range edges, layout-independent (all headers inside ONE buffer, so
// the result cannot depend on how the linker orders separate statics). Ranges are half-open [lo, hi) like the
// firmware's MANUALLY_RESERVED_* tables; a header starting exactly at hi belongs to the heap.
static void test_arena_reserved_boundary()
{
    alignas(16) static uint8_t pool[4096];
    const size_t HS = sizeof(FakeHeader);
    auto at = [](size_t off) { return reinterpret_cast<FakeHeader *>(pool + off); };
    const uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pool));
    const uint32_t lo[2] = {base + 1024, base + 2048};
    const uint32_t hi[2] = {base + 1024 + 256, base + 2048 + 512};
    FakeHeader *hA = at(1024);             // == lo[0]          -> reserved
    FakeHeader *hB = at(1024 + 256 - HS);  // last slot in R0   -> reserved
    FakeHeader *hC = at(1024 + 256);       // == hi[0]          -> heap
    FakeHeader *hD = at(1024 - HS);        // just below lo[0]  -> heap
    FakeHeader *hE = at(2048 + 512);       // == hi[1]          -> heap
    FakeHeader *hF = at(2048 + 64);        // inside R1         -> reserved (second range is consulted)
    *hA = {hB, 1, 10, nullptr}; *hB = {hC, 2, 20, nullptr}; *hC = {hD, 4, 40, nullptr};
    *hD = {hE, 8, 80, nullptr}; *hE = {hF, 16, 160, nullptr}; *hF = {nullptr, 32, 320, nullptr};
    struct FakeArena { FakeHeader *head; size_t allocs, padding_overhead, allocated_blocks; } a{hA, 6, 0, 6};
    HapArenaStats s;
    hap_arena_stats(a, lo, hi, 2, &s);
    CHECK(s.res_n == 3 && s.res_cap == 1 + 2 + 32 && s.res_used == 10 + 20 + 320);
    CHECK(s.heap_n == 3 && s.heap_cap == 4 + 8 + 16 && s.heap_used == 40 + 80 + 160);
    CHECK(s.chain_truncated == 0);
    // firmware with no MANUALLY_RESERVED_* define: sentinel-only table, nres == 0 -> everything is heap
    const uint32_t lo0[1] = {0}, hi0[1] = {0};
    hap_arena_stats(a, lo0, hi0, 0, &s);
    CHECK(s.res_n == 0 && s.heap_n == 6 && s.heap_cap == 63 && s.heap_used == 630);
    // only the first range: header inside R1 is heap now
    hap_arena_stats(a, lo, hi, 1, &s);
    CHECK(s.res_n == 2 && s.res_cap == 3 && s.heap_n == 4 && s.heap_cap == 60);
}

static void test_arena_stats_and_counters()
{
    alignas(16) static uint8_t reserved_mem[256], heap_mem1[1040], heap_mem2[1040];
    FakeHeader *r = reinterpret_cast<FakeHeader *>(reserved_mem);
    FakeHeader *h1 = reinterpret_cast<FakeHeader *>(heap_mem1);
    FakeHeader *h2 = reinterpret_cast<FakeHeader *>(heap_mem2);
    *r = {h1, 240, 236, reserved_mem + 16}; *h1 = {h2, 1024, 1000, heap_mem1 + 16}; *h2 = {nullptr, 1024, 12, heap_mem2 + 16};
    struct FakeArena { FakeHeader *head; size_t allocs, padding_overhead, allocated_blocks; } a{r, 77, 9, 2};
    const uint32_t lo[1] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(reserved_mem))};
    const uint32_t hi[1] = {lo[0] + 256};
    HapArenaStats s;
    hap_arena_stats(a, lo, hi, 1, &s);
    CHECK(s.res_n == 1 && s.res_cap == 240 && s.res_used == 236);
    CHECK(s.heap_n == 2 && s.heap_cap == 2048 && s.heap_used == 1012);
    CHECK(s.allocs == 77 && s.padding == 9 && s.allocated_blocks == 2 && s.chain_truncated == 0);
    // A reserved interval is half-open; an empty interval contains no headers.
    const uint32_t empty_hi[1] = {lo[0]};
    hap_arena_stats(a, lo, empty_hi, 1, &s);
    CHECK(s.res_n == 0 && s.res_cap == 0 && s.heap_n == 3);
    h2->next = h1;  // corrupted cycle -> bounded
    hap_arena_stats(a, lo, hi, 1, &s);
    CHECK(s.chain_truncated == 1);

    HapArenaCounters c; memset(&c, 0, sizeof(c));
    hap_counters_note_direct(&c, (void *)0x3FFB0000, 2000);
    CHECK(c.direct_n == 0);                                   // unarmed: ignored
    c.armed = 1;
    for (uint32_t i = 0; i < HAP_DIRECT_MAX + 2; ++i) hap_counters_note_direct(&c, (void *)(uintptr_t)(0x3FFB0000 + i * 0x1000), 1100 + i);
    hap_counters_note_direct(&c, nullptr, 3000);
    CHECK(c.direct_n == HAP_DIRECT_MAX + 2 && c.direct_fail == 1);
    CHECK(c.direct_addr[HAP_DIRECT_MAX - 1] == 0x3FFB0000 + (HAP_DIRECT_MAX - 1) * 0x1000);
    uint32_t sum = 0; for (uint32_t i = 0; i < HAP_DIRECT_MAX + 2; ++i) sum += 1100 + i;
    CHECK(c.direct_bytes == sum);
}

static void test_format()
{
    HapRegion r{0x3FFE4350, 0x40000000, 8000, 100000, 4852, 9, 120};
    char buf[240];
    hap_format_region(buf, sizeof(buf), 3, &r);
    CHECK(strcmp(buf, "HAP1 r3 0x3ffe4350-0x40000000 free=8000 largest=4852 nfree=9 used=100000 nused=120") == 0);
    HapOwner owners[3];
    owners[0] = {0x3FFE5650, HAP_OWN_TCB, "nimble_host"};
    owners[1] = {0x3FFE5700, HAP_OWN_STACK, "mqtt_task"};
    owners[2] = {0x3FFE7168, HAP_OWN_ARENA_DIRECT, ""};
    HapTop t{{0x3FFE6E44, 800, 0, 1}, {0x3FFE5644, 6144, 1, 1}, {0x3FFE7164, 16, 1, 1}, 2, 0};
    hap_format_top(buf, sizeof(buf), 0, &t, owners, 3);
    CHECK(strcmp(buf, "HAP1 top0 r2 0x3ffe6e44+800 prev=0x3ffe5644+6144:U:tcb:nimble_host next=0x3ffe7164+16:U:arena_dir") == 0);
    HapTop u{{0x3FFB028A, 1000, 0, 1}, {0x3FFB0258, 50, 0, 1}, {0, 0, 0, 0}, 0, 0};
    hap_format_top(buf, sizeof(buf), 1, &u, owners, 0);
    CHECK(strcmp(buf, "HAP1 top1 r0 0x3ffb028a+1000 prev=0x3ffb0258+50:F next=-") == 0);
    HapTop v{{0x3FFB0000, 64, 0, 1}, {0x3FFAF000, 128, 1, 1}, {0, 0, 0, 0}, 0, 0};
    hap_format_top(buf, sizeof(buf), 2, &v, owners, 3);
    CHECK(strstr(buf, "prev=0x3faf000") == nullptr && strstr(buf, "prev=0x3ffaf000+128:U:? next=-") != nullptr);
    // undersized buffer: truncation, NUL-terminated, canary intact
    char small[24 + 4]; memset(small, 0x5A, sizeof(small));
    const int n = hap_format_top(small, 24, 0, &t, owners, 3);
    CHECK(n > 24 && small[23] == '\0' && small[24] == 0x5A && small[27] == 0x5A);
    HapArenaStats s{1, 15056, 14980, 3, 3072, 2100, 900, 40, 3, 0};
    HapArenaCounters c; memset(&c, 0, sizeof(c)); c.armed = 1; c.direct_n = 2; c.direct_bytes = 2037 + 1500;
    hap_format_arena(buf, sizeof(buf), &s, &c);
    CHECK(strcmp(buf, "HAP1 arena dram res_n=1 res_cap=15056 res_used=14980 heap_n=3 heap_cap=3072 heap_used=2100 allocs=900 pad=40 blocks=3 trunc=0 direct_n=2 direct_bytes=3537 direct_fail=0 armed=1") == 0);
    CHECK(strlen(buf) < 239);
}

int main()
{
    test_walk_topn_neighbours();
    test_region_bound_and_external_block();
    test_cross_region_neighbour_boundary();
    test_arena_reserved_boundary();
    test_arena_stats_and_counters();
    test_format();
    static_assert(HAP_CAPS == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT), "internal 8-bit only");
    static_assert((HAP_CAPS & MALLOC_CAP_SPIRAM) == 0, "no PSRAM");
    printf("%s failures=%d\n", failures ? "HAP1 TESTS FAIL" : "HAP1 TESTS PASS", failures);
    return failures ? 1 : 0;
}
