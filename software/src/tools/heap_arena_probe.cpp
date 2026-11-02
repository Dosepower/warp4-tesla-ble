/* HAP1 heap/arena probe firmware glue (DIAGNOSTIC ONLY). Compiled only with -DWARP_HEAP_ARENA_PROBE=1, which
 * tesla_production_build.py permits only in env warp4_tesla_stack_probe.
 *
 * - State lives in ONE PSRAM block allocated at tools_malloc_pre_setup() (internal heap untouched; if PSRAM is
 *   unavailable the probe disables itself, never falls back to internal RAM). Only a 4-byte pointer is in .bss.
 * - hap_probe_capture(): main thread only (first caller = setup() fixes the thread; other threads are refused).
 *   Order: heap_caps_walk(INTERNAL|8BIT) -> heap_caps_get_info -> dram_arena chain -> uxTaskGetSystemState owners.
 *   None of these allocate. Regions are each consistent under their own lock; the whole capture is not atomic.
 * - hap_probe_emit(): event-log lines, called by the owner AFTER its measured window.
 */
#if defined(WARP_HEAP_ARENA_PROBE)

#include "heap_arena_probe.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <esp_memory_utils.h>
#include <soc/soc.h>

#include "arena.h"
#include "event_log_prefix.h"
#include "main_dependencies.h"

static_assert(HAP_DRAM_LOW == SOC_DRAM_LOW, "HAP DRAM window must match SOC_DRAM_LOW");
static_assert(HAP_DRAM_HIGH == SOC_DRAM_HIGH, "HAP DRAM window must match SOC_DRAM_HIGH");

extern Arena dram_arena;

#define HAP_MAX_TASKS  48U
#define HAP_MAX_OWNERS (2U * HAP_MAX_TASKS + HAP_ARENA_CHAIN_GUARD + HAP_DIRECT_MAX)

struct HapState {
    HapWalk walk;
    HapArenaStats arena;
    HapArenaCounters counters;
    multi_heap_info_t info;
    TaskStatus_t tasks[HAP_MAX_TASKS];
    HapOwner owners[HAP_MAX_OWNERS];
    uint32_t nowners;
    uint32_t ntasks;
    uint32_t snaps;
    uint32_t skipped_thread;
    uint32_t captured;
    int64_t walk_us;
    int64_t capture_us;
    TaskHandle_t main_task;
    const char *tag;
};

static HapState *hap = nullptr;

static const uint32_t hap_res_lo[] = {
#if defined(MANUALLY_RESERVED_6432B_AT_0x3FFAE6E0_DRAM)
    0x3FFAE6E0UL,
#endif
#if defined(MANUALLY_RESERVED_15072B_AT_0x3FFE0440_DRAM)
    0x3FFE0440UL,
#endif
    0UL};
static const uint32_t hap_res_hi[] = {
#if defined(MANUALLY_RESERVED_6432B_AT_0x3FFAE6E0_DRAM)
    0x3FFAE6E0UL + 6432UL,
#endif
#if defined(MANUALLY_RESERVED_15072B_AT_0x3FFE0440_DRAM)
    0x3FFE0440UL + 15072UL,
#endif
    0UL};
static constexpr size_t hap_nres = sizeof(hap_res_lo) / sizeof(hap_res_lo[0]) - 1;

