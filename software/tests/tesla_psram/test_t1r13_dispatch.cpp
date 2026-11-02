// T1r13EXT host test: the REAL dispatcher core + NimBLE adapter (tesla_ble_call_dispatch.h, the same header the
// firmware TU instantiates) driven with std::thread primitives and a scripted fake SDK. No radio/device/network.
#include "modules/tesla_mem_fixture/tesla_ble_call_dispatch.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace tesla_ble_dispatch;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---------------- host OS: virtual time = real time * SCALE ----------------
struct HostOs {
    struct Sem { std::mutex m; std::condition_variable cv; bool given = false; };
    using Task = int;
    static constexpr uint32_t FOREVER = UINT32_MAX;
    static constexpr uint32_t SCALE = 20;
    static inline std::atomic<bool> fail_sem{false}, fail_task{false}, external_stack{false}, external_job{false}, exiting{false};
    static inline std::atomic<int> hold_helper{0}, tasks_created{0};
    static inline std::mutex reg_m;
    static inline std::vector<Sem *> sems;
    static inline std::vector<std::thread> threads;
    static inline const auto t0 = std::chrono::steady_clock::now();

    static bool sem_init(Sem &s)
    {
        if (fail_sem) { return false; }
        { std::lock_guard<std::mutex> g(s.m); s.given = false; }
        std::lock_guard<std::mutex> g(reg_m); sems.push_back(&s); return true;
    }
    static void give(Sem &s) { { std::lock_guard<std::mutex> g(s.m); s.given = true; } s.cv.notify_all(); }
    static bool take(Sem &s, uint32_t ms)
    {
        std::unique_lock<std::mutex> l(s.m);
        auto pred = [&] { return s.given || exiting.load(); };
        if (ms == FOREVER) { s.cv.wait(l, pred); }
        else { s.cv.wait_for(l, std::chrono::microseconds(uint64_t(ms) * 1000 / SCALE), pred); }
        if (s.given) { s.given = false; return true; }
        return false;
    }
    static bool create_helper(void (*entry)(void *), void *arg, Task &t)
    {
        if (fail_task) { return false; }
        std::lock_guard<std::mutex> g(reg_m);
        threads.emplace_back(entry, arg);
        t = ++tasks_created;
        return true;
    }
    static bool stack_internal(Task t) { return t != 0 && !external_stack; }
    static bool object_internal(const void *p, size_t n) { return p && n && !external_job; }
    static uint32_t now_ms()
    {
        return uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count() * SCALE / 1000);
    }
    static void sleep_ms(uint32_t ms) { std::this_thread::sleep_for(std::chrono::microseconds(uint64_t(ms) * 1000 / SCALE)); }
    static bool helper_exit_requested() { return exiting.load(); }
    static void before_step() { while (hold_helper.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); } }
    static void shutdown()
    {
        exiting = true;
        { std::lock_guard<std::mutex> g(reg_m); for (auto *s : sems) { { std::lock_guard<std::mutex> l(s->m); } s->cv.notify_all(); } }
        for (auto &t : threads) { t.join(); }
    }
};

// ---------------- scripted fake NimBLE ----------------
struct FakeSdk {
    struct Addr { uint8_t type; uint8_t val[6]; };
    struct ConnParams { uint16_t itvl_min, itvl_max, latency, supervision; };
    using GapCb = int(void *, void *);
    using AttrCb = int(uint16_t, void *);
    using SvcCb = int(uint16_t, void *);
    struct Uuid { uint8_t type; };
    struct Uuid16 { Uuid u; uint16_t value; };
    struct Uuid128 { Uuid u; uint8_t value[16]; };
    union UuidAny { Uuid u; Uuid16 u16; Uuid128 u128; };
    static constexpr int EINVAL_RC = 3;
    static constexpr uint8_t TERM_REASON = 0x13;
    static size_t uuid_size(const Uuid *u) { return !u ? 0 : u->type == 16 ? sizeof(Uuid16) : u->type == 128 ? sizeof(Uuid128) : 0; }

    // script (set by the test thread before a call; read by the helper)
    static inline std::atomic<int> rc{0}, delay_ms{0}, cancel_rc{0}, terminate_rc{0};
    static inline std::atomic<bool> wait_release{false}, released{false}, find_master{false};
    // observations (written by the helper, read after a published state)
    static inline std::atomic<int> executed{0}, cancels{0}, terminates{0};
    static inline std::atomic<uint16_t> last_handle{0}, last_conn{0}, last_terminate_conn{0};
    static inline Addr last_peer{}, find_peer{};
    static inline uint8_t seen[DATA_MAX];
    static inline std::atomic<uint16_t> seen_len{0};
    static inline std::atomic<const void *> seen_ptr{nullptr};
    static inline std::atomic<void *> seen_arg{nullptr};

