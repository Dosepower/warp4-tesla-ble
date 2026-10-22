#pragma once
#include <cstdint>
#include <cstddef>
#ifndef WARP_TESLA_MEMORY_PHASES
#define WARP_TESLA_MEMORY_PHASES 0
#endif
#if WARP_TESLA_MEMORY_PHASES
#include <cstdio>
#include <limits>
#ifdef TESLA_MEMORY_HOST_TEST
#include "bluetooth_memory_platform.h"
#else
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Arduino.h>
#endif
#endif

// Read-only observations. No task handle, object pointer, identity, packet,
// keys or API action is retained. All stack reads are of the executing task.
// HCI/BTU/controller tasks are NOT sampled by this instrument: no unsafe handle
// lookup or global task-table walk is used. BTC is sampled after our callbacks.
namespace bluetooth_memory {
enum class Stage : uint8_t {
    Boot, ContextBefore, ContextAfter, ControllerInit, ControllerEnabled,
    HostInit, HostEnabled, GattReady, BtActive, BtStopBefore, BtStopAfter,
    RuntimeBefore, QueuesReady, TaskCreated, WorkerBegin, WorkerActive,
    ProtocolReady, ProfileApplied, WorkerStopped, TeardownStopped,
    GattCallback, GapCallback, Count
};
#if WARP_TESLA_MEMORY_PHASES
struct Sample {
    uint32_t ms=0, free_internal=0, min_internal=0, largest_internal=0;
    uint32_t free_dma=0, largest_dma=0, free_psram=0, largest_psram=0;
    uint32_t stack_min=0, samples=0;
};
static_assert(sizeof(Sample)==40,"Bounded scalar-only sample");
inline Sample slots[static_cast<unsigned>(Stage::Count)]{};
#ifdef TESLA_MEMORY_HOST_TEST
inline std::mutex mutex;
struct Guard { std::lock_guard<std::mutex> lock{mutex}; };
#else
// This lock and the scalar table stay internal, not in a PSRAM-backed object.
inline portMUX_TYPE mutex=portMUX_INITIALIZER_UNLOCKED;
struct Guard { Guard(){portENTER_CRITICAL(&mutex);} ~Guard(){portEXIT_CRITICAL(&mutex);} };
#endif
inline const char *name(Stage stage) {
    static constexpr const char *names[]={"boot","context_before","context_after","controller_init","controller_enabled",
        "host_init","host_enabled","gatt_ready","bt_active","bt_stop_before","bt_stop_after",
        "runtime_before","queues_ready","task_created","worker_begin","worker_active","protocol_ready",
        "profile_applied","worker_stopped","teardown_stopped","gatt_callback","gap_callback"};
    static_assert(sizeof(names)/sizeof(*names)==static_cast<unsigned>(Stage::Count));
    return static_cast<unsigned>(stage)<static_cast<unsigned>(Stage::Count)?names[static_cast<unsigned>(stage)]:"invalid";
}
inline void sample(Stage stage,bool current_stack=false,uint32_t interval_ms=0) {
    const unsigned index=static_cast<unsigned>(stage);
    if(index>=static_cast<unsigned>(Stage::Count))return;
#ifdef TESLA_MEMORY_HOST_TEST
    const uint32_t now=memory_current_ms();
#else
    const uint32_t now=millis();
#endif
    if(interval_ms) {Guard guard;if(slots[index].samples && now-slots[index].ms<interval_ms)return;}
    const unsigned internal=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    const unsigned dma=internal|MALLOC_CAP_DMA;
    const unsigned psram=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
    Sample value;
    value.ms=now;
    value.free_internal=heap_caps_get_free_size(internal);
    value.min_internal=heap_caps_get_minimum_free_size(internal);
    value.largest_internal=heap_caps_get_largest_free_block(internal);
    value.free_dma=heap_caps_get_free_size(dma);
    value.largest_dma=heap_caps_get_largest_free_block(dma);
    value.free_psram=heap_caps_get_free_size(psram);
    value.largest_psram=heap_caps_get_largest_free_block(psram);
#ifdef TESLA_MEMORY_HOST_TEST
    value.stack_min=current_stack?memory_current_stack(nullptr):0;
#else
    // ESP-IDF FreeRTOS reports bytes (unlike vanilla word-based FreeRTOS).
    value.stack_min=current_stack?uxTaskGetStackHighWaterMark(nullptr):0;
#endif
    // Heap queries and stack scanning occur OUTSIDE the scalar-copy lock.
    Guard guard;
    const auto &old=slots[index];
    value.samples=old.samples==UINT32_MAX?UINT32_MAX:old.samples+1;
    if(old.samples && current_stack && old.stack_min<value.stack_min)value.stack_min=old.stack_min;
    slots[index]=value;
}
inline bool read(Stage stage,Sample &out) {
    const unsigned index=static_cast<unsigned>(stage);
    if(index>=static_cast<unsigned>(Stage::Count))return false;
    Guard guard;out=slots[index];return true;
}
inline bool json(Stage stage,char *buffer,size_t capacity) {
    Sample s;if(!read(stage,s))return false;
    const int n=snprintf(buffer,capacity,
        "{\"stage\":\"%s\",\"samples\":%lu,\"ms\":%lu,\"free_internal\":%lu,\"min_internal\":%lu,\"largest_internal\":%lu,"
        "\"free_dma\":%lu,\"largest_dma\":%lu,\"free_psram\":%lu,\"largest_psram\":%lu,\"stack_min_bytes\":%lu}",
        name(stage),(unsigned long)s.samples,(unsigned long)s.ms,(unsigned long)s.free_internal,
        (unsigned long)s.min_internal,(unsigned long)s.largest_internal,(unsigned long)s.free_dma,
        (unsigned long)s.largest_dma,(unsigned long)s.free_psram,(unsigned long)s.largest_psram,(unsigned long)s.stack_min);
    return n>=0 && static_cast<size_t>(n)<capacity;
}
#else
inline void sample(Stage,bool=false,uint32_t=0) {}
#endif
} // namespace bluetooth_memory
