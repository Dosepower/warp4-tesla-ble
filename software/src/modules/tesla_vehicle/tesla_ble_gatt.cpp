#ifndef TESLA_BLE_GATT_HOST_TEST
#include "tesla_vehicle.h"
#endif
#include <cstring>
#if WARP_BLUETOOTH_COMPILED && !TESLA_BLE_USE_NIMBLE
#include "tesla_ble_uuid.h"
#endif
#include "tesla_ble_iram_diagnostics.h"

std::atomic<TeslaVehicle *> TeslaVehicle::gatt_owner{nullptr};

#if TESLA_BLE_USE_NIMBLE
#include "tesla_nimble_gatt.inc"
#elif WARP_BLUETOOTH_COMPILED
static constexpr uint16_t TESLA_GATT_APP = 0x5742;

void TeslaVehicle::process_disconnect()
{
    Runtime *r = tesla->runtime_owner.get();
    const auto generation = r->pending_disconnect.take();
    if (generation && *generation == r->session.connection()) {
        // Includes local RX overflow: the link may still physically be open.
        // Retire physical ownership before callbacks or replacement readiness.
        disconnect_internal();
    }
}

bool TeslaVehicle::gatt_operation(GattOperation kind, uint16_t handle, tesla_warp::FunctionRef<bool()> submit)
{
    Runtime *r = tesla->runtime_owner.get();
    const auto identity = r->session.identity();
    if (!identity) { return false; } // No submission against unavailable identity.
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if (kind != Close && (!active_identity_matches(*identity) || !runtime_binding_matches())) { return false; }
#endif
    const auto generation = r->connection_generation.load();
    const auto connection = kind == Register || kind == Open ? 0 : r->gatt_connection.load();
    auto &slot = kind == Close ? r->retirement_operation : r->operation;
    const auto gated_submit = [this, identity, kind, &submit]() {
#if TESLA_BLE_MANUAL_VEHICLE_UI
        if (kind != Close && (!active_identity_matches(*identity) || !runtime_binding_matches())) { return false; }
#endif
        return submit();
    };
    const bool ok = slot.run(kind, connection, handle, []() { return millis(); }, gated_submit,
        [r, kind, connection]() {
            if (kind == Discover && r->discovery_ready.load()) { r->operation.complete(kind, connection, 0, true); }
            vTaskDelay(pdMS_TO_TICKS(10));
        }, [this, r, identity, generation, kind]() {
#if TESLA_BLE_MANUAL_VEHICLE_UI
            if (kind != Close && !active_identity_matches(*identity)) { return TeslaBleCancellation::Cancel; }
#endif
            if (r->stop_requested.load() || (kind != Close && r->connection_generation.load() != generation)) {
                return TeslaBleCancellation::Cancel;
            }
            const auto latest = r->session.identity();
            if (!latest) { return TeslaBleCancellation::Defer; }
            return latest != identity ? TeslaBleCancellation::Cancel : TeslaBleCancellation::Proceed;
        });
    if (slot.retired()) {
        post_event(EventType::Error, "BLE operation cancelled/timed out; restart required");
        // A cancelled WRITE must first retire its physical stream using a
        // separate, never-reused CLOSE slot. Other work is not admissible.
        if (kind != Write) { r->stop_requested.store(true); }
    }
    return ok;
}

