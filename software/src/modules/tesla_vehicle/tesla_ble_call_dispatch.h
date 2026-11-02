/* Production Tesla module (origin T1r13 lab fixture, core logic unchanged) - internal-stack NimBLE call dispatcher core.
 * SDK- and RTOS-free: the firmware instantiates it with FreeRTOS primitives and the real NimBLE adapter
 * (tesla_mem_fixture_radio_deny.cpp); the host tests instantiate THIS SAME code with std::thread primitives and a fake
 * SDK (tests/test_t1r13_dispatch.cpp). Nothing here decides which vehicle may be contacted: admission stays in the
 * unchanged tesla_real_lab::Gate; the dispatcher only moves an already-admitted call onto an internal stack.
 *
 * Ownership state machine (single std::atomic<State>, no FreeRTOS mutex, no priority-inheritance owner):
 *   Uninit -start-> Starting -> Ready | Uninit (semaphore/task failure, retry allowed) | Refused (placement, terminal)
 *   Ready -caller CAS-> Filling -> Submitted -helper CAS-> Executing -helper CAS-> Completed -caller-> Ready
 *   Filling -fill rejected-> Ready (job wiped)
 *   Submitted|Executing -caller deadline CAS-> Abandoned (caller returns Timeout = outcome UNKNOWN; helper owns job)
 *   Abandoned -helper-> Quarantined (terminal until reboot; late rc/cleanup recorded; job + kept copy wiped)
 *   Uninit|Ready -host stop/deinit-> HostTransition -> previous state
 * Job ownership: caller in Filling, helper in Submitted/Executing/Abandoned, caller again in Completed. The helper
 * wipes every copied byte BEFORE publishing Completed or Quarantined, and the caller wipes on a rejected fill, so
 * payload bytes never outlive a call on any completion path.
 * A call in flight or a quarantined dispatcher refuses host stop/deinit (no use-after-deinit, the lifecycle context and
 * callback cookie stay alive because the production teardown then fails closed). The helper task is never deleted. */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace tesla_ble_dispatch {

// Unchanged production deadlines (static_assert-tied in the firmware TU to TeslaBleOperation::TIMEOUT_MS):
constexpr uint32_t WORKER_WATCHDOG_MS = 10000; // worker ownership watchdog per GATT operation
constexpr uint32_t WORKER_RETIRE_MS = 7000;    // worker starts physical retirement (tesla_nimble_gatt.inc)
// Dispatcher deadline for one bounced submission. NimBLE blocks a submit at most for its HCI command timeout
// (2000 ms in upstream ble_hs_hci.c); 3000 ms leaves 1000 ms scheduling slack above that.
constexpr uint32_t DEADLINE_MS = 3000;
constexpr uint32_t WATCHDOG_MARGIN_MS = 7000;
static_assert(DEADLINE_MS + WATCHDOG_MARGIN_MS <= WORKER_WATCHDOG_MS, "dispatcher deadline must leave the explicit margin below the worker watchdog");
static_assert(DEADLINE_MS + 4000 <= WORKER_RETIRE_MS, "a timed-out submission must return well before worker retirement starts");
// Host stop/deinit waits at most for one bounded call to finish or be abandoned.
constexpr uint32_t HOST_WAIT_MS = DEADLINE_MS + 500;
constexpr uint32_t HOST_POLL_MS = 5;

enum class State : uint32_t { Uninit, Starting, Ready, Filling, Submitted, Executing, Completed, HostTransition, Abandoned, Quarantined, Refused };
enum class Outcome : uint8_t { Executed, Timeout, Busy, Unavailable, Quarantined, Rejected };
enum class StartResult : uint8_t { Ready, AlreadyReady, Quarantined, Busy, JobNotInternal, SemaphoreFailed, TaskCreateFailed, StackNotInternal };
struct Result { Outcome outcome; int rc; };
struct LateCleanup { int cancel_rc = -1, terminate_rc = -1; bool attempted = false; }; // raw SDK rcs, never interpreted as success

inline const char *state_name(State s)
{
    static const char *const n[] = {"uninit", "starting", "ready", "filling", "submitted", "executing", "completed", "host_transition", "abandoned", "quarantined", "refused"};
    const auto i = static_cast<uint32_t>(s);
    return i < sizeof(n) / sizeof(n[0]) ? n[i] : "?";
}
inline const char *start_name(StartResult r)
{
    static const char *const n[] = {"ready", "already_ready", "quarantined", "busy", "job_not_internal", "semaphore_failed", "task_create_failed", "stack_not_internal"};
    const auto i = static_cast<uint32_t>(r);
    return i < sizeof(n) / sizeof(n[0]) ? n[i] : "?";
}

inline void secure_wipe(void *p, size_t n)
{
    volatile uint8_t *b = static_cast<volatile uint8_t *>(p);
    for (size_t i = 0; i < n; ++i) { b[i] = 0; }
    std::atomic_signal_fence(std::memory_order_seq_cst);
}
inline bool all_zero(const void *p, size_t n)
{
    const volatile uint8_t *b = static_cast<const volatile uint8_t *>(p);
    uint8_t acc = 0;
    for (size_t i = 0; i < n; ++i) { acc |= b[i]; }
    return acc == 0;
}

struct Stats {
    State state = State::Uninit;
    StartResult last_start = StartResult::Ready;
    bool started_once = false, restart_required = false, admitting = false, helper_present = false;
    uint32_t calls = 0, completed = 0, timeouts = 0, abandoned_before_start = 0, late_completions = 0, late_success = 0,
             late_skipped = 0, rejected = 0, busy = 0, unavailable = 0, quarantine_refused = 0, wipes = 0, spurious = 0,
             start_failures = 0, task_create_failures = 0, stack_check_failures = 0, host_transitions = 0, host_refused = 0;
    int late_rc = 0;
    LateCleanup late_cleanup;
};

// Os: Sem, Task, FOREVER, sem_init/give/take(ms), create_helper(entry,arg,task), stack_internal(task),
//     object_internal(p,n), now_ms(), sleep_ms(ms), helper_exit_requested(), before_step().
// Exec: Job, Keep, execute(Job&)->int, keep(const Job&, int rc)->Keep, late_cleanup(const Keep&, int rc)->LateCleanup.
template <class Os, class Exec> class Dispatcher {
public:
    using Job = typename Exec::Job;
    using Keep = typename Exec::Keep;

    // main (single caller): create/verify the helper. Never admits a call before the helper stack placement is proven.
    StartResult start()
    {
        State s = State::Uninit;
        if (!state_.compare_exchange_strong(s, State::Starting, std::memory_order_acq_rel)) {
            switch (s) {
            case State::Abandoned: case State::Quarantined: return last_start_ = StartResult::Quarantined;
            case State::Refused: return last_start_; // terminal placement refusal, reason unchanged
            case State::Starting: case State::HostTransition: return StartResult::Busy;
            default: return StartResult::AlreadyReady;
            }
        }
        started_once_ = true;
        if (!Os::object_internal(&job_, sizeof(job_))) { return refuse(StartResult::JobNotInternal); }
        if (!sems_ready_) {
            if (!Os::sem_init(req_) || !Os::sem_init(done_)) { return fail(StartResult::SemaphoreFailed); }
            sems_ready_ = true; // static semaphores are never re-created once valid
        }
        if (!helper_created_.load(std::memory_order_acquire)) {
            if (!Os::create_helper(&Dispatcher::helper_main, this, helper_)) { task_create_failures_.fetch_add(1); return fail(StartResult::TaskCreateFailed); }
            helper_created_.store(true, std::memory_order_release);
        }
        // Actual placement of the created helper stack, read back from the RTOS (not the requested caps).
        if (!Os::stack_internal(helper_)) { stack_check_failures_.fetch_add(1); return refuse(StartResult::StackNotInternal); }
        state_.store(State::Ready, std::memory_order_release);
        return last_start_ = StartResult::Ready;
    }

    // Any task: fill copies every argument into the internal job; the caller's stack is never referenced afterwards.
    template <class Fill> Result run(Fill &&fill)
    {
        State s = State::Ready;
        if (!state_.compare_exchange_strong(s, State::Filling, std::memory_order_acq_rel)) {
            switch (s) {
            case State::Abandoned: case State::Quarantined: quarantine_refused_.fetch_add(1); return {Outcome::Quarantined, 0};
            case State::Uninit: case State::Starting: case State::Refused: unavailable_.fetch_add(1); return {Outcome::Unavailable, 0};
            default: busy_.fetch_add(1); return {Outcome::Busy, 0}; // never waits on another caller
            }
        }
        if (!fill(job_)) {
            wipe_job();
            rejected_.fetch_add(1);
            state_.store(State::Ready, std::memory_order_release);
            return {Outcome::Rejected, 0};
        }
        calls_.fetch_add(1);
        state_.store(State::Submitted, std::memory_order_release);
        Os::give(req_);
        const uint32_t t0 = Os::now_ms();
        for (;;) {
            s = state_.load(std::memory_order_acquire);
            if (s == State::Completed) { break; }
            const uint32_t elapsed = Os::now_ms() - t0;
            if (elapsed >= DEADLINE_MS) {
                // Outcome unknown: hand the job to the helper for good. Only Submitted/Executing/Completed are
                // reachable here; a failed CAS reloads s and re-evaluates.
                if ((s == State::Submitted || s == State::Executing) &&
                    state_.compare_exchange_strong(s, State::Abandoned, std::memory_order_acq_rel)) {
                    timeouts_.fetch_add(1);
                    if (s == State::Submitted) { abandoned_before_start_.fetch_add(1); }
                    return {Outcome::Timeout, 0};
                }
                continue;
            }
            Os::take(done_, DEADLINE_MS - elapsed); // wake-up hint only; state_ is the source of truth
        }
        const int rc = rc_;
        rc_ = 0;
        completed_.fetch_add(1);
        state_.store(State::Ready, std::memory_order_release);
        return {Outcome::Executed, rc};
    }

    // Helper task body (one request). Public for the RTOS entry trampoline only.
    void helper_step()
    {
        State s = State::Submitted;
        if (!state_.compare_exchange_strong(s, State::Executing, std::memory_order_acq_rel)) {
            if (s == State::Abandoned) {
                // Caller gave up before the SDK was entered: the call is provably NOT executed.
                late_skipped_.fetch_add(1);
                wipe_job();
                state_.store(State::Quarantined, std::memory_order_release);
            } else {
                spurious_.fetch_add(1);
            }
            return;
        }
        const int rc = Exec::execute(job_);
        Keep keep = Exec::keep(job_, rc); // helper-stack copy of what a late cleanup may need (internal stack)
        wipe_job();
        rc_ = rc;
        s = State::Executing;
        if (state_.compare_exchange_strong(s, State::Completed, std::memory_order_acq_rel)) {
            secure_wipe(&keep, sizeof(keep));
            Os::give(done_);
            return;
        }
        // Late completion (s == Abandoned): the helper is the sole, deterministic owner of the result and cleanup.
        rc_ = 0;
        late_completions_.fetch_add(1);
        late_rc_ = rc;
        if (rc == 0) { late_success_.fetch_add(1); }
        late_cleanup_ = Exec::late_cleanup(keep, rc);
        secure_wipe(&keep, sizeof(keep));
        wipe_count_.fetch_add(1);
        state_.store(State::Quarantined, std::memory_order_release);
    }

    // Lifecycle task (nimble_port_stop/deinit wrappers): true = the real teardown may run; caller MUST call host_end().
    bool host_begin()
    {
        const uint32_t t0 = Os::now_ms();
        for (;;) {
            State s = state_.load(std::memory_order_acquire);
            if (s == State::Refused) { host_prior_ = s; host_transitions_.fetch_add(1); return true; } // nothing was ever admitted
            if (s == State::Uninit || s == State::Ready) {
                if (state_.compare_exchange_strong(s, State::HostTransition, std::memory_order_acq_rel)) {
                    host_prior_ = s;
                    host_transitions_.fetch_add(1);
                    return true;
                }
                continue;
            }
            if (s == State::Abandoned || s == State::Quarantined || s == State::HostTransition || Os::now_ms() - t0 >= HOST_WAIT_MS) {
                host_refused_.fetch_add(1);
                return false;
            }
            Os::sleep_ms(HOST_POLL_MS); // Starting/Filling/Submitted/Executing/Completed: one bounded call in progress
        }
    }
    void host_end()
    {
        if (host_prior_ == State::Refused) { return; }
        state_.store(host_prior_, std::memory_order_release);
    }

    bool restart_required() const { const State s = state_.load(); return s == State::Abandoned || s == State::Quarantined; }
    bool admitting() const
    {
        const State s = state_.load();
        return s == State::Ready || s == State::Filling || s == State::Submitted || s == State::Executing || s == State::Completed;
    }
    State state() const { return state_.load(); }
    // Readback: no copied byte remains outside an active Filling/Submitted/Executing window.
    bool job_clear() const { return all_zero(&job_, sizeof(job_)); }
    const Job &job_for_placement() const { return job_; }
    typename Os::Task helper() const { return helper_created_.load(std::memory_order_acquire) ? helper_ : typename Os::Task{}; }

    Stats stats() const
    {
        Stats r;
        r.state = state_.load();
        r.last_start = last_start_;
        r.started_once = started_once_;
        r.restart_required = restart_required();
        r.admitting = admitting();
        r.helper_present = helper_created_.load();
        r.calls = calls_.load(); r.completed = completed_.load(); r.timeouts = timeouts_.load();
        r.abandoned_before_start = abandoned_before_start_.load(); r.late_completions = late_completions_.load();
        r.late_success = late_success_.load(); r.late_skipped = late_skipped_.load(); r.rejected = rejected_.load();
        r.busy = busy_.load(); r.unavailable = unavailable_.load(); r.quarantine_refused = quarantine_refused_.load();
        r.wipes = wipe_count_.load(); r.spurious = spurious_.load(); r.start_failures = start_failures_.load();
        r.task_create_failures = task_create_failures_.load(); r.stack_check_failures = stack_check_failures_.load();
        r.host_transitions = host_transitions_.load(); r.host_refused = host_refused_.load();
        if (r.state == State::Quarantined) { r.late_rc = late_rc_; r.late_cleanup = late_cleanup_; } // published by the Quarantined store
        return r;
    }

private:
    static void helper_main(void *p)
    {
        auto &d = *static_cast<Dispatcher *>(p);
        while (!Os::helper_exit_requested()) {
            if (Os::take(d.req_, Os::FOREVER)) {
                if (Os::helper_exit_requested()) { break; }
                Os::before_step();
                d.helper_step();
            }
        }
    }
    void wipe_job() { secure_wipe(&job_, sizeof(job_)); wipe_count_.fetch_add(1); }
    StartResult refuse(StartResult why) { state_.store(State::Refused, std::memory_order_release); return last_start_ = why; }
    StartResult fail(StartResult why) { start_failures_.fetch_add(1); state_.store(State::Uninit, std::memory_order_release); return last_start_ = why; }

    Job job_{};
    std::atomic<State> state_{State::Uninit};
    typename Os::Sem req_{}, done_{};
    typename Os::Task helper_{};
    std::atomic<bool> helper_created_{false};
    bool sems_ready_ = false, started_once_ = false; // main only
    StartResult last_start_ = StartResult::Ready;
    State host_prior_ = State::Uninit;               // lifecycle task only, inside HostTransition
    int rc_ = 0;                                     // published by Completed
    int late_rc_ = 0;                                // published by Quarantined
    LateCleanup late_cleanup_{};                     // published by Quarantined
    std::atomic<uint32_t> calls_{0}, completed_{0}, timeouts_{0}, abandoned_before_start_{0}, late_completions_{0}, late_success_{0},
        late_skipped_{0}, rejected_{0}, busy_{0}, unavailable_{0}, quarantine_refused_{0}, wipe_count_{0}, spurious_{0},
        start_failures_{0}, task_create_failures_{0}, stack_check_failures_{0}, host_transitions_{0}, host_refused_{0};
};

// ---------------- NimBLE adapter (the 5 bounced submit APIs), templated on the SDK binding ----------------
// Sdk: Addr, ConnParams, GapCb, AttrCb, SvcCb, Uuid, UuidAny, uuid_size(const Uuid*), connect/terminate/conn_cancel/
//      write_flat/disc_svc (the __real_ symbols in firmware), find_master_by_addr(const Addr&, uint16_t&), TERM_REASON.
enum class Call : uint8_t { None, Connect, Terminate, ConnCancel, WriteFlat, DiscSvc };
// Tesla's productive TX path uses 20-byte fragments; CCCD writes use 2 bytes.
// This is only the dispatcher's owned TX copy, not an RX/frame/ATT limit.
constexpr size_t DATA_MAX = 20;

template <class Sdk> struct NimbleExec {
    struct Job {
        Call call;
        uint8_t own_type, reason;
        bool has_params;
        typename Sdk::Addr peer;
        int32_t duration;
        typename Sdk::ConnParams params;
        typename Sdk::GapCb *gap_cb;
        typename Sdk::AttrCb *attr_cb;
        typename Sdk::SvcCb *svc_cb;
        void *arg;
        uint16_t conn, handle, len;
        typename Sdk::UuidAny uuid;
        uint8_t data[DATA_MAX];
    };
    struct Keep { Call call; int rc; typename Sdk::Addr peer; };

    static bool fill_connect(Job &j, uint8_t own, const typename Sdk::Addr *peer, int32_t duration,
                             const typename Sdk::ConnParams *params, typename Sdk::GapCb *cb, void *arg)
    {
        if (!peer) { return false; }
        j.call = Call::Connect; j.own_type = own; j.peer = *peer; j.duration = duration;
        j.has_params = params != nullptr; if (params) { j.params = *params; }
        j.gap_cb = cb; j.arg = arg; return true;
    }
    static bool fill_terminate(Job &j, uint16_t conn, uint8_t reason) { j.call = Call::Terminate; j.conn = conn; j.reason = reason; return true; }
    static bool fill_conn_cancel(Job &j) { j.call = Call::ConnCancel; return true; }
    static bool fill_write(Job &j, uint16_t conn, uint16_t handle, const void *data, uint16_t len, typename Sdk::AttrCb *cb, void *arg)
    {
        if (len > DATA_MAX || (len && !data)) { return false; }
        j.call = Call::WriteFlat; j.conn = conn; j.handle = handle; j.len = len;
        const uint8_t *src = static_cast<const uint8_t *>(data);
        for (uint16_t i = 0; i < len; ++i) { j.data[i] = src[i]; }
        j.attr_cb = cb; j.arg = arg; return true;
    }
    static bool fill_disc_svc(Job &j, uint16_t conn, const typename Sdk::Uuid *uuid, typename Sdk::SvcCb *cb, void *arg)
    {
        const size_t n = Sdk::uuid_size(uuid);
        if (!n || n > sizeof(j.uuid)) { return false; }
        j.call = Call::DiscSvc; j.conn = conn;
        const uint8_t *src = reinterpret_cast<const uint8_t *>(uuid);
        uint8_t *dst = reinterpret_cast<uint8_t *>(&j.uuid);
        for (size_t i = 0; i < n; ++i) { dst[i] = src[i]; }
        j.svc_cb = cb; j.arg = arg; return true;
    }

    static int execute(Job &j)
    {
        switch (j.call) {
        case Call::Connect: return Sdk::connect(j.own_type, &j.peer, j.duration, j.has_params ? &j.params : nullptr, j.gap_cb, j.arg);
        case Call::Terminate: return Sdk::terminate(j.conn, j.reason);
        case Call::ConnCancel: return Sdk::conn_cancel();
        case Call::WriteFlat: return Sdk::write_flat(j.conn, j.handle, j.data, j.len, j.attr_cb, j.arg);
        case Call::DiscSvc: return Sdk::disc_svc(j.conn, reinterpret_cast<const typename Sdk::Uuid *>(&j.uuid), j.svc_cb, j.arg);
        case Call::None: break;
        }
        return Sdk::EINVAL_RC;
    }
    static Keep keep(const Job &j, int rc)
    {
        Keep k{};
        k.call = j.call; k.rc = rc;
        if (j.call == Call::Connect && rc == 0) { k.peer = j.peer; }
        return k;
    }
    // Runs on the helper's internal stack after the caller abandoned. Only an accepted late CONNECT owns a procedure/link
    // the production central no longer tracks; retire exactly that one (cancel the pending procedure, else terminate the
    // master link to the same peer). Raw rcs are recorded; success is never assumed and quarantine is kept regardless.
    static LateCleanup late_cleanup(const Keep &k, int rc)
    {
        LateCleanup c;
        if (k.call != Call::Connect || rc != 0) { return c; }
        c.attempted = true;
        c.cancel_rc = Sdk::conn_cancel();
        if (c.cancel_rc != 0) {
            uint16_t handle = 0xffff;
            if (Sdk::find_master_by_addr(k.peer, handle) && handle != 0xffff) { c.terminate_rc = Sdk::terminate(handle, Sdk::TERM_REASON); }
        }
        return c;
    }
};

} // namespace tesla_ble_dispatch
