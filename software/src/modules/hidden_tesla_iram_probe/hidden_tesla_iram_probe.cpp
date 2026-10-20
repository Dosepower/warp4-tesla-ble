#include "hidden_tesla_iram_probe.h"
#if !defined(TESLA_BLE_CONFIG_MEMORY_PROBE) || !TESLA_BLE_CONFIG_MEMORY_PROBE
#error "Aggregate allocator probe requires its dedicated non-production profile"
#endif
#if defined(TESLA_BLE_R13_MASTER_BENCH) && TESLA_BLE_R13_MASTER_BENCH
#error "A reduced master bench is not a production-equivalent memory probe"
#endif
#include "modules.h"
#include "generated/module_dependencies.h"
#include "modules/api/api.h"
#include "tools/malloc.h"
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <stdint.h>
// Existing public-linkage allocator object; observation only, no core edit.
extern Arena iram_arena;
static_assert(sizeof(micros_t) == 8 && alignof(micros_t) == 8);
static_assert(sizeof(Arena::BlockHeader) == 16);

static void snapshot(unsigned phase, uint32_t pre_count)
{
    // No trial allocation, registration, task, radio, path, ID or pointer output.
    // Per-heap locking does not make these views one atomic global snapshot.
    const size_t n = api.states.size();
    const bool bounded = n > 0 && n <= (UINT32_MAX - 8220U) / 8U;
    const uint32_t payload = bounded ? static_cast<uint32_t>(n * 8U) : UINT32_MAX;
    uint32_t blocks = 0, unused = 0, largest_aligned = 0;
    bool integrity = true;
    const auto *block = iram_arena.head;
    for (; block && blocks < 1024U; block = block->next) {
        ++blocks;
        if (block->used > block->capacity || !block->memory || block->capacity > UINT32_MAX - unused) {
            integrity = false; break;
        }
        const size_t remaining = block->capacity - block->used;
#ifdef DEBUG_FS_ENABLE
        constexpr size_t canary = Arena::CANARY_SIZE;
#else
        constexpr size_t canary = 0;
#endif
        const size_t padding = (0U - (reinterpret_cast<uintptr_t>(block->memory) + block->used + canary)) & 7U;
        const size_t available = remaining >= canary + padding ? remaining - canary - padding : 0;
        unused += static_cast<uint32_t>(remaining);
        if (available > largest_aligned) { largest_aligned = static_cast<uint32_t>(available); }
    }
    integrity = integrity && !block;
    multi_heap_info_t strict{}, executable{}, internal8{};
    heap_caps_get_info(&strict, MALLOC_CAP_IRAM);
    heap_caps_get_info(&executable, MALLOC_CAP_EXEC | MALLOC_CAP_INTERNAL);
    heap_caps_get_info(&internal8, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const bool arena_fits = bounded && integrity && iram_arena.head && payload <= largest_aligned;
    const uint32_t required = bounded ? (n > 128U ? payload + 28U : 1076U) : UINT32_MAX;
    // Feasibility, NOT allocation success or a post-allocation margin oracle.
    const bool geometry_bound =
#if defined(CONFIG_HEAP_POISONING_LIGHT) && CONFIG_HEAP_POISONING_LIGHT && !defined(DEBUG_FS_ENABLE)
        iram_arena.block_capacity == 1024U && iram_arena.min_alignment == 4U;
#else
        false;
#endif
    const bool pre_feasible = phase == 0 && bounded && integrity && geometry_bound && iram_arena.head &&
                              (arena_fits || strict.largest_free_block >= required);
    // 8220 is sufficient for a further 8192-byte aligned direct payload at THIS
    // post-events snapshot. It is not a guarantee about subsequent allocations.
    const bool post_reserve = phase == 1 && strict.largest_free_block >= 8220U && geometry_bound;
    esp_rom_printf("TESLA_MQTT_MEMORY schema=2 phase=%u n=%u pre_n=%u payload=%u strict_free=%u strict_largest=%u strict_min=%u exec_free=%u exec_largest=%u exec_min=%u internal8_free=%u internal8_largest=%u internal8_min=%u arena_blocks=%u arena_unused=%u arena_largest8=%u arena_integrity=%u arena_fits=%u geometry_bound=%u nofit_required=%u pre_feasible=%u post_reserve_payload=%u post_reserve=%u\n",
        phase, static_cast<unsigned>(n), pre_count, payload,
        static_cast<unsigned>(strict.total_free_bytes), static_cast<unsigned>(strict.largest_free_block), static_cast<unsigned>(strict.minimum_free_bytes),
        static_cast<unsigned>(executable.total_free_bytes), static_cast<unsigned>(executable.largest_free_block), static_cast<unsigned>(executable.minimum_free_bytes),
        static_cast<unsigned>(internal8.total_free_bytes), static_cast<unsigned>(internal8.largest_free_block), static_cast<unsigned>(internal8.minimum_free_bytes),
        blocks, unused, largest_aligned, static_cast<unsigned>(integrity), static_cast<unsigned>(arena_fits), static_cast<unsigned>(geometry_bound), required,
        static_cast<unsigned>(pre_feasible), 8192U, static_cast<unsigned>(post_reserve));
}
void HiddenTeslaIramProbe::register_events()
{
    pre_count = api.states.size() <= UINT32_MAX ? static_cast<uint32_t>(api.states.size()) : UINT32_MAX;
    snapshot(0, pre_count);
}
void HiddenTeslaIramProbe::loop()
{
    if (post_emitted) { return; }
    post_emitted = true;
    // First main-loop entry proves all stock register_events hooks returned,
    // including MQTT. It does not prove an MQTT connection or publishing works.
    snapshot(1, pre_count);
}
