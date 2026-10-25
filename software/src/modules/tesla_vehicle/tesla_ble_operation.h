#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Single worker writer; main performs one bounded, lock-free observation.
// Includes time inside the IDF submission, not only completion waits.
class TeslaBleWorkWatch
{
public:
    __attribute__((noinline)) void begin(uint32_t now) { version.store(++writer_version); started.store(now); active.store(true); version.store(++writer_version); }
    __attribute__((noinline)) void finish() { version.store(++writer_version); active.store(false); version.store(++writer_version); }
    __attribute__((noinline)) bool expired(uint32_t now, uint32_t timeout) const
    {
        const auto before = version.load();
        const bool running = active.load();
        const auto since = started.load();
        // Timeouts and sampling skew must stay below half the uint32_t clock
        // range. A later begin() after main's clock sample is not an expiry.
        const uint32_t elapsed = now - since;
        return !(before & 1) && before == version.load() && running &&
               elapsed < (uint32_t{1} << 31) && elapsed > timeout;
    }
    uint32_t elapsed_ms(uint32_t now) const
    {
        const auto before = version.load();
        const auto since = started.load();
        const uint32_t elapsed = now - since;
        return !(before & 1) && before == version.load() && elapsed < (uint32_t{1} << 31) ? elapsed : 0;
    }
private:
    // See identity snapshot: final ELF verifies aligned word/byte loads/stores
    // without libatomic locks. Version has one worker writer, so no RMW needed.
    static_assert(alignof(std::atomic<uint32_t>) >= 4, "Aligned ESP32 word accesses required");
    uint32_t writer_version = 0;
    std::atomic<uint32_t> version{0}, started{0};
    std::atomic<bool> active{false};
};

// Serial GATT completion slot. Arm BEFORE submission (callbacks may be inline).
enum class TeslaBleCancellation { Proceed, Defer, Cancel };
inline TeslaBleCancellation tesla_ble_cancellation(bool cancel)
{
    return cancel ? TeslaBleCancellation::Cancel : TeslaBleCancellation::Proceed;
}
inline TeslaBleCancellation tesla_ble_cancellation(TeslaBleCancellation state) { return state; }
// Serial GATT slot. Correlation/SDK lifetime remains the adapter's responsibility.
// A generation-tagged CAS binds retirement to the observed operation, not a
// successor starting between main's observation and publication.
class TeslaBleOperation
{
public:
    static constexpr uint32_t TIMEOUT_MS = 10000;
    enum class Cause : uint8_t { None = 0, Deadline = 3, Cancelled = 4 };
    template <typename Clock, typename Submit, typename Pause, typename Cancel>
    bool run(uint8_t kind, uint16_t connection, uint16_t handle,
             Clock now, Submit submit, Pause pause, Cancel cancel)
    {
        if (retired() || tesla_ble_cancellation(cancel()) != TeslaBleCancellation::Proceed) { return false; }
        const uint64_t token = ((control.load() + GENERATION) & ~uint64_t{0xffff}) | (uint64_t{kind} << 8) | ACTIVE;
        started.store(now());
        control.store(token);
        struct Finish {
            TeslaBleOperation &owner; uint64_t token;
            ~Finish() { owner.control.compare_exchange_strong(token, token & ~STATUS); }
        } finish{*this, token};
        const auto key = encode(kind, connection, handle);
        state.store(key | PENDING, std::memory_order_release);
        if (!submit()) { state.store(0, std::memory_order_release); return false; }
        for (;;) {
            const auto cancellation = tesla_ble_cancellation(cancel());
            const auto time = now();
            if (cancellation == TeslaBleCancellation::Cancel) { retire(token, time, Cause::Cancelled); }
            supervise(time);
            if (retired()) { state.store(0, std::memory_order_release); return false; }
            if (cancellation == TeslaBleCancellation::Defer) { pause(); continue; }
            const auto result = state.load(std::memory_order_acquire);
            if ((result & PENDING) == 0) { return (result & SUCCESS) != 0; }
            pause();
        }
    }
    void complete(uint8_t kind, uint16_t connection, uint16_t handle, bool success)
    {
        auto expected = encode(kind, connection, handle) | PENDING;
        state.compare_exchange_strong(expected, encode(kind, connection, handle) | (success ? SUCCESS : 0), std::memory_order_release);
    }
    bool expired(uint32_t now) const
    {
        const auto token = control.load();
        const uint32_t elapsed = now - started.load();
        return (token & STATUS) == ACTIVE && token == control.load() &&
            elapsed < (uint32_t{1} << 31) && elapsed > TIMEOUT_MS;
    }
    bool retired() const { return (control.load() & STATUS) >= PUBLISHING; }
    bool supervise(uint32_t now)
    {
        const auto token = control.load();
        const uint32_t elapsed = now - started.load();
        if ((token & STATUS) == ACTIVE && elapsed < (uint32_t{1} << 31) && elapsed > TIMEOUT_MS) {
            retire(token, now, Cause::Deadline);
        }
        return (control.load() & STATUS) > PUBLISHING; // diagnostic publication complete
    }
    Cause cause() const {
        const auto status = control.load() & STATUS;
        return status > PUBLISHING ? static_cast<Cause>(status) : Cause::None;
    }
    uint32_t generation() const { return uint32_t(control.load() >> 16); }
    uint32_t kind() const { return uint32_t((control.load() >> 8) & 0xff); }
    uint32_t elapsed_ms(uint32_t now) const {
        if (cause() != Cause::None) { return fault_elapsed.load(); }
        const uint32_t elapsed = now - started.load();
        return elapsed < (uint32_t{1} << 31) ? elapsed : 0;
    }
private:
    void retire(uint64_t token, uint32_t now, Cause cause) {
        if ((token & STATUS) != ACTIVE) { return; }
        const uint32_t elapsed = now - started.load();
        const auto publishing = (token & ~STATUS) | PUBLISHING;
        if (!control.compare_exchange_strong(token, publishing)) { return; }
        // No new run is admitted after the CAS; start/kind/generation are frozen.
        fault_elapsed.store(elapsed < (uint32_t{1} << 31) ? elapsed : 0);
        control.store((publishing & ~STATUS) | uint64_t(cause));
    }
    static uint64_t encode(uint8_t kind, uint16_t connection, uint16_t handle) {
        return uint64_t{kind} | (uint64_t{connection} << 8) | (uint64_t{handle} << 24);
    }
    static constexpr uint64_t PENDING = uint64_t{1} << 62, SUCCESS = uint64_t{1} << 61;
    static constexpr uint64_t GENERATION = uint64_t{1} << 16, STATUS = 0xff, ACTIVE = 1, PUBLISHING = 2;
    std::atomic<uint64_t> state{0}, control{0};
    std::atomic<uint32_t> started{0}, fault_elapsed{0};
};

template <typename Cancel, typename Write>
bool tesla_ble_write_fragments(size_t size, size_t payload, Cancel cancel, Write write)
{
    if (size == 0 || payload == 0) { return false; }
    for (size_t offset = 0; offset < size; offset += payload) {
        if (cancel() || !write(offset, std::min(payload, size - offset))) { return false; }
    }
    return !cancel();
}