void hap_probe_pre_setup()
{
    void *p = heap_caps_calloc(1, sizeof(HapState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p != nullptr && !esp_ptr_external_ram(p)) {
        heap_caps_free(p);
        p = nullptr;
    }
    hap = static_cast<HapState *>(p);
    if (hap != nullptr) {
        hap->counters.armed = 1;
    }
}

void hap_arena_note_direct(const Arena *a, const void *p, size_t size)
{
    if (hap != nullptr && a == &dram_arena) {
        hap_counters_note_direct(&hap->counters, p, size);
    }
}

static void hap_add_owner(uint32_t addr, uint8_t kind, const char *name)
{
    if (hap->nowners >= HAP_MAX_OWNERS || addr == 0) {
        return;
    }
    HapOwner &o = hap->owners[hap->nowners++];
    o.addr = addr;
    o.kind = kind;
    o.name[0] = '\0';
    if (name != nullptr) {
        strncpy(o.name, name, sizeof(o.name) - 1);
        o.name[sizeof(o.name) - 1] = '\0';
    }
}

bool hap_probe_capture(const char *tag)
{
    // One reused snapshot; later MQTT attempts must not become invisible.
    if (hap == nullptr) {
        return false;
    }
    TaskHandle_t cur = xTaskGetCurrentTaskHandle();
    if (hap->main_task == nullptr) {
        hap->main_task = cur;
    } else if (cur != hap->main_task) {
        ++hap->skipped_thread;
        return false;
    }

    const int64_t t0 = esp_timer_get_time();
    hap_walk_reset(&hap->walk);
    heap_caps_walk(HAP_CAPS, hap_walker_cb, &hap->walk);
    const int64_t t1 = esp_timer_get_time();
    heap_caps_get_info(&hap->info, HAP_CAPS);
    hap_arena_stats(dram_arena, hap_res_lo, hap_res_hi, hap_nres, &hap->arena);

    hap->nowners = 0;
    uint32_t guard = 0;
    for (const Arena::BlockHeader *b = dram_arena.head; b != nullptr && guard < HAP_ARENA_CHAIN_GUARD; b = b->next, ++guard) {
        hap_add_owner(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b)), HAP_OWN_ARENA_BLK, nullptr);
    }
    const uint32_t nd = hap->counters.direct_n < HAP_DIRECT_MAX ? hap->counters.direct_n : HAP_DIRECT_MAX;
    for (uint32_t i = 0; i < nd; ++i) {
        hap_add_owner(hap->counters.direct_addr[i], HAP_OWN_ARENA_DIRECT, nullptr);
    }
    // Numeric-only snapshot: never dereference pcTaskName or a copied handle.
    // The SDK fills these values while its task-list lock is held. Task deletion
    // after return cannot invalidate the copied integers themselves. Heap/task
    // snapshots are NOT atomic together; attribution remains time-local evidence.
    hap->ntasks = static_cast<uint32_t>(uxTaskGetSystemState(hap->tasks, HAP_MAX_TASKS, nullptr));
    for (uint32_t i = 0; i < hap->ntasks; ++i) {
        const TaskStatus_t &ts = hap->tasks[i];
        hap_add_owner(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ts.xHandle)), HAP_OWN_TCB, nullptr);
        hap_add_owner(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ts.pxStackBase)), HAP_OWN_STACK, nullptr);
    }

    hap->walk_us = t1 - t0;
    hap->capture_us = esp_timer_get_time() - t0;
    hap->tag = tag;
    hap->captured = 1;
    ++hap->snaps;
    return true;
}

void hap_probe_emit()
{
    if (hap == nullptr) {
        logger.printfln("HAP1 disabled (no PSRAM state)");
        return;
    }
    if (!hap->captured) {
        return;
    }
    hap->captured = 0;
    const HapWalk &w = hap->walk;
    logger.printfln("HAP1 snap=%s n=%u walk_us=%ld cap_us=%ld blocks=%u regions=%u dropped=%u ext_rej=%u tasks=%u owners=%u skip_thr=%u",
                    hap->tag, static_cast<unsigned>(hap->snaps), static_cast<long>(hap->walk_us), static_cast<long>(hap->capture_us),
                    static_cast<unsigned>(w.blocks), static_cast<unsigned>(w.nregions), static_cast<unsigned>(w.regions_dropped),
                    static_cast<unsigned>(w.rejected_external), static_cast<unsigned>(hap->ntasks), static_cast<unsigned>(hap->nowners),
                    static_cast<unsigned>(hap->skipped_thread));
    logger.printfln("HAP1 info free=%u largest=%u min=%u fblocks=%u ablocks=%u tblocks=%u",
                    static_cast<unsigned>(hap->info.total_free_bytes), static_cast<unsigned>(hap->info.largest_free_block),
                    static_cast<unsigned>(hap->info.minimum_free_bytes), static_cast<unsigned>(hap->info.free_blocks),
                    static_cast<unsigned>(hap->info.allocated_blocks), static_cast<unsigned>(hap->info.total_blocks));
    char buf[240];
    for (uint32_t i = 0; i < w.nregions; ++i) {
        hap_format_region(buf, sizeof(buf), i, &w.regions[i]);
        logger.printfln("%s", buf);
    }
    for (uint32_t i = 0; i < w.ntop; ++i) {
        hap_format_top(buf, sizeof(buf), i, &w.top[i], hap->owners, hap->nowners);
        logger.printfln("%s", buf);
    }
    hap_format_arena(buf, sizeof(buf), &hap->arena, &hap->counters);
    logger.printfln("%s", buf);
    // Lab-only ownership inventory: emit previously captured numeric addresses.
    // No arena traversal, allocation or foreign task access during this output.
    for (uint32_t i = 0; i < hap->nowners; ++i) {
        const HapOwner &owner = hap->owners[i];
        if (owner.kind == HAP_OWN_ARENA_BLK) {
            logger.printfln("HAP1 arena_owner%u addr=0x%08x", static_cast<unsigned>(i), static_cast<unsigned>(owner.addr));
        }
    }
    const uint32_t nd = hap->counters.direct_n < HAP_DIRECT_MAX ? hap->counters.direct_n : HAP_DIRECT_MAX;
    for (uint32_t i = 0; i < nd; ++i) {
        logger.printfln("HAP1 adir%u 0x%08x+%u", static_cast<unsigned>(i), static_cast<unsigned>(hap->counters.direct_addr[i]),
                        static_cast<unsigned>(hap->counters.direct_size[i]));
    }
}

#endif
