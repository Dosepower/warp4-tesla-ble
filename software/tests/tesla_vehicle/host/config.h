#pragma once
// SPDX-License-Identifier: Apache-2.0
// HOST TEST ONLY. Resolves `#include "config.h"` from production Tesla headers
// (tesla_config_string.h) to the host Config model in module_platform.h, which
// exposes the same public official members the helper uses. Never on a firmware
// include path (firmware builds include src/ only).
#ifndef TESLA_BLE_MODULE_HOST_TEST
#error "tests/tesla_vehicle/host/config.h is a host-test shim"
#endif
#include "module_platform.h"
