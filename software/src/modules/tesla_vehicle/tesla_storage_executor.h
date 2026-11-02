#pragma once
// Production (origin T1r8-T1r14 lab, behavior unchanged): bounded request/reply executor that runs every
// Tesla storage POSIX call issued from the PSRAM-stack "tesla_ble" worker on an INTERNAL-stack task.
//
// Contract (single submitter = worker; main owns create/stop/reap; main never waits on the executor):
//  * Each call is one heap Job. T1r11 (TESLA_PSRAM_PLACEMENT): the WHOLE Job is one explicit PSRAM block
//    (tesla_psram::alloc, ExecutorJob, no internal fallback). Its std::atomic state is RMW-safe in PSRAM only via
//    CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND libcalls (checked by tesla_psram.h, verified in the ELF). The executor
//    copies every path/data byte into a bounded bounce union on its INTERNAL stack BEFORE the POSIX call and back
//    after it, so no PSRAM byte is accessed while LittleFS/spi_flash may run with the cache disabled; the bounce is
//    zeroized after every call (also on error). Without the lab flag the Job stays internal as before. The Job owns copies
//    of every input (paths, write bytes) and every output (read bytes, stat fields): no pointer into any
//    caller stack crosses the queue, so a timed-out caller cannot be written through later.
//  * Ownership is decided by ONE compare-exchange on Job::state: executor Pending->Done hands the Job back to
//    the requester (executor never touches it again); requester Pending->Abandoned on timeout hands it to the
//    executor, which finishes the call, closes an orphan FD from an abandoned open and frees it. Exactly one
//    side frees; every free zeroizes the whole Job (read/write bytes may be key material).
//  * No task notifications/semaphores: the requester polls the state with a 1-tick delay, so no stale handle
//    can be signalled after either side is gone.
//  * Any failure is EIO (never ENOENT): callers report Error/Unknown, never Absent. A timeout additionally
//    latches storage quarantine (restart required) because the abandoned call may still mutate flash.
//  * Calls from internal stacks (main loop, lifecycle, executor itself) execute directly and never queue.
//    The cache-off backstop (fail closed + IDF assert) stays on the direct path.
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(TESLA_STORAGE_EXECUTOR) && TESLA_STORAGE_EXECUTOR
#if !(defined(TESLA_WORKER_PSRAM_STACK) && TESLA_WORKER_PSRAM_STACK)
#error "TESLA_STORAGE_EXECUTOR requires TESLA_WORKER_PSRAM_STACK"
#endif
#include <mbedtls/platform_util.h>
#ifndef TESLA_STORAGE_EXECUTOR_STACK
#define TESLA_STORAGE_EXECUTOR_STACK 5120
#endif
namespace tesla_storage_executor {
constexpr uint32_t STACK_BYTES = TESLA_STORAGE_EXECUTOR_STACK;
constexpr unsigned QUEUE_DEPTH = 4;
constexpr size_t CHUNK = 128, PATH = 128;
}
#if defined(TESLA_PSRAM_PLACEMENT) && TESLA_PSRAM_PLACEMENT
#define TESLA_EXECUTOR_JOB_PSRAM 1
#include "tesla_psram.h"
#else
#define TESLA_EXECUTOR_JOB_PSRAM 0
#endif
#ifdef TESLA_STORAGE_EXECUTOR_HOST_TEST
#include "executor_host_platform.h" // host threads; proves ownership/protocol, NOT ESP placement
#else
#include "tesla_worker_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_cpu.h"
namespace tesla_storage_executor { namespace plat {
using Task = TaskHandle_t; using Queue = QueueHandle_t;
constexpr uint32_t INTERNAL_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
inline bool on_psram_stack() { return tesla_worker_psram::on_psram_stack(); }
inline bool backstop_deny() { return tesla_worker_psram::deny_flash_from_psram_stack(); }
inline void *alloc_internal(size_t n) { return heap_caps_calloc(1, n, INTERNAL_CAPS); }
inline void free_internal(void *p) { heap_caps_free(p); }
inline bool is_internal(const void *p) { return p && esp_ptr_internal(p) && !esp_ptr_external_ram(p); }
inline bool is_external(const void *p) { return p && esp_ptr_external_ram(p); }
#if TESLA_EXECUTOR_JOB_PSRAM
// T1r11: queue storage + StaticQueue_t in PSRAM. Its xQueueLock portMUX uses esp_cpu_compare_and_set's external-RAM
// path (IDF cpu.c, interrupts off + internal CAS lock); spi_flash suspends the scheduler on both CPUs while the cache
// is off (cache_utils.c), so no tick/event-list processing touches the queue then. Task context only, no ISR use.
constexpr uint32_t QUEUE_CAPS = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
constexpr uint32_t QUEUE_CAPS = INTERNAL_CAPS;
#endif
inline Queue queue_create() { return xQueueCreateWithCaps(QUEUE_DEPTH, sizeof(void *), QUEUE_CAPS); }
inline void queue_delete(Queue q) { vQueueDeleteWithCaps(q); }
inline bool queue_send(Queue q, void *p) { return xQueueSend(q, &p, 0) == pdTRUE; }
inline bool queue_receive(Queue q, void **p, uint32_t ms) { return xQueueReceive(q, p, pdMS_TO_TICKS(ms)) == pdTRUE; }
inline unsigned queue_waiting(Queue q) { return uxQueueMessagesWaiting(q); }
// Same core as the worker (0), one priority above it: a queued call preempts the polling worker.
inline bool task_create(void (*fn)(void *), Task *h) {
    return xTaskCreatePinnedToCoreWithCaps(fn, "tesla_fs", STACK_BYTES, nullptr, 2, h, 0, INTERNAL_CAPS) == pdPASS; }
inline bool task_buffers(Task h, uintptr_t &stack, uintptr_t &tcb) {
    StackType_t *s = nullptr; StaticTask_t *t = nullptr;
    if (xTaskGetStaticBuffers(h, &s, &t) != pdTRUE) { return false; }
    stack = reinterpret_cast<uintptr_t>(s); tcb = reinterpret_cast<uintptr_t>(t); return true; }
inline bool task_parked(Task h) { return eTaskGetState(h) == eSuspended; }
inline void task_delete(Task h) { vTaskDeleteWithCaps(h); }
inline void park_self() { for (;;) { vTaskSuspend(nullptr); } }
inline void sleep_tick() { vTaskDelay(1); }
inline uint32_t now_ms() { return pdTICKS_TO_MS(xTaskGetTickCount()); }
inline uint32_t stack_hwm(Task h) { return uxTaskGetStackHighWaterMark(h); }
inline uintptr_t current_sp() { return reinterpret_cast<uintptr_t>(esp_cpu_get_sp()); }
inline uint32_t tcb_bytes() { return sizeof(StaticTask_t); }
inline uint32_t queue_bytes() { return sizeof(StaticQueue_t) + QUEUE_DEPTH * sizeof(void *); }
}}
#endif

