#pragma once
// LST1 lifecycle stack TRIAL (experimental, NOT a release value; no wallbox deployment approval).
// Single Tesla-local source of truth for the permanent NimBLE "bluetooth" lifecycle task stack. Used by the real
// xTaskCreatePinnedToCore() in bluetooth_nimble_lifecycle.inc and by the LSP observer (lifecycle_stack_probe.h), so
// measured HWM values are always reported against the size actually requested.
// Basis: LSP1 hardware run (4096 B stack) HWM min 2792 B => 1304 B observed touched over init + VIN scan + clean stop
// only. 3072 B keeps 1768 B above that observation; no chip connection/error/timeout path has been measured yet.
// Task lifetime, priority, core, TCB, NimBLE host stack and all other pools are unchanged.
#include <cstdint>
namespace bluetooth_lifecycle_stack {
constexpr uint32_t BYTES = 3072; // was 4096 (LSP1/MQR1 baseline)
constexpr uint32_t BASELINE_BYTES = 4096;
static_assert(BYTES >= 3072 && BYTES <= BASELINE_BYTES, "LST1 trial: never below the justified 3072 B floor");
} // namespace bluetooth_lifecycle_stack
