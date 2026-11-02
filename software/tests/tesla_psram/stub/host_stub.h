#pragma once
// HOST STUB ONLY: two labelled fake arenas ("PSRAM" / "INTERNAL"). Proves the lab header's
// ownership/labelling logic, NOT real ESP32 placement (device readback fields prove that, after parent flash).
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <map>
typedef int BaseType_t; typedef unsigned UBaseType_t; typedef uint32_t configSTACK_DEPTH_TYPE; typedef uint8_t StackType_t;
struct StaticTask_t { int dummy[22]; };
typedef void (*TaskFunction_t)(void *);
struct HostTask { StackType_t *stack; StaticTask_t *tcb; int tls_cb_runs; };
typedef HostTask *TaskHandle_t;
#define pdPASS 1
#define pdFAIL 0
#define pdTRUE 1
#define MALLOC_CAP_SPIRAM (1u<<10)
#define MALLOC_CAP_8BIT (1u<<2)
namespace hs {
enum Region { NONE, PSRAM, INTERNAL };
constexpr size_t ARENA = 1 << 20;
alignas(16) inline uint8_t psram[ARENA]; alignas(16) inline uint8_t internal[ARENA];
inline size_t psram_top = 0, internal_top = 0;
inline std::map<const void *, Region> live; // outstanding allocations and their region
inline int wrong_region_frees = 0, double_frees = 0, psram_alloc_calls = 0, internal_alloc_calls = 0;
inline bool fail_next_stack_alloc = false;
inline void *sp = nullptr; // simulated current stack pointer
inline Region region_of(const void *p) {
    auto a = reinterpret_cast<uintptr_t>(p);
    if (a >= (uintptr_t)psram && a < (uintptr_t)psram + ARENA) return PSRAM;
    if (a >= (uintptr_t)internal && a < (uintptr_t)internal + ARENA) return INTERNAL;
    return NONE; }
inline void *alloc(Region r, size_t n) {
    n = (n + 15) & ~size_t(15);
    size_t &top = r == PSRAM ? psram_top : internal_top; uint8_t *base = r == PSRAM ? psram : internal;
    if (top + n > ARENA) { return nullptr; }
    void *p = base + top; top += n; live[p] = r;
    (r == PSRAM ? psram_alloc_calls : internal_alloc_calls)++; return p; }
inline void free_in(Region expected, void *p) {
    if (!p) { return; }
    auto it = live.find(p);
    if (it == live.end()) { double_frees++; return; }
    if (it->second != expected) { wrong_region_frees++; }
    live.erase(it); }
}
inline bool esp_ptr_external_ram(const void *p) { return hs::region_of(p) == hs::PSRAM; }
inline bool esp_ptr_internal(const void *p) { return hs::region_of(p) == hs::INTERNAL; }
inline void *esp_cpu_get_sp() { return hs::sp; }
inline void *pvPortMalloc(size_t n) { return hs::alloc(hs::INTERNAL, n); }
inline void vPortFree(void *p) { hs::free_in(hs::INTERNAL, p); }
inline void *heap_caps_malloc(size_t n, uint32_t caps) { return (caps & MALLOC_CAP_SPIRAM) ? hs::alloc(hs::PSRAM, n) : hs::alloc(hs::INTERNAL, n); }
inline void heap_caps_free(void *p) { hs::free_in(hs::region_of(p) == hs::PSRAM ? hs::PSRAM : hs::INTERNAL, p); }
// Mirrors IDF v5.5 idf_additions.c order: TCB via pvPortMalloc, stack via heap_caps_malloc(caps); on stack OOM the TCB is freed.
inline BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t, const char *, configSTACK_DEPTH_TYPE depth, void *, UBaseType_t, TaskHandle_t *h, BaseType_t, UBaseType_t caps) {
    auto *tcb = static_cast<StaticTask_t *>(pvPortMalloc(sizeof(StaticTask_t)));
    if (!tcb) return pdFAIL;
    StackType_t *stack = hs::fail_next_stack_alloc ? nullptr : static_cast<StackType_t *>(heap_caps_malloc(depth, caps));
    hs::fail_next_stack_alloc = false;
    if (!stack) { vPortFree(tcb); return pdFAIL; }
    *h = new HostTask{stack, tcb, 0}; return pdPASS; }
inline BaseType_t xTaskGetStaticBuffers(TaskHandle_t h, StackType_t **s, StaticTask_t **t) { *s = h->stack; *t = h->tcb; return pdTRUE; }
// Mirrors vTaskDeleteWithCaps: TLS deletion callbacks run, then stack heap_caps_free + TCB vPortFree.
inline void vTaskDeleteWithCaps(TaskHandle_t h) { h->tls_cb_runs++; heap_caps_free(h->stack); vPortFree(h->tcb); delete h; }
