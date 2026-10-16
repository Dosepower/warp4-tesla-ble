#pragma once
#include "FreeRTOS.h"
#include <cassert>
#include <mutex>
#include <new>
using SemaphoreHandle_t=std::mutex *;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return new(std::nothrow) std::mutex;}
inline int xSemaphoreTake(SemaphoreHandle_t handle,uint32_t ticks) {
 assert(!target_main_zero_wait || ticks==0);
 if(ticks==0)return handle->try_lock()?1:0;
 assert(ticks==portMAX_DELAY);handle->lock();return 1;
}
inline void xSemaphoreGive(SemaphoreHandle_t handle){handle->unlock();}
inline void vSemaphoreDelete(SemaphoreHandle_t handle){delete handle;}