namespace tesla_storage_executor {
enum class Op : uint8_t { Stat, Unlink, Rename, Open, Fstat, Read, Write, Fsync, Close };
enum State : uint8_t { Pending = 0, Done = 1, Abandoned = 2 };
enum Phase : uint8_t { Absent = 0, Running = 1, StopRequested = 2, Parked = 3 };
struct Job {
    Op op = Op::Stat;
    int fd = -1, flags = 0;
    mode_t mode = 0;
    size_t count = 0;
    long rc = -1;
    int err = 0;
    uint32_t st_mode = 0;
    int64_t st_size = 0;
    std::atomic<uint8_t> state{Pending};
    char path[PATH]{}, path2[PATH]{};
    uint8_t data[CHUNK]{};
};
struct Stats {
    std::atomic<uint32_t> submitted{0}, completed{0}, abandoned{0}, timeouts{0}, queue_full{0}, alloc_failures{0},
        refused{0}, orphan_fds_closed{0}, orphan_close_failures{0}, freed{0}, creates{0}, create_failures{0}, deletes{0},
        direct_calls{0}, routed_calls{0}, bounce_calls{0}, bounce_refused{0}, bounce_nonzero{0};
    std::atomic<bool> queue_external{false}, job_external_seen{false}, job_internal_seen{false};
    std::atomic<int32_t> live_jobs{0};
    std::atomic<uint32_t> stack_addr{0}, tcb_addr{0}, sp{0};
    std::atomic<bool> stack_internal{false}, tcb_internal{false}, sp_internal{false}, buffers_valid{false};
    std::atomic<uint8_t> phase{Absent};
};
inline Stats stats;
inline std::atomic<bool> quarantine{false};          // timeout/orphan-close failure: storage unusable until reboot
#if defined(TESLA_WORKER_TEST_HOOKS) && TESLA_WORKER_TEST_HOOKS
inline std::atomic<uint32_t> inject_alloc_failures{0}; // test hook (host/fixture only, not in production)
#endif
inline uint32_t timeout_ms = 5000;                     // per call, bounded
inline plat::Task handle{};                            // main-owned (start/reap)
inline plat::Queue queue{};                            // created before, deleted after the only submitter

inline void *job_memory() {
#if TESLA_EXECUTOR_JOB_PSRAM
    void *m = tesla_psram::alloc(sizeof(Job), tesla_psram::ExecutorJob); // PSRAM only, nullptr on failure (no fallback)
    if (m) { (plat::is_external(m) ? stats.job_external_seen : stats.job_internal_seen).store(true); }
    return m;
#else
    return plat::alloc_internal(sizeof(Job));
#endif
}
inline void free_job(Job *j) {
    j->~Job();
    mbedtls_platform_zeroize(j, sizeof(Job));
#if TESLA_EXECUTOR_JOB_PSRAM
    tesla_psram::release(j); // zeroizes exactly prefix+sizeof(Job) again, then heap_caps_free
#else
    plat::free_internal(j);
#endif
    stats.live_jobs.fetch_sub(1); stats.freed.fetch_add(1);
}
inline Job *make(Op op) {
#if defined(TESLA_WORKER_TEST_HOOKS) && TESLA_WORKER_TEST_HOOKS
    uint32_t pending = inject_alloc_failures.load();
    while (pending != 0 && !inject_alloc_failures.compare_exchange_weak(pending, pending - 1)) {}
#else
    constexpr uint32_t pending = 0;
#endif
    void *memory = pending != 0 ? nullptr : job_memory();
    if (!memory) { stats.alloc_failures.fetch_add(1); errno = EIO; return nullptr; }
    Job *j = new (memory) Job{};
    j->op = op; stats.live_jobs.fetch_add(1);
    return j;
}

#if TESLA_EXECUTOR_JOB_PSRAM
// Bounded bounce on the executor's INTERNAL stack (256 B, measured executor stack headroom T1r10: 3388 B).
union Bounce { struct { char a[PATH]; char b[PATH]; } p; uint8_t d[CHUNK]; };
static_assert(sizeof(Bounce) == 2 * PATH, "bounce is two paths; data shares the first path's bytes");
inline void execute(Job &j) {
    Bounce b;
    // Copy every scalar out of the PSRAM Job before any POSIX call; the call only sees internal-stack memory.
    const Op op = j.op; const int fd = j.fd, flags = j.flags; const mode_t mode = j.mode;
    const size_t count = j.count <= CHUNK ? j.count : CHUNK;
    if (!plat::is_internal(&b)) { // never expected (executor stack is internal); fail closed, no PSRAM-backed call
        stats.bounce_refused.fetch_add(1); j.err = EIO; j.rc = -1; return; }
    stats.bounce_calls.fetch_add(1);
    switch (op) {
    case Op::Rename: std::memcpy(b.p.b, j.path2, PATH); b.p.b[PATH - 1] = '\0'; [[fallthrough]];
    case Op::Stat: case Op::Unlink: case Op::Open: std::memcpy(b.p.a, j.path, PATH); b.p.a[PATH - 1] = '\0'; break;
    case Op::Write: std::memcpy(b.d, j.data, count); break;
    default: break;
    }
    errno = 0;
    long rc = -1;
    struct stat st{};
    switch (op) {
    case Op::Stat: rc = ::stat(b.p.a, &st); break;
    case Op::Fstat: rc = ::fstat(fd, &st); break;
    case Op::Unlink: rc = ::unlink(b.p.a); break;
    case Op::Rename: rc = ::rename(b.p.a, b.p.b); break;
    case Op::Open: rc = ::open(b.p.a, flags, mode); break;
    case Op::Read: rc = ::read(fd, b.d, count); break;
    case Op::Write: rc = ::write(fd, b.d, count); break;
    case Op::Fsync: rc = ::fsync(fd); break;
    case Op::Close: rc = ::close(fd); break;
    }
    const int err = rc < 0 ? errno : 0;
    if (op == Op::Read && rc > 0 && static_cast<size_t>(rc) <= count) { std::memcpy(j.data, b.d, static_cast<size_t>(rc)); }
    mbedtls_platform_zeroize(&b, sizeof(b)); // on success AND error: read/write bytes may be key material
#ifdef TESLA_STORAGE_EXECUTOR_HOST_TEST
    { const volatile uint8_t *v = reinterpret_cast<const volatile uint8_t *>(&b);
      for (size_t i = 0; i < sizeof(b); ++i) { if (v[i]) { stats.bounce_nonzero.fetch_add(1); break; } } }
#endif
    j.err = err;
    j.rc = rc;
    if (rc == 0 && (op == Op::Stat || op == Op::Fstat)) { j.st_mode = st.st_mode; j.st_size = st.st_size; }
}
#else
inline void execute(Job &j) {
    errno = 0;
    long rc = -1;
    struct stat st{};
    switch (j.op) {
    case Op::Stat: rc = ::stat(j.path, &st); break;
    case Op::Fstat: rc = ::fstat(j.fd, &st); break;
    case Op::Unlink: rc = ::unlink(j.path); break;
    case Op::Rename: rc = ::rename(j.path, j.path2); break;
    case Op::Open: rc = ::open(j.path, j.flags, j.mode); break;
    case Op::Read: rc = ::read(j.fd, j.data, j.count); break;
    case Op::Write: rc = ::write(j.fd, j.data, j.count); break;
    case Op::Fsync: rc = ::fsync(j.fd); break;
    case Op::Close: rc = ::close(j.fd); break;
    }
    j.err = rc < 0 ? errno : 0;
    j.rc = rc;
    if (rc == 0 && (j.op == Op::Stat || j.op == Op::Fstat)) { j.st_mode = st.st_mode; j.st_size = st.st_size; }
}
#endif

inline void task_entry(void *) {
    const uintptr_t sp = plat::current_sp();
    stats.sp.store(static_cast<uint32_t>(sp));
    stats.sp_internal.store(plat::is_internal(reinterpret_cast<const void *>(sp)));
    for (;;) {
        void *item = nullptr;
        if (!plat::queue_receive(queue, &item, 100)) {
            if (stats.phase.load() == StopRequested && plat::queue_waiting(queue) == 0) { break; }
            continue;
        }
        if (!item) { break; } // stop sentinel: FIFO, so every earlier (abandoned) call already finished
        Job *j = static_cast<Job *>(item);
        execute(*j);
        stats.completed.fetch_add(1);
        uint8_t expected = Pending;
        if (j->state.compare_exchange_strong(expected, Done, std::memory_order_acq_rel)) { continue; } // requester owns
        // Abandoned: requester gave up; executor owns the finished call.
        if (j->op == Op::Open && j->rc >= 0) {
            if (::close(static_cast<int>(j->rc)) == 0) { stats.orphan_fds_closed.fetch_add(1); }
            else { stats.orphan_close_failures.fetch_add(1); quarantine.store(true); }
        }
        free_job(j);
    }
    stats.phase.store(Parked, std::memory_order_release);
    plat::park_self();
}

// main: delete a parked executor. Never blocks; returns true when no executor remains.
inline bool reap() {
    const uint8_t phase = stats.phase.load(std::memory_order_acquire);
    if (phase == Absent) { return true; }
    if (phase != Parked || !plat::task_parked(handle)) { return false; }
    plat::task_delete(handle);
    plat::queue_delete(queue); // empty: executor parks only after the sentinel or an empty queue
    handle = {}; queue = {};
    stats.deletes.fetch_add(1);
    stats.phase.store(Absent, std::memory_order_release);
    return true;
}
// main: before the worker exists. Refuses while a previous executor is still retiring.
inline bool start() {
    if (!reap()) { stats.create_failures.fetch_add(1); return false; }
    queue = plat::queue_create();
    if (!queue) { stats.create_failures.fetch_add(1); return false; }
#if TESLA_EXECUTOR_JOB_PSRAM
    stats.queue_external.store(plat::is_external(queue)); // real handle address (StaticQueue_t)
#endif
    stats.phase.store(Running, std::memory_order_release);
    if (!plat::task_create(task_entry, &handle)) {
        stats.phase.store(Absent); plat::queue_delete(queue); queue = {}; handle = {};
        stats.create_failures.fetch_add(1); return false;
    }
    uintptr_t stack = 0, tcb = 0;
    const bool valid = plat::task_buffers(handle, stack, tcb);
    stats.stack_addr.store(static_cast<uint32_t>(stack)); stats.tcb_addr.store(static_cast<uint32_t>(tcb));
    stats.stack_internal.store(valid && plat::is_internal(reinterpret_cast<const void *>(stack)));
    stats.tcb_internal.store(valid && plat::is_internal(reinterpret_cast<const void *>(tcb)));
    stats.buffers_valid.store(valid);
    stats.creates.fetch_add(1);
    return true;
}
// main: only after the worker (sole submitter) was deleted. Non-blocking.
inline void request_stop() {
    if (stats.phase.load() != Running) { return; }
    stats.phase.store(StopRequested, std::memory_order_release);
    (void)plat::queue_send(queue, nullptr); // if full, the executor parks once the queue drains
}

// worker side: returns 0 when the Job came back (caller reads + frees), -1 when it is no longer the caller's.
inline int submit(Job *j) {
    if (stats.phase.load(std::memory_order_acquire) != Running || !queue) {
        stats.refused.fetch_add(1); free_job(j); errno = EIO; return -1; }
    j->state.store(Pending, std::memory_order_relaxed);
    if (!plat::queue_send(queue, j)) { stats.queue_full.fetch_add(1); free_job(j); errno = EIO; return -1; }
    stats.submitted.fetch_add(1);
    const uint32_t begin = plat::now_ms();
    while (j->state.load(std::memory_order_acquire) == Pending && plat::now_ms() - begin < timeout_ms) { plat::sleep_tick(); }
    uint8_t expected = Pending;
    if (j->state.compare_exchange_strong(expected, Abandoned, std::memory_order_acq_rel)) {
        stats.timeouts.fetch_add(1); stats.abandoned.fetch_add(1);
        quarantine.store(true, std::memory_order_release);
        errno = EIO; return -1;
    }
    return 0;
}
inline bool copy_path(char (&out)[PATH], const char *in) {
    if (!in) { return false; }
    const size_t n = strnlen(in, PATH);
    if (n >= PATH) { return false; }
    std::memcpy(out, in, n + 1); return true;
}
inline long finish(Job *j) { const long rc = j->rc; const int e = j->err; free_job(j); errno = e; return rc; }
} // namespace tesla_storage_executor