    static int body()
    {
        executed.fetch_add(1);
        if (delay_ms) { HostOs::sleep_ms(uint32_t(delay_ms.load())); }
        if (wait_release) { while (!released.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); } }
        return rc.load();
    }
    static int connect(uint8_t, const Addr *p, int32_t, const ConnParams *, GapCb *, void *a) { last_peer = *p; seen_arg = a; return body(); }
    static int terminate(uint16_t c, uint8_t) { if (executed.load() == 0 || !in_late) { last_conn = c; return body(); } terminates.fetch_add(1); last_terminate_conn = c; return terminate_rc.load(); }
    static int conn_cancel() { if (!in_late) { return body(); } cancels.fetch_add(1); return cancel_rc.load(); }
    static int write_flat(uint16_t c, uint16_t h, const void *d, uint16_t n, AttrCb *, void *a)
    {
        last_conn = c; last_handle = h; seen_ptr = d; seen_arg = a; seen_len = n; std::memcpy(seen, d, n); return body();
    }
    static int disc_svc(uint16_t c, const Uuid *u, SvcCb *, void *a) { last_conn = c; seen_ptr = u; seen_arg = a; return body(); }
    static bool find_master_by_addr(const Addr &a, uint16_t &h) { find_peer = a; if (!find_master) { return false; } h = 0x0042; return true; }
    static inline std::atomic<bool> in_late{false};
    static void reset()
    {
        rc = 0; delay_ms = 0; cancel_rc = 0; terminate_rc = 0; wait_release = false; released = false; find_master = false;
        executed = 0; cancels = 0; terminates = 0; in_late = false; seen_len = 0; seen_ptr = nullptr; seen_arg = nullptr;
        std::memset(seen, 0, sizeof(seen)); last_peer = {}; find_peer = {};
    }
};

// Late cleanup instrumented: same NimbleExec, but mark the helper as being in the late path so the fake can
// distinguish the cleanup conn_cancel/terminate from the original bounced call.
struct Exec : NimbleExec<FakeSdk> {
    static LateCleanup late_cleanup(const Keep &k, int rc)
    {
        FakeSdk::in_late = true;
        auto c = NimbleExec<FakeSdk>::late_cleanup(k, rc);
        FakeSdk::in_late = false;
        return c;
    }
};
using D = Dispatcher<HostOs, Exec>;
static std::vector<std::unique_ptr<D>> all;
static D &fresh() { HostOs::fail_sem = false; HostOs::fail_task = false; HostOs::external_stack = false; HostOs::external_job = false; HostOs::hold_helper = 0; FakeSdk::reset(); all.emplace_back(new D()); return *all.back(); }
static void wait_state(D &d, State s) { for (int i = 0; i < 5000 && d.state() != s; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); } }

static Result do_write(D &d, const uint8_t *p, uint16_t n, void *arg = reinterpret_cast<void *>(0x1234))
{
    return d.run([&](Exec::Job &j) { return Exec::fill_write(j, 7, 0x21, p, n, nullptr, arg); });
}
static Result do_connect(D &d, const FakeSdk::Addr &a)
{
    return d.run([&](Exec::Job &j) { return Exec::fill_connect(j, 0, &a, 7000, nullptr, nullptr, reinterpret_cast<void *>(0x5678)); });
}
static const FakeSdk::Addr PEER{0, {0x55, 0x44, 0x33, 0x22, 0x11, 0x02}}; // test constant (locally administered), not a vehicle