bool TeslaVehicle::ensure_connected()
{
    Runtime *r = tesla->runtime_owner.get();
    process_disconnect(); // Consume current cleanup before any replacement readiness.
    if (r->stop_requested.load()) { return false; }
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if (!runtime_binding_matches()) { return false; }
#endif
    if (r->connected.load() && r->link_open.load()) { return true; }
    // Initial connection cleanup is not loss of an accepted pairing's link.
    disconnect_internal(false);
    if (r->stop_requested.load()) { return false; }
    const auto setup_generation = r->session.connection();
    if (r->gatt_if.load() == ESP_GATT_IF_NONE &&
        !gatt_operation(Register, 0, []() { return esp_ble_gattc_app_register(TESLA_GATT_APP) == ESP_OK; })) {
        return false;
    }
    bool opened = false;
    for (uint8_t type =
#if TESLA_BLE_MANUAL_VEHICLE_UI
         r->remote_address_type; type == r->remote_address_type &&
#else
         0; type <= 1 &&
#endif
         !opened && !r->stop_requested.load(); ++type) {
        r->discovery_ready.store(false);
        opened = gatt_operation(Open, 0, [r, type]() {
            return esp_ble_gattc_open(r->gatt_if.load(), r->remote_address,
                                     static_cast<esp_ble_addr_type_t>(type), true) == ESP_OK;
        });
    }
    if (!opened || r->stop_requested.load()) { return false; }
    if (!gatt_operation(Discover, 0, [r]() {
            if (r->discovery_ready.load()) {
                r->operation.complete(Discover, r->gatt_connection.load(), 0, true);
            }
            return true;
        })) { return false; }

    esp_gattc_service_elem_t service{};
    esp_gattc_char_elem_t write{}, notify{};
    esp_gattc_descr_elem_t descriptor{};
    auto service_uuid = tesla_ble_uuid(0x11);
    auto write_uuid = tesla_ble_uuid(0x12);
    auto notify_uuid = tesla_ble_uuid(0x13);
    esp_bt_uuid_t cccd{};
    cccd.len = ESP_UUID_LEN_16;
    cccd.uuid.uuid16 = 0x2902;
    // Local-cache queries avoid Arduino remote-object semaphore waits, but SDK
    // traversal/allocation and submission are supervised, not cancellable.
    if (!gatt_operation(Cache, 0, [&]() {
        uint16_t count = 1;
        auto interface = r->gatt_if.load();
        auto connection = r->gatt_connection.load();
        bool ok = esp_ble_gattc_get_service(interface, connection, &service_uuid, &service, &count, 0) == ESP_GATT_OK && count == 1;
        count = 1;
        ok = ok && esp_ble_gattc_get_char_by_uuid(interface, connection, service.start_handle, service.end_handle,
                                                write_uuid, &write, &count) == ESP_GATT_OK && count == 1;
        count = 1;
        ok = ok && esp_ble_gattc_get_char_by_uuid(interface, connection, service.start_handle, service.end_handle,
                                                notify_uuid, &notify, &count) == ESP_GATT_OK && count == 1;
        count = 1;
        ok = ok && (write.properties & ESP_GATT_CHAR_PROP_BIT_WRITE) &&
             (notify.properties & (ESP_GATT_CHAR_PROP_BIT_INDICATE | ESP_GATT_CHAR_PROP_BIT_NOTIFY)) &&
             esp_ble_gattc_get_descr_by_char_handle(interface, connection, notify.char_handle, cccd,
                                                    &descriptor, &count) == ESP_GATT_OK && count == 1;
        r->operation.complete(Cache, connection, 0, ok);
        return true;
    }) || r->stop_requested.load()) { return false; }
    if (!gatt_operation(Subscribe, notify.char_handle, [r, &notify]() {
            return esp_ble_gattc_register_for_notify(r->gatt_if.load(), r->remote_address, notify.char_handle) == ESP_OK;
        })) { return false; }
    // Tesla's official transport requests indications (CCCD 0x0002). Prefer
    // them whenever offered; notify-only peers need CCCD 0x0001. Both arrive
    // as ESP_GATTC_NOTIFY_EVT, including is_notify=false for indications.
    uint8_t enable[2] = {static_cast<uint8_t>((notify.properties & ESP_GATT_CHAR_PROP_BIT_INDICATE) ? 2 : 1), 0};
    if (r->stop_requested.load() || !gatt_operation(Descriptor, descriptor.handle, [r, &descriptor, &enable]() {
            return esp_ble_gattc_write_char_descr(r->gatt_if.load(), r->gatt_connection.load(), descriptor.handle,
                                                  2, enable, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE) == ESP_OK;
        }) || !r->link_open.load()) { return false; }
    if (!r->session.connect(setup_generation, [r, &write, &notify]() {
            r->write_handle = write.char_handle;
            r->notify_handle.store(notify.char_handle);
            r->connected.store(true);
        })) { return false; }
    r->physical_generation.store(r->connection_generation.load());
    if (!r->vehicle || !r->vehicle->open(r->physical_generation.load())) {
        disconnect_internal();
        return false;
    }
    if (r->stop_requested.load() || r->session.connection() != setup_generation || !r->connected.load()) {
        r->vehicle->close(r->vehicle->generation());
        return false;
    }
    post_event(EventType::Connected);
    return true;
}

