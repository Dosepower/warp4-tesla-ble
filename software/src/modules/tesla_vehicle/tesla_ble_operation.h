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
// Retire the runtime after cancellation/timeout: IDF cannot correlate late
// register-for-notify events with a second registration on the same interface.
class TeslaBleOperation
{
public:
    static constexpr uint32_t TIMEOUT_MS = 10000;
    template <typename Clock, typename Submit, typename Pause, typename Cancel>
    bool run(uint8_t kind, uint16_t connection, uint16_t handle,
             Clock now, Submit submit, Pause pause, Cancel cancel)
    {
        if (retired() || tesla_ble_cancellation(cancel()) != TeslaBleCancellation::Proceed) { return false; }
        watch.begin(now());
        struct Finish { TeslaBleWorkWatch &watch; ~Finish() { watch.finish(); } } finish{watch};
        started.store(now());
        const auto key = encode(kind, connection, handle);
        state.store(key | PENDING, std::memory_order_release);
        if (!submit()) { state.store(0, std::memory_order_release); return false; }
        for (;;) {
            const auto cancellation = tesla_ble_cancellation(cancel());
            if (retired() || cancellation == TeslaBleCancellation::Cancel || expired(now())) {
                abandoned.store(true);
                state.store(0, std::memory_order_release);
                return false;
            }
            if (cancellation == TeslaBleCancellation::Defer) { pause(); continue; }
            auto result = state.load(std::memory_order_acquire);
            if ((result & PENDING) == 0) { return (result & SUCCESS) != 0; }
            pause();
        }
    }
    void complete(uint8_t kind, uint16_t connection, uint16_t handle, bool success)
    {
        auto expected = encode(kind, connection, handle) | PENDING;
        state.compare_exchange_strong(expected, encode(kind, connection, handle) | (success ? SUCCESS : 0),
                                       std::memory_order_release);
    }
    bool expired(uint32_t now) const
    {
        return (state.load(std::memory_order_acquire) & PENDING) != 0 && now - started.load() > TIMEOUT_MS;
    }
    bool retired() const { return abandoned.load(); }
    bool supervise(uint32_t now)
    {
        if (watch.expired(now, TIMEOUT_MS)) { abandoned.store(true); }
        return retired();
    }
private:
    static uint64_t encode(uint8_t kind, uint16_t connection, uint16_t handle)
    {
        return uint64_t{kind} | (uint64_t{connection} << 8) | (uint64_t{handle} << 24);
    }
    static constexpr uint64_t PENDING = uint64_t{1} << 62;
    static constexpr uint64_t SUCCESS = uint64_t{1} << 61;
    std::atomic<uint64_t> state{0};
    std::atomic<uint32_t> started{0};
    std::atomic<bool> abandoned{false};
    TeslaBleWorkWatch watch;
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
