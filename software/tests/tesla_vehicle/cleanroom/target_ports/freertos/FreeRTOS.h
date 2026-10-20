// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cassert>
#include <mutex>
#include <new>
namespace TargetMutexFixture {
inline unsigned creates=0,deletes=0,takes=0,gives=0,live=0;
inline bool fail_create=false,fail_take=false;
struct Semaphore { std::mutex mutex; bool held=false; };
}
using SemaphoreHandle_t=TargetMutexFixture::Semaphore *;
constexpr int pdTRUE=1,portMAX_DELAY=0x7fffffff;
// Separate static critical-section exclusion; semaphore failure/count fixtures
// continue to apply only to dynamically created task mutexes.
struct portMUX_TYPE { std::mutex mutex; };
#define portMUX_INITIALIZER_UNLOCKED {}
inline void portENTER_CRITICAL(portMUX_TYPE *mux) { mux->mutex.lock(); }
inline void portEXIT_CRITICAL(portMUX_TYPE *mux) { mux->mutex.unlock(); }