void TeslaVehicle::disconnect_internal(bool cancel_pair)
{
    Runtime *r = tesla->runtime_owner.get();
    if (r == nullptr) { return; }
    r->command_outcome.begin_worker_cleanup();
    const bool interrupted_poll = r->in_flight_started_ms != 0;
    // Invalidate approval/submission before close callbacks, but expose terminal
    // cancellation only after all old stream cleanup has finished.
    const auto cancelled = cancel_pair ? r->command_outcome.retire_pair() : 0;
    const uint64_t retired = r->vehicle ? r->vehicle->generation() : 0;
    r->session.disconnect();
    r->connected.store(false);
    r->notify_handle.store(0);
    r->write_handle = 0;
    r->advance_connection();
    r->policy.on_transport_disconnected();
    r->in_flight_started_ms = 0;
    if (interrupted_poll) { r->manual_poll_pending.store(false); }
    xQueueReset(r->queues.rx());
    // At stop, host shutdown owns the physical link; do not wait for a callback
    // after admission has closed. Never submit any IDF work from the main loop.
    if (r->link_open.exchange(false) && !r->stop_requested.load()) {
        r->close_events.store(0);
        r->closing_link.store(true);
        if (!gatt_operation(Close, 0, [r]() {
                return esp_ble_gattc_close(r->gatt_if.load(), r->gatt_connection.load()) == ESP_OK;
            })) { r->stop_requested.store(true); }
        r->closing_link.store(false);
    }
    // Finish old physical-link cleanup before exposing completion reentry.
    // Otherwise an old unwind can exchange/close the newly connected link.
    r->pair_sending = false; r->pair_verifying = false; r->pair_status_active = false;
    r->pair_approval_waiting.store(false);
    if (r->operation.retired()) { r->stop_requested.store(true); }
    if (r->vehicle) { r->vehicle->close(retired); }
    if (cancelled) {
        cancel_pairing("Pairing link/session lost; transmission may have occurred; approval not authenticated", cancelled);
    }
    r->pending_disconnect.take(); // No old stream token may survive retirement.
    r->command_outcome.end_cleanup();
}

bool TeslaVehicle::transport_owns(uint64_t generation) const
{
    const Runtime *r = tesla->runtime_owner.get();
    if (!r || r->stop_requested.load() || !r->connected.load() || !r->link_open.load() ||
        generation != r->connection_generation.load() || generation != r->physical_generation.load()) { return false; }
    const auto identity = r->session.identity();
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if (!identity || !active_identity_matches(*identity)) { return false; }
#endif
    return identity && *identity == r->observed_identity;
}

bool TeslaVehicle::transport_recover_frame(uint64_t generation)
{
    // Never assert a peer frame reset on the same physical link.
    Runtime *r = tesla->runtime_owner.get();
    if (r && generation == r->physical_generation.load()) { disconnect_internal(); }
    return false;
}

bool TeslaVehicle::transport_write_bytes(const uint8_t *data, size_t size)
{
    Runtime *r = tesla->runtime_owner.get();
    if (!r || !r->connected.load() || !r->write_handle) { return false; }
    const auto generation = r->connection_generation.load();
    // Default ATT MTU 23: no optimistic MTU assignment from Arduino setMTU.
    return tesla_ble_write_fragments(size, 20, [r, generation]() {
        const auto identity = r->session.identity();
        return r->stop_requested.load() || r->command_outcome.forgetting() || !r->link_open.load() || !identity ||
               r->connection_generation.load() != generation ||
               *identity != r->observed_identity ||
               (r->in_flight_started_ms != 0 && !r->session.matches(r->in_flight));
    }, [this, r, &data](size_t offset, size_t length) {
        return gatt_operation(Write, r->write_handle, [r, &data, offset, length]() {
            return esp_ble_gattc_write_char(r->gatt_if.load(), r->gatt_connection.load(), r->write_handle,
                                            length, const_cast<uint8_t *>(data + offset),
                                            ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE) == ESP_OK;
        });
    });
}

