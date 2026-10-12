#pragma once

// Diagnostic-only, no runtime creation, heap allocation or peer identifiers.
// rc is meaningful only for SDK returns and asynchronous GATT event status.
#if defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
void tesla_ble_iram_snapshot(const char *stage, const char *phase, int rc = 0, bool rc_valid = false);
#else
inline void tesla_ble_iram_snapshot(const char *, const char *, int = 0, bool = false) {}
#endif
