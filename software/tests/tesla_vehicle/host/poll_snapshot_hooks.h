// SPDX-License-Identifier: Apache-2.0
#pragma once
// Test-only deterministic interleaving points for TeslaPollSnapshotChannel
// (src/modules/tesla_vehicle/tesla_poll_snapshot.h; empty in production). Each
// hook is one-shot: it is cleared before it runs, so the hook body may re-enter
// the writer or reader without recursing. Included before tesla_vehicle.cpp.
#include <functional>
inline std::function<void()> &host_snapshot_write_hook() { static std::function<void()> f; return f; }
inline std::function<void()> &host_snapshot_read_hook() { static std::function<void()> f; return f; }
inline void host_snapshot_fire(std::function<void()> &slot)
{
    if (!slot) { return; }
    std::function<void()> f;
    f.swap(slot);
    f();
}
// Writer: after the odd write-begin marker and the header words, before the trace words.
#define TESLA_POLL_SNAPSHOT_WRITE_HOOK() host_snapshot_fire(host_snapshot_write_hook())
// Reader: after an even version was observed, before the payload is copied.
#define TESLA_POLL_SNAPSHOT_READ_HOOK() host_snapshot_fire(host_snapshot_read_hook())
