#pragma once
// HOST TEST PLATFORM ONLY (T1r8): std::thread/mutex model of the executor's FreeRTOS calls.
// Proves the request/reply ownership protocol, timeout/stop/reap behaviour and that every file call issued by a
// thread labelled "PSRAM stack" executes on the executor thread. It is NOT ESP placement evidence.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
namespace tesla_storage_executor { namespace plat {
struct HQueue { std::mutex m; std::condition_variable cv; std::deque<void *> q; unsigned cap = QUEUE_DEPTH; };
struct HTask { std::thread t; std::mutex m; std::condition_variable cv; bool parked = false, release = false; };
using Task = HTask *; using Queue = HQueue *;
inline thread_local bool tl_psram = false;      // test labels the worker thread
inline thread_local HTask *tl_task = nullptr;
inline std::atomic<int> live_allocs{0}, nonzero_frees{0}, backstop_denials{0}, queues_deleted_nonempty{0};
inline std::atomic<bool> fail_task_create{false};
inline bool on_psram_stack() { return tl_psram; }
inline bool backstop_deny() { if (tl_psram) { backstop_denials++; return true; } return false; }
inline void *alloc_internal(size_t n) {
    auto *p = static_cast<uint8_t *>(calloc(1, n + 16)); if (!p) { return nullptr; }
    std::memcpy(p, &n, sizeof(n)); live_allocs++; return p + 16; }
inline void free_internal(void *v) {
    auto *p = static_cast<uint8_t *>(v) - 16; size_t n; std::memcpy(&n, p, sizeof(n));
    for (size_t i = 0; i < n; ++i) { if (p[16 + i]) { nonzero_frees++; break; } } // must be zeroized
    live_allocs--; free(p); }
inline bool is_internal(const void *) { return true; } // host label only
inline Queue queue_create() { return new HQueue; }
inline void queue_delete(Queue q) { if (!q->q.empty()) { queues_deleted_nonempty++; } delete q; }
inline bool queue_send(Queue q, void *p) {
    std::lock_guard<std::mutex> l(q->m); if (q->q.size() >= q->cap) { return false; }
    q->q.push_back(p); q->cv.notify_all(); return true; }
inline bool queue_receive(Queue q, void **p, uint32_t ms) {
    std::unique_lock<std::mutex> l(q->m);
    if (!q->cv.wait_for(l, std::chrono::milliseconds(ms), [q] { return !q->q.empty(); })) { return false; }
    *p = q->q.front(); q->q.pop_front(); return true; }
inline unsigned queue_waiting(Queue q) { std::lock_guard<std::mutex> l(q->m); return static_cast<unsigned>(q->q.size()); }
inline bool task_create(void (*fn)(void *), Task *h) {
    if (fail_task_create.exchange(false)) { return false; }
    auto *t = new HTask; *h = t;
    t->t = std::thread([fn, t] { tl_task = t; tl_psram = false; fn(nullptr); });
    return true; }
inline bool task_buffers(Task, uintptr_t &s, uintptr_t &t) { s = 1; t = 2; return true; }
inline bool task_parked(Task h) { std::lock_guard<std::mutex> l(h->m); return h->parked; }
inline void task_delete(Task h) { { std::lock_guard<std::mutex> l(h->m); h->release = true; h->cv.notify_all(); } h->t.join(); delete h; }
inline void park_self() { HTask *me = tl_task; std::unique_lock<std::mutex> l(me->m); me->parked = true; me->cv.wait(l, [me] { return me->release; }); }
inline void sleep_tick() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
inline uint32_t now_ms() { return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
inline uint32_t stack_hwm(Task) { return 0; }
inline uintptr_t current_sp() { int local = 0; return reinterpret_cast<uintptr_t>(&local); }
inline uint32_t tcb_bytes() { return 0; }
inline uint32_t queue_bytes() { return 0; }
}}
