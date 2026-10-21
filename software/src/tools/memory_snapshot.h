#pragma once
#include <cstddef>
#include <cstdio>

// Caller supplies IDF heap API declarations. No heap allocation, identifiers,
// configuration data or persistent writes; sampled counters are not atomic.
// min_free_dram is the SDK sum of per-region low watermarks, not necessarily
// one simultaneous total. min_free_psram=null means unavailable: this SDK adds
// PSRAM after startup with an initial minimum of zero. Do not reset global
// heap monitoring on GET. Free/largest omit slack in reserved permanent arenas.
inline bool memory_snapshot_json(char *buffer, size_t capacity)
{
    const unsigned internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const unsigned external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const int n = snprintf(buffer, capacity,
        "{\"free_dram\":%zu,\"min_free_dram\":%zu,\"largest_free_dram_block\":%zu,"
        "\"free_psram\":%zu,\"min_free_psram\":null,\"largest_free_psram_block\":%zu}",
        heap_caps_get_free_size(internal), heap_caps_get_minimum_free_size(internal),
        heap_caps_get_largest_free_block(internal), heap_caps_get_free_size(external),
        heap_caps_get_largest_free_block(external));
    return n >= 0 && static_cast<size_t>(n) < capacity;
}
