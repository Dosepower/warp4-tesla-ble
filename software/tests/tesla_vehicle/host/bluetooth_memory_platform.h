#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <mutex>
constexpr unsigned MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2,MALLOC_CAP_DMA=4,MALLOC_CAP_SPIRAM=8;
inline size_t heap_caps_get_free_size(unsigned caps) { return caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)?101:caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT)?104:106; }
inline size_t heap_caps_get_largest_free_block(unsigned caps) { return caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)?103:caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT)?105:107; }
inline size_t heap_caps_get_minimum_free_size(unsigned) { return 102; }
inline unsigned memory_current_stack(void *handle) { assert(handle==nullptr);return 2048; }
inline uint32_t memory_current_ms() { return 100; }
