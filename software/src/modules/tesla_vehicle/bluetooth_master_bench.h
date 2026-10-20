#pragma once
#include "bluetooth_support.h"
#if TESLA_BLE_R13_MASTER_BENCH
#include "bluetooth_lifecycle.h"
// Bench diagnostics are compiled into the shared direct-IDF owner only under
// the explicit guarded non-production macro. No second lifecycle implementation.
using BluetoothMasterBench = BluetoothLifecycle;
#endif
