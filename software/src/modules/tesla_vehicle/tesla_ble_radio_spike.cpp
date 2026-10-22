// Phase-A API-reachability lower-bound spike, NOT a production transport.
// There is no constructor, setup hook, route, task or allocation at boot.
// The dedicated raw-only profile roots this entry point to measure the real
// controller+host+central/GATT closure without implementing Phase B prematurely.
#if defined(TESLA_BLE_RADIO_SPIKE_NIMBLE) && TESLA_BLE_RADIO_SPIKE_NIMBLE
#include <sdkconfig.h>
#if !CONFIG_BT_NIMBLE_ENABLED || CONFIG_BT_BLUEDROID_ENABLED
#error "The NimBLE spike requires a genuinely rebuilt NimBLE-only SDK"
#endif
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_uuid.h>
#include <esp_bt.h>

// Match the existing lazy lifecycle's Arduino policy. No host/controller init.
extern "C" bool bleInUse() { return true; }

// Opaque caller-owned callbacks/contexts: these are the actual IDF APIs, not
// mocks or Arduino wrappers. The linker root is never invoked by firmware.
// A viable measurement would still require the full generation-safe adapter,
// API wiring and teardown supervision before any production approval.
struct TeslaRadioSpikeRequest {
    unsigned operation;
    uint8_t own_address_type;
    ble_addr_t peer; // Full address, INCLUDING public/random type from scan.
    uint16_t connection, first, last;
    const ble_uuid_t *uuid;
    const void *bytes;
    uint16_t size;
    void *context;
    ble_gap_event_fn *gap;
    ble_gatt_disc_svc_fn *service;
    ble_gatt_chr_fn *characteristic;
    ble_gatt_dsc_fn *descriptor;
    ble_gatt_mtu_fn *mtu;
    ble_gatt_attr_fn *write;
};
static void radio_host(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}
extern "C" int tesla_radio_spike_dispatch(const TeslaRadioSpikeRequest *r) {
    if (!r) { return BLE_HS_EINVAL; }
    switch (r->operation) {
    case 0: { // Initialize only; no scan, connect, advertising or protocol work.
        const int result = nimble_port_init();
        if (result == 0) { nimble_port_freertos_init(radio_host); }
        return result;
    }
    case 1: return nimble_port_stop(); // Caller waits for host-task completion.
    case 2: return nimble_port_deinit(); // Separate from stop, never from callback.
    case 3: {
        ble_gap_disc_params parameters{};
        parameters.passive = 0; // Explicit ACTIVE scanning; receive scan responses.
        parameters.itvl = 0x50;
        parameters.window = 0x30;
        parameters.filter_duplicates = 0;
        return ble_gap_disc(r->own_address_type, 3000, &parameters, r->gap, r->context);
    }
    case 4: return ble_gap_disc_cancel();
    case 5: return ble_gap_connect(r->own_address_type, &r->peer, 5000, nullptr, r->gap, r->context);
    case 6: return ble_gap_conn_cancel();
    case 7: return ble_gap_terminate(r->connection, BLE_ERR_REM_USER_CONN_TERM);
    case 8: return ble_gattc_disc_svc_by_uuid(r->connection, r->uuid, r->service, r->context);
    case 9: return ble_gattc_disc_all_chrs(r->connection, r->first, r->last, r->characteristic, r->context);
    case 10: return ble_gattc_disc_all_dscs(r->connection, r->first, r->last, r->descriptor, r->context);
    case 11: return ble_gattc_exchange_mtu(r->connection, r->mtu, r->context);
    case 12: // With-response write for BOTH characteristic and CCCD handles.
        return ble_gattc_write_flat(r->connection, r->first, r->bytes, r->size, r->write, r->context);
    default: return BLE_HS_EINVAL;
    }
}
#elif defined(TESLA_BLE_RADIO_SPIKE_BLUEDROID) && TESLA_BLE_RADIO_SPIKE_BLUEDROID
#include <esp_bt.h>
#include <esp_bt_main.h>
// Existing production extension supplies the complete GATT references. Keep the
// actual lifecycle reachable even if the storage attestation rejects new bytes.
extern "C" int tesla_radio_spike_dispatch(unsigned operation) {
    switch (operation) {
    case 0: {
        esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        return esp_bt_controller_init(&config);
    }
    case 1: return esp_bt_controller_enable(ESP_BT_MODE_BLE);
    case 2: return esp_bluedroid_init();
    case 3: return esp_bluedroid_enable();
    case 4: return esp_bluedroid_disable();
    case 5: return esp_bluedroid_deinit();
    case 6: return esp_bt_controller_disable();
    case 7: return esp_bt_controller_deinit();
    default: return ESP_ERR_INVALID_ARG;
    }
}
#endif
