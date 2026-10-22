// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "FreeRTOS.h"
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    using namespace TargetMutexFixture; ++creates; if(fail_create) { return nullptr; }
    auto *p=new(std::nothrow) Semaphore; if(p) { ++live; } return p;
}
inline int xSemaphoreTake(SemaphoreHandle_t p,int delay) {
    using namespace TargetMutexFixture; assert(p); ++takes; if(fail_take) { return 0; }
    if(!delay) { if(!p->mutex.try_lock()) { return 0; } } else { p->mutex.lock(); }
    assert(!p->held); p->held=true; return pdTRUE;
}
inline void xSemaphoreGive(SemaphoreHandle_t p) {
    using namespace TargetMutexFixture; assert(p && p->held); ++gives; p->held=false; p->mutex.unlock();
}
inline void vSemaphoreDelete(SemaphoreHandle_t p) {
    using namespace TargetMutexFixture; assert(p && !p->held); ++deletes; --live; delete p;
}
