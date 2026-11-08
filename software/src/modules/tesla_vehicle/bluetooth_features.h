#pragma once
// Existing target compatibility defaults; explicit variant profiles override.
#ifndef WARP_FEATURE_TESLA
#define WARP_FEATURE_TESLA 1
#endif
#ifndef WARP_FEATURE_REMOTE
#define WARP_FEATURE_REMOTE TESLA_BLE_USE_NIMBLE
#endif
#if (WARP_FEATURE_TESLA != 0 && WARP_FEATURE_TESLA != 1) || (WARP_FEATURE_REMOTE != 0 && WARP_FEATURE_REMOTE != 1)
#error "Bluetooth features must be 0 or 1"
#endif
#if !WARP_FEATURE_TESLA && !WARP_FEATURE_REMOTE
#error "Bluetooth profile requires at least one feature"
#endif
#if WARP_FEATURE_REMOTE && !TESLA_BLE_USE_NIMBLE
#error "Remote requires shared NimBLE"
#endif
