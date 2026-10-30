// SPDX-License-Identifier: Apache-2.0
#pragma once
// Coherent hand-over of one finished SoC operation (outcome + nonsecret
// diagnostics + event trace) from the worker task (single writer) to the main
// task (reader). Diagnostics only, never authority.
//
// Protocol: single-writer sequence lock over word-sized atomics.
//   writer: version odd (write begin) -> payload words -> version even (end)
//   reader: v1 = version (odd: write in progress, retry) -> payload words ->
//           v2 = version; accept only v1 == v2.
// The odd write-begin marker is required: a reader that only compares one
// publication counter before and after copying cannot detect a write that has
// begun but not yet advanced that counter. All accesses are sequentially
// consistent atomics: the reader's later version load is ordered after any
// payload load that observed a write started after the odd marker.
// The writer never waits. The reader never blocks or disables interrupts: it
// retries a bounded number of times and otherwise reports "not now" so the
// caller simply tries again on its next loop pass. No lock is held over BLE,
// I/O or callbacks. Only the worker may publish (see TeslaVehicle::set_poll_outcome).
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <tesla_clean/vehicle.hpp>

// Test-only interleaving points (empty in production builds).
#ifndef TESLA_POLL_SNAPSHOT_WRITE_HOOK
#define TESLA_POLL_SNAPSHOT_WRITE_HOOK() do {} while (0)
#endif
#ifndef TESLA_POLL_SNAPSHOT_READ_HOOK
#define TESLA_POLL_SNAPSHOT_READ_HOOK() do {} while (0)
#endif

struct TeslaPollSnapshot {
    // Completed publications of this runtime when copied (0: none yet).
    uint32_t sequence = 0;
    uint8_t outcome = 0;  // TeslaVehicle::PollOutcome
    bool automatic = false;
    // Classification of the same operation (TeslaPollFailure, stage + 1, ...).
    uint8_t failure = 0, stage = 0, flags = 0;
    uint32_t elapsed_ms = 0, fault = 0;
    uint32_t counts = 0;     // attempts | timeouts << 8 | frames << 16 | rejected << 24
    uint32_t trace_meta = 0; // trace_total | unrelated << 16 | late (7 bit) << 24 | overflow << 31
    uint8_t trace_head = 0;  // next ring slot of trace (tesla_clean::VehicleDiagnostics::trace_head)
    std::array<uint32_t, tesla_clean::vehicle_trace_capacity> trace{};
};

class TeslaPollSnapshotChannel {
public:
    static constexpr size_t header_words = 6;
    static constexpr size_t words = header_words + tesla_clean::vehicle_trace_capacity;
    static constexpr unsigned read_attempts = 4;

    // Worker task only (single writer). Never waits.
    void publish(const TeslaPollSnapshot &s)
    {
        const uint32_t v = version_.load();
        version_.store(v + 1); // odd: write begun
        data_[0].store(uint32_t{s.outcome} | uint32_t{s.automatic ? 1u : 0u} << 8 | uint32_t{s.failure} << 16 |
            uint32_t{s.stage} << 24);
        data_[1].store(uint32_t{s.flags} | uint32_t{s.trace_head} << 8);
        data_[2].store(s.elapsed_ms);
        data_[3].store(s.fault);
        data_[4].store(s.counts);
        data_[5].store(s.trace_meta);
        TESLA_POLL_SNAPSHOT_WRITE_HOOK();
        for (size_t i = 0; i < s.trace.size(); ++i) { data_[header_words + i].store(s.trace[i]); }
        version_.store(v + 2); // even: complete
    }

    // Completed publications (a publication still being written is not counted).
    uint32_t sequence() const { return version_.load() >> 1; }

    // Any task. Bounded, non-blocking. False: a publication was being written
    // throughout every attempt; retry on a later pass. out is complete on true.
    bool read(TeslaPollSnapshot &out) const
    {
        for (unsigned attempt = 0; attempt < read_attempts; ++attempt) {
            const uint32_t v1 = version_.load();
            if (v1 & 1u) { continue; }
            TESLA_POLL_SNAPSHOT_READ_HOOK();
            const uint32_t w0 = data_[0].load(), w1 = data_[1].load();
            out.elapsed_ms = data_[2].load();
            out.fault = data_[3].load();
            out.counts = data_[4].load();
            out.trace_meta = data_[5].load();
            for (size_t i = 0; i < out.trace.size(); ++i) { out.trace[i] = data_[header_words + i].load(); }
            if (version_.load() != v1) { continue; }
            out.sequence = v1 >> 1;
            out.outcome = static_cast<uint8_t>(w0);
            out.automatic = ((w0 >> 8) & 1u) != 0;
            out.failure = static_cast<uint8_t>(w0 >> 16);
            out.stage = static_cast<uint8_t>(w0 >> 24);
            out.flags = static_cast<uint8_t>(w1);
            out.trace_head = static_cast<uint8_t>(w1 >> 8);
            return true;
        }
        return false;
    }

private:
    std::atomic<uint32_t> version_{0};
    std::array<std::atomic<uint32_t>, words> data_{};
};
