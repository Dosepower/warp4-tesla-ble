#pragma once
#ifndef TESLA_BLE_MANUAL_WEB_BENCH
#define TESLA_BLE_MANUAL_WEB_BENCH 0
#endif
#ifndef TESLA_BLE_MANUAL_VEHICLE_UI
#define TESLA_BLE_MANUAL_VEHICLE_UI 0
#endif
#ifndef TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
#define TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC 0
#endif
#if TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC || TESLA_BLE_MANUAL_WEB_BENCH
#include <cstdint>
void r13_psram_snapshot(const char *stage, const char *phase, int rc = 0, bool rc_valid = false, bool teardown = false);
void r13_psram_checkpoint(const char *module, const char *hook, bool after, unsigned states, unsigned request);
void r13_psram_boot(bool complete);
void r13_psram_loop();
void r13_psram_worker_begin();
#if TESLA_BLE_MANUAL_WEB_BENCH
void r13_psram_worker_end();
#endif
bool r13_psram_ok();
#else
inline bool r13_psram_ok() { return true; }
#endif
