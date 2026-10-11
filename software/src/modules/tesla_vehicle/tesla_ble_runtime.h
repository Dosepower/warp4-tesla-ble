#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <cstring>

template <typename Probe>
bool tesla_ble_initial_key_presence(bool enabled, Probe probe)
{
    return enabled && probe();
}

// Single worker publication immediately before ACK; immutable thereafter.
class TeslaBleShutdownResult
{
public:
    struct Outcome { bool key_present = false; bool key_known = true; char error[96] = {}; };
    void publish(bool key_present, const char *error, bool key_known = true)
    {
        outcome.key_present = key_present;
        outcome.key_known = key_known;
        if (error) {
            std::strncpy(outcome.error, error, sizeof(outcome.error) - 1);
            outcome.error[sizeof(outcome.error) - 1] = '\0';
        }
        ready.store(true, std::memory_order_release);
    }
    const Outcome *read() const { return ready.load(std::memory_order_acquire) ? &outcome : nullptr; }
private:
    Outcome outcome;
    std::atomic<bool> ready{false};
};

// Main only polls. Stack work is dispatched once outside the shared loop.
class TeslaBleTeardown
{
public:
    enum class Result : uint8_t { Pending, Succeeded, Failed };
    static constexpr uint32_t TIMEOUT_MS = 30000;
    void begin(uint32_t now) { started = now; }
    template <typename Launch>
    Result poll(uint32_t now, bool worker_stopped, bool callbacks_drained, Launch launch)
    {
        if (terminal) { return Result::Failed; }
        const auto result = completion.load(std::memory_order_acquire);
        if (result != Result::Pending) {
            terminal = result == Result::Failed;
            return result;
        }
        if (now - started > TIMEOUT_MS) { terminal = true; return Result::Failed; }
        if (!launched && worker_stopped && callbacks_drained) {
            launched = true;
            if (!launch()) { terminal = true; return Result::Failed; }
        }
        return Result::Pending;
    }
    void complete(bool success)
    {
        Result pending = Result::Pending;
        completion.compare_exchange_strong(pending, success ? Result::Succeeded : Result::Failed,
                                            std::memory_order_release);
    }
    bool failed() const { return terminal; }
    Result completed_result() const { return completion.load(std::memory_order_acquire); }
    bool succeeded() const { return !terminal && completion.load(std::memory_order_acquire) == Result::Succeeded; }
private:
    uint32_t started = 0;
    bool launched = false;
    bool terminal = false;
    std::atomic<Result> completion{Result::Pending};
};

// Permanent admission control: the closed bit and in-flight count share one
// atomic, so close cannot miss a callback between checking and incrementing.
class TeslaBleCallbackGate
{
public:
    class Lease
    {
    public:
        explicit Lease(TeslaBleCallbackGate *gate) : gate(gate) {}
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        ~Lease() { if (gate != nullptr) { gate->entries.fetch_sub(1, std::memory_order_release); } }
        explicit operator bool() const { return gate != nullptr; }
    private:
        TeslaBleCallbackGate *gate;
    };

    Lease enter()
    {
        uint32_t value = entries.load(std::memory_order_acquire);
        while ((value & CLOSED) == 0) {
            if (entries.compare_exchange_weak(value, value + 1, std::memory_order_acquire)) {
                return Lease(this);
            }
        }
        return Lease(nullptr);
    }
    // Main context only; reopen only after all old producers are quiescent.
    void open() { entries.store(0, std::memory_order_release); }
    void close() { entries.fetch_or(CLOSED, std::memory_order_acq_rel); }
    bool drained() const { return (entries.load(std::memory_order_acquire) & ~CLOSED) == 0; }

private:
    static constexpr uint32_t CLOSED = uint32_t{1} << 31;
    std::atomic<uint32_t> entries{CLOSED};
};

// Used by the firmware with FreeRTOS handles and by the host failure tests.
template <typename Handle, void (*Delete)(Handle)>
class TeslaBleQueues
{
public:
    TeslaBleQueues() = default;
    TeslaBleQueues(const TeslaBleQueues &) = delete;
    TeslaBleQueues &operator=(const TeslaBleQueues &) = delete;
    ~TeslaBleQueues() { reset(); }

    template <typename Create>
    bool create(Create allocate)
    {
        rx_handle = allocate();
        if (rx_handle != Handle{}) {
            event_handle = allocate();
        }
        if (rx_handle == Handle{} || event_handle == Handle{}) {
            reset();
            return false;
        }
        return true;
    }

    Handle rx() const { return rx_handle; }
    Handle events() const { return event_handle; }

private:
    void reset()
    {
        if (event_handle != Handle{}) { Delete(event_handle); }
        if (rx_handle != Handle{}) { Delete(rx_handle); }
        event_handle = Handle{};
        rx_handle = Handle{};
    }
    Handle rx_handle{};
    Handle event_handle{};
};

// Main-context owner. Factories return an RAII candidate; nothing is allocated
// when disabled. The worker never owns or destroys this permanent control state.
template <typename Runtime>
class TeslaBleRuntimeOwner
{
public:
    template <typename Allocate, typename Activate>
    bool start(bool enabled, Allocate allocate, Activate activate)
    {
        if (!enabled || runtime != nullptr || reboot_required) {
            return false;
        }
        runtime = allocate();
        ++runtime_epoch;
        if (runtime == nullptr || !activate(*runtime)) {
            runtime.reset();
            return false;
        }
        return true;
    }

    Runtime *get() const { return runtime.get(); }

    template <typename Signal>
    bool begin_stop(Signal signal)
    {
        if (runtime == nullptr || stopping) {
            return false;
        }
        stopping = true;
        signal(*runtime);
        return true;
    }

    template <typename Quiesce>
    bool finish_stop(bool worker_stopped, bool callbacks_drained, bool controller_used, Quiesce quiesce)
    {
        if (!stopping || !worker_stopped || !callbacks_drained || !quiesce(*runtime)) {
            return false;
        }
        reboot_required = reboot_required || controller_used;
        runtime.reset();
        stopping = false;
        return true;
    }

    bool is_stopping() const { return stopping; }
    bool restart_required() const { return reboot_required; }
    void require_restart() { reboot_required = true; } // Main-only terminal quarantine latch.
    uint64_t epoch() const { return runtime_epoch; }
    bool accepts_event(uint64_t epoch) const
    {
        return runtime != nullptr && !stopping && epoch == runtime_epoch;
    }

private:
    std::unique_ptr<Runtime> runtime;
    bool stopping = false;
    bool reboot_required = false;
    uint64_t runtime_epoch = 0;
};