void TeslaVehicle::gatt_callback(esp_gattc_cb_event_t event, esp_gatt_if_t interface, esp_ble_gattc_cb_param_t *param)
{
    auto *owner = gatt_owner.load();
    if (!owner || !param) { return; }
    auto lease = owner->tesla->callback_gate.enter();
    if (!lease) { return; }
    Runtime *r = owner->tesla->runtime_owner.get();
    if (!r || r->stop_requested.load()) { return; }
    if (event == ESP_GATTC_REG_EVT) {
        if (param->reg.app_id == TESLA_GATT_APP) {
            tesla_ble_iram_snapshot("gatt.app_register", "event", param->reg.status, true);
            if (param->reg.status == ESP_GATT_OK) { r->gatt_if.store(interface); }
            r->operation.complete(Register, 0, 0, param->reg.status == ESP_GATT_OK);
        }
        return;
    }
    if (interface != r->gatt_if.load()) { return; }
    const auto connection = r->gatt_connection.load();
    switch (event) {
    case ESP_GATTC_OPEN_EVT:
        if (memcmp(param->open.remote_bda, r->remote_address, 6) != 0) { return; }
        tesla_ble_iram_snapshot("gatt.open", "event", param->open.status, true);
        if (param->open.status == ESP_GATT_OK) {
            r->gatt_connection.store(param->open.conn_id);
            r->link_open.store(true);
        }
        r->operation.complete(Open, 0, 0, param->open.status == ESP_GATT_OK);
        break;
    case ESP_GATTC_DIS_SRVC_CMPL_EVT:
        if (param->dis_srvc_cmpl.conn_id == connection) {
            r->discovery_ready.store(param->dis_srvc_cmpl.status == ESP_GATT_OK);
            r->operation.complete(Discover, connection, 0, param->dis_srvc_cmpl.status == ESP_GATT_OK);
        }
        break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
        r->operation.complete(Subscribe, connection, param->reg_for_notify.handle, param->reg_for_notify.status == ESP_GATT_OK);
        break;
    case ESP_GATTC_WRITE_DESCR_EVT:
    case ESP_GATTC_WRITE_CHAR_EVT:
        r->operation.complete(event == ESP_GATTC_WRITE_DESCR_EVT ? Descriptor : Write,
                               param->write.conn_id, param->write.handle, param->write.status == ESP_GATT_OK);
        break;
    case ESP_GATTC_CLOSE_EVT:
        if (r->closing_link.load() && param->close.conn_id == connection) {
            const auto seen = r->close_events.fetch_or(1) | 1;
            if (param->close.status != ESP_GATT_OK || seen == 3) {
                r->retirement_operation.complete(Close, connection, 0, param->close.status == ESP_GATT_OK);
            }
        }
        break;
    case ESP_GATTC_DISCONNECT_EVT:
        if (param->disconnect.conn_id != connection) { return; }
        // Last virtual connection: pinned SDK promises CLOSE and DISCONNECT,
        // not their relative order. ACK both before permitting a successor.
        if (r->closing_link.load()) {
            if ((r->close_events.fetch_or(2) | 2) == 3) {
                r->retirement_operation.complete(Close, connection, 0, true);
            }
            return;
        }
        if (!r->link_open.load() && !r->connected.load()) { return; }
        r->command_outcome.begin_cleanup();
        {
        const auto disconnected_generation = r->session.disconnect();
        r->connected.store(false);
        r->link_open.store(false);
        r->notify_handle.store(0);
        r->advance_connection();
        r->pending_disconnect.store(disconnected_generation);
        owner->post_event(EventType::Disconnected);
        }
        break;
    case ESP_GATTC_SRVC_CHG_EVT:
        // Cached handles are no longer trustworthy; fail closed until reboot.
        r->session.disconnect();
        r->stop_requested.store(true);
        break;
    case ESP_GATTC_NOTIFY_EVT:
        if (!r->connected.load() || param->notify.conn_id != connection ||
            param->notify.handle != r->notify_handle.load() || memcmp(param->notify.remote_bda, r->remote_address, 6) != 0 ||
            !param->notify.value || !param->notify.value_len || param->notify.value_len > sizeof(RxFragment::data)) { return; }
        {
            RxFragment fragment;
            fragment.connection_generation = r->physical_generation.load();
            fragment.length = param->notify.value_len;
            memcpy(fragment.data, param->notify.value, fragment.length);
            if (xQueueSend(r->queues.rx(), &fragment, 0) != pdTRUE) {
                r->command_outcome.begin_cleanup();
                // Dropping any fragment corrupts framing; retire on the worker.
                r->connected.store(false);
                r->pending_disconnect.store(r->session.disconnect());
                r->advance_connection();
            }
        }
        break;
    default: break;
    }
}
#else
// A disabled SDK must link without any controller/host library references.
void TeslaVehicle::process_disconnect() {}
bool TeslaVehicle::gatt_operation(GattOperation, uint16_t, tesla_warp::FunctionRef<bool()>) { return false; }
bool TeslaVehicle::ensure_connected() { return false; }
void TeslaVehicle::disconnect_internal(bool) {}
bool TeslaVehicle::transport_owns(uint64_t) const { return false; }
bool TeslaVehicle::transport_recover_frame(uint64_t) { return false; }
bool TeslaVehicle::transport_write_bytes(const uint8_t *, size_t) { return false; }

#endif