namespace tesla_storage_fs {
namespace ex = tesla_storage_executor;
inline bool routed() { const bool r = ex::plat::on_psram_stack(); (r ? ex::stats.routed_calls : ex::stats.direct_calls).fetch_add(1); return r; }
// Direct path keeps the T1r7 backstop: never start flash I/O on a PSRAM stack (unreachable by routing).
#define TESLA_FS_DIRECT_BACKSTOP(fail) do { if (ex::plat::backstop_deny()) { errno = EIO; return fail; } } while (0)
inline ex::Job *path_job(ex::Op op, const char *p) {
    ex::Job *j = ex::make(op);
    if (j && !ex::copy_path(j->path, p)) { ex::free_job(j); errno = ENAMETOOLONG; return nullptr; }
    return j;
}
inline int fs_stat(const char *p, struct stat *st) {
    if (!routed()) { TESLA_FS_DIRECT_BACKSTOP(-1); return ::stat(p, st); }
    ex::Job *j = path_job(ex::Op::Stat, p);
    if (!j || ex::submit(j) != 0) { return -1; }
    if (j->rc == 0) { std::memset(st, 0, sizeof(*st)); st->st_mode = j->st_mode; st->st_size = static_cast<off_t>(j->st_size); }
    return static_cast<int>(ex::finish(j));
}
inline int fs_fstat(int fd, struct stat *st) {
    if (!routed()) { TESLA_FS_DIRECT_BACKSTOP(-1); return ::fstat(fd, st); }
    ex::Job *j = ex::make(ex::Op::Fstat);
    if (!j) { return -1; }
    j->fd = fd;
    if (ex::submit(j) != 0) { return -1; }
    if (j->rc == 0) { std::memset(st, 0, sizeof(*st)); st->st_mode = j->st_mode; st->st_size = static_cast<off_t>(j->st_size); }
    return static_cast<int>(ex::finish(j));
}
inline int fs_unlink(const char *p) {
    if (!routed()) { TESLA_FS_DIRECT_BACKSTOP(-1); return ::unlink(p); }
    ex::Job *j = path_job(ex::Op::Unlink, p);
    return !j || ex::submit(j) != 0 ? -1 : static_cast<int>(ex::finish(j));
}
inline int fs_rename(const char *from, const char *to) {
    if (!routed()) { TESLA_FS_DIRECT_BACKSTOP(-1); return ::rename(from, to); }
    ex::Job *j = path_job(ex::Op::Rename, from);
    if (j && !ex::copy_path(j->path2, to)) { ex::free_job(j); errno = ENAMETOOLONG; return -1; }
    return !j || ex::submit(j) != 0 ? -1 : static_cast<int>(ex::finish(j));
}
inline int fs_open(const char *p, int flags, mode_t mode = 0) {
    if (!routed()) { TESLA_FS_DIRECT_BACKSTOP(-1); return ::open(p, flags, mode); }
    ex::Job *j = path_job(ex::Op::Open, p);
    if (!j) { return -1; }
    j->flags = flags; j->mode = mode;
    return ex::submit(j) != 0 ? -1 : static_cast<int>(ex::finish(j));
}
inline ssize_t fs_read(int fd, uint8_t *out, size_t count) {
    if (!routed()) { TESLA_FS_DIRECT_BACKSTOP(-1); return ::read(fd, out, count); }
    ex::Job *j = ex::make(ex::Op::Read);
    if (!j) { return -1; }
    j->fd = fd; j->count = count < ex::CHUNK ? count : ex::CHUNK;
    if (ex::submit(j) != 0) { return -1; }
    if (j->rc > 0 && static_cast<size_t>(j->rc) <= j->count) { std::memcpy(out, j->data, static_cast<size_t>(j->rc)); }
    else if (j->rc > 0) { j->rc = -1; j->err = EIO; }
    return ex::finish(j); // zeroizes the copied bytes
}
inline ssize_t fs_write(int fd, const uint8_t *in, size_t count) {
    if (!routed()) { TESLA_FS_DIRECT_BACKSTOP(-1); return ::write(fd, in, count); }
    ex::Job *j = ex::make(ex::Op::Write);
    if (!j) { return -1; }
    j->fd = fd; j->count = count < ex::CHUNK ? count : ex::CHUNK;
    std::memcpy(j->data, in, j->count);
    return ex::submit(j) != 0 ? -1 : ex::finish(j);
}
inline int fs_fsync(int fd) {
    if (!routed()) { TESLA_FS_DIRECT_BACKSTOP(-1); return ::fsync(fd); }
    ex::Job *j = ex::make(ex::Op::Fsync);
    if (!j) { return -1; }
    j->fd = fd;
    return ex::submit(j) != 0 ? -1 : static_cast<int>(ex::finish(j));
}
// A refused/timed-out close is reported as failure (caller latches quarantine); a timed-out close still runs.
inline int fs_close(int fd) {
    if (!routed()) { TESLA_FS_DIRECT_BACKSTOP(-1); return ::close(fd); }
    ex::Job *j = ex::make(ex::Op::Close);
    if (!j) { return -1; }
    j->fd = fd;
    return ex::submit(j) != 0 ? -1 : static_cast<int>(ex::finish(j));
}
#undef TESLA_FS_DIRECT_BACKSTOP
inline bool quarantined() { return ex::quarantine.load(std::memory_order_acquire); }
} // namespace tesla_storage_fs
#else
// Non-executor builds: plain POSIX, byte-for-byte the previous behaviour.
namespace tesla_storage_fs {
inline int fs_stat(const char *p, struct stat *st) { return ::stat(p, st); }
inline int fs_fstat(int fd, struct stat *st) { return ::fstat(fd, st); }
inline int fs_unlink(const char *p) { return ::unlink(p); }
inline int fs_rename(const char *a, const char *b) { return ::rename(a, b); }
inline int fs_open(const char *p, int flags, mode_t mode = 0) { return ::open(p, flags, mode); }
inline ssize_t fs_read(int fd, uint8_t *b, size_t n) { return ::read(fd, b, n); }
inline ssize_t fs_write(int fd, const uint8_t *b, size_t n) { return ::write(fd, b, n); }
inline int fs_fsync(int fd) { return ::fsync(fd); }
inline int fs_close(int fd) { return ::close(fd); }
inline bool quarantined() { return false; }
}
#endif
