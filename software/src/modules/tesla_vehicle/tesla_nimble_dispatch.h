#pragma once
// Production Tesla-owned NimBLE submit routing (see tesla_nimble_dispatch.cpp). Submits are routed only by
// tesla_nimble_central.cpp; main (tesla_vehicle.cpp) and the lifecycle discovery wrapper only read the latch below.
// TESLA_NIMBLE_DISPATCH_HOST_BINDING is a host-test-only compile switch (never set by warp4_tesla.ini; rejected by
// tesla_production_build.py) that compiles this unchanged binding against tests/tesla_vehicle/host/dispatch_binding.
#if defined(TESLA_NIMBLE_DISPATCH) && TESLA_NIMBLE_DISPATCH && \
    (!defined(TESLA_BLE_MODULE_HOST_TEST) || defined(TESLA_NIMBLE_DISPATCH_HOST_BINDING))
#define TESLA_NIMBLE_DISPATCH_BOUND 1
#include <cstdint>
#include <esp_err.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
namespace tesla_nimble_dispatch {
bool start();            // idempotent; false = helper/job not internal or creation failed (worker start then fails)
bool restart_required(); // latched after an abandoned (timed-out) bounced call; consumed by TeslaVehicle::loop()
uint32_t refused_external_stack_calls();
int gap_connect(uint8_t, const ble_addr_t *, int32_t, const struct ble_gap_conn_params *, ble_gap_event_fn *, void *);
int gap_terminate(uint16_t, uint8_t);
int gap_conn_cancel();
int gattc_write_flat(uint16_t, uint16_t, const void *, uint16_t, ble_gatt_attr_fn *, void *);
int gattc_disc_svc_by_uuid(uint16_t, const ble_uuid_t *, ble_gatt_disc_svc_fn *, void *);
int gap_disc(uint8_t, int32_t, const struct ble_gap_disc_params *, ble_gap_event_fn *, void *);
int gap_disc_cancel();
int gattc_disc_all_chrs(uint16_t, uint16_t, uint16_t, ble_gatt_chr_fn *, void *);
int gattc_disc_all_dscs(uint16_t, uint16_t, uint16_t, ble_gatt_dsc_fn *, void *);
int port_stop();
esp_err_t port_deinit();
} // namespace tesla_nimble_dispatch
#endif

// Read-only admission latch: true once a bounced controller call timed out with unknown outcome (terminal for this
// boot). Always false where the dispatcher is not compiled (non-dispatch builds and the ordinary host suites).
inline bool tesla_nimble_dispatch_quarantined()
{
#if defined(TESLA_NIMBLE_DISPATCH_BOUND)
    return tesla_nimble_dispatch::restart_required();
#else
    return false;
#endif
}
