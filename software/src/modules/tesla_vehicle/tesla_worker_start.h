#pragma once
// Production worker start/stop for the PSRAM-stack Tesla worker (replaces the T1r8-T1r14 lab
// tesla_lab_profile_prep::start_worker/destroy_worker; lab profile preparation/keys are NOT ported).
// Order: internal-stack storage executor -> internal-stack NimBLE dispatcher -> PSRAM-stack worker.
#if defined(TESLA_WORKER_PSRAM_STACK) && TESLA_WORKER_PSRAM_STACK
#if !defined(TESLA_STORAGE_EXECUTOR) || !TESLA_STORAGE_EXECUTOR || !defined(TESLA_NIMBLE_DISPATCH) || !TESLA_NIMBLE_DISPATCH
#error "A PSRAM worker stack requires TESLA_STORAGE_EXECUTOR=1 and TESLA_NIMBLE_DISPATCH=1"
#endif
#include "tesla_worker_psram.h"
#include "tesla_storage_executor.h"
#include "tesla_nimble_dispatch.h"
namespace tesla_worker_start {
inline BaseType_t start(TaskFunction_t entry, void *context, TaskHandle_t *handle) {
    if (!tesla_storage_executor::start()) { *handle = nullptr; return pdFAIL; }
    if (!tesla_nimble_dispatch::start()) {
        tesla_storage_executor::request_stop(); (void)tesla_storage_executor::reap();
        *handle = nullptr; return pdFAIL;
    }
    const BaseType_t rc = tesla_worker_psram::create(entry, context, handle);
    if (rc != pdPASS) { tesla_storage_executor::request_stop(); (void)tesla_storage_executor::reap(); }
    return rc;
}
// main, after the production ACK barrier (worker parked). The dispatcher helper is never deleted.
inline void destroy(TaskHandle_t handle) {
    tesla_worker_psram::destroy(handle);
    tesla_storage_executor::request_stop();
    (void)tesla_storage_executor::reap();
}
} // namespace tesla_worker_start
#endif
