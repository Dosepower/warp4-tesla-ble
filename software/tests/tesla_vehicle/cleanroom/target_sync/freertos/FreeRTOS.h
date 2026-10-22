#pragma once
#include <cstdint>
#include <mutex>
#define pdTRUE 1
constexpr uint32_t portMAX_DELAY=UINT32_MAX;
inline thread_local bool target_main_zero_wait=false;
// Model cross-core critical-section exclusion separately from task semaphore
// waits. The semaphore mock's main-task zero-wait assertion stays unchanged.
struct portMUX_TYPE { std::mutex mutex; };
#define portMUX_INITIALIZER_UNLOCKED {}
inline void portENTER_CRITICAL(portMUX_TYPE *mux) { mux->mutex.lock(); }
inline void portEXIT_CRITICAL(portMUX_TYPE *mux) { mux->mutex.unlock(); }