int main()
{
    static_assert(DEADLINE_MS + WATCHDOG_MARGIN_MS <= WORKER_WATCHDOG_MS, "margin");
    CHECK(DEADLINE_MS < WORKER_WATCHDOG_MS && WORKER_WATCHDOG_MS - DEADLINE_MS >= 7000);

    { // 1. job placement not internal -> terminal refusal, never admits, host teardown unaffected
        D &d = fresh(); HostOs::external_job = true;
        CHECK(d.start() == StartResult::JobNotInternal); CHECK(d.state() == State::Refused);
        uint8_t b[2] = {1, 0};
        CHECK(do_write(d, b, 2).outcome == Outcome::Unavailable); CHECK(FakeSdk::executed == 0);
        HostOs::external_job = false;
        CHECK(d.start() == StartResult::JobNotInternal); CHECK(!d.admitting()); CHECK(!d.restart_required());
        CHECK(d.host_begin()); d.host_end(); CHECK(d.state() == State::Refused); CHECK(d.job_clear());
    }
    { // 2. semaphore / task (stack allocation) failure -> Uninit, nothing admitted, retry succeeds
        D &d = fresh(); HostOs::fail_sem = true;
        CHECK(d.start() == StartResult::SemaphoreFailed); CHECK(d.state() == State::Uninit);
        HostOs::fail_sem = false; HostOs::fail_task = true;
        CHECK(d.start() == StartResult::TaskCreateFailed); CHECK(d.state() == State::Uninit);
        uint8_t b[2] = {1, 0};
        CHECK(do_write(d, b, 2).outcome == Outcome::Unavailable); CHECK(FakeSdk::executed == 0);
        CHECK(d.stats().task_create_failures == 1); CHECK(d.stats().start_failures == 2); CHECK(!d.stats().helper_present);
        CHECK(d.host_begin()); d.host_end(); CHECK(d.state() == State::Uninit);
        HostOs::fail_task = false;
        CHECK(d.start() == StartResult::Ready); CHECK(d.start() == StartResult::AlreadyReady); CHECK(d.stats().helper_present);
        CHECK(do_write(d, b, 2).outcome == Outcome::Executed);
    }
    { // 3. actual helper stack not internal -> terminal refusal before any call; helper retained, never deleted
        D &d = fresh(); HostOs::external_stack = true;
        CHECK(d.start() == StartResult::StackNotInternal); CHECK(d.state() == State::Refused);
        CHECK(d.stats().stack_check_failures == 1); CHECK(d.stats().helper_present);
        uint8_t b[2] = {1, 0};
        CHECK(do_write(d, b, 2).outcome == Outcome::Unavailable); CHECK(FakeSdk::executed == 0);
        CHECK(d.start() == StartResult::StackNotInternal);
    }
    { // 4. normal write: payload copied (not referenced), rc passthrough, all copied bytes wiped after completion
        D &d = fresh(); CHECK(d.start() == StartResult::Ready);
        uint8_t payload[20]; for (int i = 0; i < 20; ++i) { payload[i] = uint8_t(0xA0 + i); }
        FakeSdk::rc = 0;
        Result r = do_write(d, payload, 20);
        CHECK(r.outcome == Outcome::Executed && r.rc == 0);
        CHECK(FakeSdk::seen_len == 20 && std::memcmp(FakeSdk::seen, payload, 20) == 0);
        CHECK(FakeSdk::seen_ptr.load() != payload);            // helper used the internal copy
        CHECK(FakeSdk::seen_arg.load() == reinterpret_cast<void *>(0x1234));
        CHECK(d.job_clear()); CHECK(d.state() == State::Ready);
        FakeSdk::rc = 7;
        r = do_write(d, payload, 2); CHECK(r.outcome == Outcome::Executed && r.rc == 7); CHECK(d.job_clear());
        FakeSdk::rc = 0;
        FakeSdk::Uuid16 u{{16}, 0x1122};
        r = d.run([&](Exec::Job &j) { return Exec::fill_disc_svc(j, 9, &u.u, nullptr, nullptr); });
        CHECK(r.outcome == Outcome::Executed); CHECK(FakeSdk::seen_ptr.load() != &u.u); CHECK(d.job_clear());
        r = d.run([&](Exec::Job &j) { return Exec::fill_terminate(j, 0x33, 0x13); });
        CHECK(r.outcome == Outcome::Executed && FakeSdk::last_conn == 0x33); CHECK(d.job_clear());
        r = do_connect(d, PEER);
        CHECK(r.outcome == Outcome::Executed && std::memcmp(FakeSdk::last_peer.val, PEER.val, 6) == 0); CHECK(d.job_clear());
        CHECK(d.stats().calls == 5 && d.stats().completed == 5 && d.stats().timeouts == 0);
    }
    { // 5. delayed completion inside the deadline is a normal result
        D &d = fresh(); d.start();
        FakeSdk::delay_ms = DEADLINE_MS / 2; FakeSdk::rc = 0;
        const uint32_t t = HostOs::now_ms();
        uint8_t b[3] = {1, 2, 3};
        Result r = do_write(d, b, 3);
        CHECK(r.outcome == Outcome::Executed && r.rc == 0); CHECK(HostOs::now_ms() - t >= DEADLINE_MS / 2);
        CHECK(!d.restart_required()); CHECK(d.job_clear()); CHECK(d.state() == State::Ready);
    }
    { // 6. rejected fills wipe partial copies and release ownership
        D &d = fresh(); d.start();
        std::vector<uint8_t> big(DATA_MAX + 1, 0xEE);
        CHECK(do_write(d, big.data(), uint16_t(big.size())).outcome == Outcome::Rejected);
        CHECK(do_write(d, nullptr, 4).outcome == Outcome::Rejected);
        FakeSdk::Uuid bad{77};
        CHECK(d.run([&](Exec::Job &j) { return Exec::fill_disc_svc(j, 9, &bad, nullptr, nullptr); }).outcome == Outcome::Rejected);
        CHECK(d.run([&](Exec::Job &j) { j.data[0] = 0x5A; j.len = 1; return false; }).outcome == Outcome::Rejected);
        CHECK(d.job_clear()); CHECK(d.state() == State::Ready); CHECK(FakeSdk::executed == 0); CHECK(d.stats().rejected == 4);
    }
    { // 7. a second caller never blocks on the first (no mutex): Busy immediately
        D &d = fresh(); d.start();
        FakeSdk::delay_ms = DEADLINE_MS / 2;
        uint8_t b[2] = {1, 2};
        Result ra{};
        std::thread a([&] { ra = do_write(d, b, 2); });
        wait_state(d, State::Executing);
        const uint32_t t = HostOs::now_ms();
        Result rb = d.run([&](Exec::Job &j) { return Exec::fill_terminate(j, 1, 0x13); });
        CHECK(rb.outcome == Outcome::Busy); CHECK(HostOs::now_ms() - t < DEADLINE_MS / 4);
        a.join();
        CHECK(ra.outcome == Outcome::Executed); CHECK(d.job_clear()); CHECK(d.stats().busy == 1);
    }
    { // 8. timeout -> outcome unknown, quarantine, stop/deinit refused; late SUCCESS of connect -> helper retires it
        D &d = fresh(); d.start();
        FakeSdk::wait_release = true; FakeSdk::rc = 0; FakeSdk::cancel_rc = 0;
        const uint32_t t = HostOs::now_ms();
        Result r = do_connect(d, PEER);
        const uint32_t el = HostOs::now_ms() - t;
        CHECK(r.outcome == Outcome::Timeout); CHECK(el >= DEADLINE_MS && el < DEADLINE_MS + WATCHDOG_MARGIN_MS);
        CHECK(d.state() == State::Abandoned); CHECK(d.restart_required()); CHECK(!d.admitting());
        CHECK(d.stats().timeouts == 1 && d.stats().abandoned_before_start == 0);
        uint8_t b[2] = {1, 2};
        CHECK(do_write(d, b, 2).outcome == Outcome::Quarantined);
        CHECK(d.start() == StartResult::Quarantined);
        CHECK(!d.host_begin());                                 // timeout followed by stop: refused, nothing torn down
        CHECK(!d.host_begin());                                 // deinit equally refused
        CHECK(d.stats().host_refused == 2);
        FakeSdk::released = true;                               // late completion
        wait_state(d, State::Quarantined);
        const Stats s = d.stats();
        CHECK(s.state == State::Quarantined && s.late_completions == 1 && s.late_success == 1 && s.late_rc == 0);
        CHECK(s.late_cleanup.attempted && s.late_cleanup.cancel_rc == 0 && s.late_cleanup.terminate_rc == -1);
        CHECK(FakeSdk::cancels == 1 && FakeSdk::terminates == 0);
        CHECK(d.job_clear());                                   // peer/params wiped
        CHECK(!d.host_begin()); CHECK(d.restart_required());    // quarantine latched until reboot
        CHECK(do_connect(d, PEER).outcome == Outcome::Quarantined); CHECK(FakeSdk::executed == 1);
    }
    { // 9. late success where cancel no longer applies -> terminate exactly the master link to the same peer
        D &d = fresh(); d.start();
        FakeSdk::wait_release = true; FakeSdk::rc = 0; FakeSdk::cancel_rc = 14 /*EALREADY*/; FakeSdk::find_master = true; FakeSdk::terminate_rc = 0;
        CHECK(do_connect(d, PEER).outcome == Outcome::Timeout);
        FakeSdk::released = true; wait_state(d, State::Quarantined);
        const Stats s = d.stats();
        CHECK(s.late_cleanup.cancel_rc == 14 && s.late_cleanup.terminate_rc == 0);
        CHECK(FakeSdk::terminates == 1 && FakeSdk::last_terminate_conn == 0x0042);
        CHECK(std::memcmp(FakeSdk::find_peer.val, PEER.val, 6) == 0 && FakeSdk::find_peer.type == PEER.type);
        CHECK(d.job_clear()); CHECK(!d.host_begin());
    }
    { // 10. late FAILURE: recorded, no cleanup call, still quarantined (no fabricated success)
        D &d = fresh(); d.start();
        FakeSdk::wait_release = true; FakeSdk::rc = 5;
        uint8_t b[4] = {9, 9, 9, 9};
        CHECK(do_write(d, b, 4).outcome == Outcome::Timeout);
        FakeSdk::released = true; wait_state(d, State::Quarantined);
        const Stats s = d.stats();
        CHECK(s.late_rc == 5 && s.late_success == 0 && !s.late_cleanup.attempted && FakeSdk::cancels == 0 && FakeSdk::terminates == 0);
        CHECK(d.job_clear()); CHECK(d.restart_required());
    }
    { // 11. helper starved: abandoned before the SDK was entered -> call provably skipped, wiped, quarantined
        D &d = fresh(); d.start();
        HostOs::hold_helper = 1;
        uint8_t b[4] = {1, 2, 3, 4};
        CHECK(do_write(d, b, 4).outcome == Outcome::Timeout);
        CHECK(d.stats().abandoned_before_start == 1); CHECK(d.state() == State::Abandoned);
        HostOs::hold_helper = 0; wait_state(d, State::Quarantined);
        CHECK(FakeSdk::executed == 0); CHECK(d.stats().late_skipped == 1); CHECK(d.job_clear()); CHECK(!d.host_begin());
    }
    { // 12. stop during a short in-flight call waits for completion, then blocks new calls during the transition
        D &d = fresh(); d.start();
        FakeSdk::delay_ms = DEADLINE_MS / 3;
        uint8_t b[2] = {1, 2};
        Result ra{};
        std::thread a([&] { ra = do_write(d, b, 2); });
        wait_state(d, State::Executing);
        CHECK(d.host_begin()); CHECK(d.state() == State::HostTransition);
        a.join(); CHECK(ra.outcome == Outcome::Executed);
        FakeSdk::delay_ms = 0;
        CHECK(do_write(d, b, 2).outcome == Outcome::Busy);
        CHECK(!d.host_begin());                                 // no nested teardown
        d.host_end(); CHECK(d.state() == State::Ready);
        CHECK(do_write(d, b, 2).outcome == Outcome::Executed); CHECK(d.job_clear());
    }
    { // 13. stop before start; start refused during the transition
        D &d = fresh();
        CHECK(d.host_begin()); CHECK(d.start() == StartResult::Busy); d.host_end();
        CHECK(d.state() == State::Uninit); CHECK(d.start() == StartResult::Ready);
    }
    { // 14. stop racing a call that then times out -> refused once the call is abandoned (no teardown under it)
        D &d = fresh(); d.start();
        FakeSdk::wait_release = true;
        uint8_t b[2] = {1, 2};
        Result ra{};
        std::thread a([&] { ra = do_write(d, b, 2); });
        wait_state(d, State::Executing);
        const bool allowed = d.host_begin();
        a.join();
        CHECK(!allowed); CHECK(ra.outcome == Outcome::Timeout); CHECK(d.restart_required());
        FakeSdk::released = true; wait_state(d, State::Quarantined); CHECK(d.job_clear());
    }
    { // 15. stress (TSan target): two callers + a stop/deinit cycler; only Executed/Busy, counts consistent
        D &d = fresh(); d.start();
        std::atomic<uint32_t> exec{0}, busy{0}, other{0}, stops{0};
        std::atomic<bool> stop_cycling{false};
        auto caller = [&](uint8_t seed) {
            uint8_t b[20];
            for (int i = 0; i < 300; ++i) {
                for (int k = 0; k < 20; ++k) { b[k] = uint8_t(seed + i + k); }
                const Result r = do_write(d, b, uint16_t(1 + i % 20));
                if (r.outcome == Outcome::Executed) { exec++; } else if (r.outcome == Outcome::Busy) { busy++; } else { other++; }
            }
        };
        std::thread c([&] { while (!stop_cycling) { if (d.host_begin()) { stops++; std::this_thread::sleep_for(std::chrono::microseconds(50)); d.host_end(); } std::this_thread::yield(); } });
        std::thread a(caller, 1), b(caller, 100);
        a.join(); b.join(); stop_cycling = true; c.join();
        CHECK(other == 0); CHECK(exec + busy == 600); CHECK(d.stats().completed == exec.load()); CHECK(d.stats().calls == exec.load());
        CHECK(stops > 0); CHECK(d.state() == State::Ready); CHECK(d.job_clear()); CHECK(!d.restart_required());
    }
    HostOs::shutdown();
    all.clear();
    std::printf("t1r13_dispatch checks=%d fails=%d\n", checks, fails);
    return fails ? 1 : 0;
}
