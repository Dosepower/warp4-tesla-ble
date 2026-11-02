#pragma once
// HOST TEST ONLY (T1r10): deterministic heap/clock/stack stand-ins for bluetooth_memory.h.
// Every value a sampler writes equals its thread-local token, so a torn (mixed) Sample is detectable by readers.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#define MALLOC_CAP_8BIT (1u << 2)
#define MALLOC_CAP_DMA (1u << 3)
#define MALLOC_CAP_SPIRAM (1u << 10)
#define MALLOC_CAP_INTERNAL (1u << 11)
inline thread_local uint32_t memory_test_token = 0;
inline thread_local uint32_t memory_test_stack = 0;
inline std::atomic<uint32_t> memory_heap_queries{0};
inline uint32_t memory_current_ms() { return memory_test_token; }
inline uint32_t memory_current_stack(void *) { return memory_test_stack; }
inline size_t heap_caps_get_free_size(unsigned) { memory_heap_queries++; return memory_test_token; }
inline size_t heap_caps_get_minimum_free_size(unsigned) { memory_heap_queries++; return memory_test_token; }
inline size_t heap_caps_get_largest_free_block(unsigned) { memory_heap_queries++; return memory_test_token; }
