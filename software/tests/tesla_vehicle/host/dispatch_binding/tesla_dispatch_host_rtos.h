#pragma once
// TEST-ONLY host FreeRTOS boundary, included only by the production tesla_nimble_dispatch.cpp translation unit in the
// dispatch-binding host test. Binary semaphores block in real time; the dispatcher helper is a real detached thread
// (it is never deleted, matching production). Not part of any firmware build.
#include "tesla_dispatch_host_sdk.h"
#include <condition_variable>
#include <mutex>
#include <new>
#include <thread>
#include <type_traits>
struct TeslaDispatchHostSem { std::mutex m; std::condition_variable cv; bool given = false; };
// Like FreeRTOS, StaticSemaphore_t is trivially destructible raw storage: the host semaphore is placement-constructed by
// xSemaphoreCreateBinaryStatic and intentionally never destroyed (production never deletes it), so a static dispatcher's
// exit-time destructor cannot destroy a condition_variable the detached, process-lifetime helper may still wait on.
struct StaticSemaphore_t { alignas(TeslaDispatchHostSem) unsigned char storage[sizeof(TeslaDispatchHostSem)]; };
static_assert(std::is_trivially_destructible<StaticSemaphore_t>::value, "StaticSemaphore_t must be trivially destructible like FreeRTOS");
using SemaphoreHandle_t = TeslaDispatchHostSem *;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
inline SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *b) { return ::new (static_cast<void *>(b->storage)) TeslaDispatchHostSem(); }
inline int xSemaphoreGive(SemaphoreHandle_t s)
{
    { std::lock_guard<std::mutex> l(s->m); if (s->given) { return 0; } s->given = true; }
    s->cv.notify_all();
    return pdTRUE;
}
inline int xSemaphoreTake(SemaphoreHandle_t s, uint32_t ticks)
{
    std::unique_lock<std::mutex> l(s->m);
    if (ticks == portMAX_DELAY) { s->cv.wait(l, [s] { return s->given; }); }
    else if (!s->cv.wait_for(l, std::chrono::milliseconds(ticks), [s] { return s->given; })) { return 0; }
    s->given = false;
    return pdTRUE;
}
inline uint8_t *pxTaskGetStackStart(TaskHandle_t) { alignas(16) static uint8_t internal_stack[8192]; return internal_stack; }
inline std::atomic<unsigned> tesla_dispatch_host_helpers{0};
inline int tesla_dispatch_host_create_task(void (*entry)(void *), const char *name, int stack, void *ctx, int, TaskHandle_t *out, int)
{
    auto *t = new HostTask{entry, ctx, false, false, name, stack}; // never deleted, like the production helper
    *out = t;
    tesla_dispatch_host_helpers.fetch_add(1);
    std::thread([t] { run_task(t); }).detach();
    return pdPASS;
}
#define xTaskCreatePinnedToCore tesla_dispatch_host_create_task
