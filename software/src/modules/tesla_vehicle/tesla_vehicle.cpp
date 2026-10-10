#include "tesla_vehicle.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <BLEAddress.h>
#include <BLEDevice.h>
#include <BLERemoteService.h>

#include "event_log_prefix.h"
#include "generated/module_dependencies.h"
#include "tesla_ble_storage.h"
#include "tesla_ble_transport.h"
#include "tesla_ble_validation.h"
#include "tesla_ble_config_policy.h"

static constexpr const char *TESLA_SERVICE_UUID = "00000211-b2d1-43f0-9b88-960cebf8b91e";
static constexpr const char *TESLA_WRITE_UUID = "00000212-b2d1-43f0-9b88-960cebf8b91e";
static constexpr const char *TESLA_NOTIFY_UUID = "00000213-b2d1-43f0-9b88-960cebf8b91e";
static constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
static constexpr uint32_t RESPONSE_TIMEOUT_MS = 40000;

static std::vector<uint8_t> encode_vehicle_mac(uint64_t mac)
{
    std::vector<uint8_t> bytes(6);
    for (int i = 5; i >= 0; --i) {
        bytes[i] = static_cast<uint8_t>(mac & 0xFF);
        mac >>= 8;
    }
    return bytes;
}

static uint64_t decode_vehicle_mac(const std::vector<uint8_t> &bytes)
{
    if (bytes.size() != 6) {
        return 0;
    }
    uint64_t mac = 0;
    for (uint8_t byte : bytes) {
        mac = (mac << 8) | byte;
    }
    return mac;
}

TeslaVehicle *TeslaVehicle::instance = nullptr;

void TeslaVehicle::pre_setup()
{
    config = ConfigRoot{Config::Object({
        {"enable", Config::Bool(false)},
        {"vin", Config::Str("", 0, 17)},
        {"ble_address", Config::Str("", 0, 17)},
        {"ev_index", Config::Int(-1, -1, EV_MAX_EVS - 1)},
        {"poll_interval", Config::Uint(300, 30, 3600)},
        {"wake_once", Config::Bool(false)},
    }), [this](Config &update, ConfigSource source) -> String {
        String vin = update.get("vin")->asString();
        vin.toUpperCase();
        update.get("vin")->updateString(vin);

        String address = update.get("ble_address")->asString();
        address.toUpperCase();
        update.get("ble_address")->updateString(address);

        if (update.get("enable")->asBool()) {
            if (!tesla_ble_valid_vin(vin.c_str())) {
                return "VIN must contain exactly 17 valid characters";
            }
            if (!tesla_ble_valid_address(address.c_str())) {
                return "BLE address must use AA:BB:CC:DD:EE:FF format";
            }
            if (update.get("ev_index")->asInt() < 0) {
                return "A WARP EV profile must be selected";
            }
        }

        const bool changed = update.get("enable")->asBool() != config.get("enable")->asBool() ||
                             update.get("vin")->asString() != config.get("vin")->asString() ||
                             update.get("ble_address")->asString() != config.get("ble_address")->asString() ||
                             update.get("ev_index")->asInt() != config.get("ev_index")->asInt() ||
                             update.get("poll_interval")->asUint() != config.get("poll_interval")->asUint() ||
                             update.get("wake_once")->asBool() != config.get("wake_once")->asBool();
        const auto action = tesla_ble_config_update_action(source == ConfigSource::File,
                                                            task_handle != nullptr,
                                                            changed);
        if (action != TeslaBleConfigUpdateAction::None) {
            state.get("restart_required")->updateBool(true);
        }
        if (action == TeslaBleConfigUpdateAction::StopAndRestart) {
            stop_requested.store(true);
        }
        return "";
    }};

    state = Config::Object({
        {"running", Config::Bool(false)},
        {"connected", Config::Bool(false)},
        {"key_present", Config::Bool(false)},
        {"paired", Config::Bool(false)},
        {"pairing_pending", Config::Bool(false)},
        {"restart_required", Config::Bool(false)},
        {"last_soc", Config::Float(NAN)},
        {"last_update", Config::Uptime()},
        {"last_error", Config::Str("", 0, 96)},
    });

    empty_command = Config::Object({});
}

void TeslaVehicle::setup()
{
    if (!api.restorePersistentConfig("tesla_vehicle/config", &config) &&
        api.restorePersistentConfig("tesla_ble/config", &config)) {
        api.writeConfig("tesla_vehicle/config", &config);
        api.removeConfig("tesla_ble/config");
    }

    storage = std::make_shared<TeslaBleStorageAdapter>();
    pairing_policy.on_key_loaded(storage->has_private_key());
    std::vector<uint8_t> stored_vehicle_mac;
    if (storage->load("warp_ev_mac", stored_vehicle_mac)) {
        bound_vehicle_mac.store(decode_vehicle_mac(stored_vehicle_mac));
    }
    state.get("key_present")->updateBool(pairing_policy.key_present());
    state.get("paired")->updateBool(pairing_policy.paired());

    rx_queue = xQueueCreate(8, sizeof(RxFragment));
    event_queue = xQueueCreate(8, sizeof(ModuleEvent));
    if (rx_queue == nullptr || event_queue == nullptr) {
        state.get("last_error")->updateString("Failed to allocate BLE queues");
        initialized = true;
        return;
    }

    if (config.get("enable")->asBool()) {
        vin = config.get("vin")->asString().c_str();
        ble_address = config.get("ble_address")->asString().c_str();
        configured_ev_index = config.get("ev_index")->asInt();
        poll_interval_seconds = config.get("poll_interval")->asUint();
        wake_once = config.get("wake_once")->asBool();
        start_task();
    }

    initialized = true;
}

void TeslaVehicle::register_urls()
{
    api.addPersistentConfig("tesla_vehicle/config", &config, {}, {"vin", "ble_address"});
    api.addState("tesla_vehicle/state", &state);

    api.addCommand("tesla_vehicle/pair", &empty_command, {}, [this](Language, String &errmsg) {
        if (!config.get("enable")->asBool() || task_handle == nullptr) {
            errmsg = "Tesla BLE is not enabled";
            return;
        }
        pair_requested.store(true);
        state.get("pairing_pending")->updateBool(true);
    }, true);

    api.addCommand("tesla_vehicle/poll", &empty_command, {}, [this](Language, String &errmsg) {
        if (!config.get("enable")->asBool() || task_handle == nullptr) {
            errmsg = "Tesla BLE is not enabled";
            return;
        }
        poll_requested.store(true);
    }, true);

    api.addCommand("tesla_vehicle/forget_key", &empty_command, {}, [this](Language, String &errmsg) {
        if (task_handle == nullptr) {
            bool ok = storage->remove("private_key") &&
                      storage->remove("session_vcsec") &&
                      storage->remove("session_infotainment") &&
                      storage->remove("warp_ev_mac");
            if (!ok) {
                errmsg = "Could not remove Tesla key material";
                return;
            }
            state.get("paired")->updateBool(false);
            state.get("key_present")->updateBool(false);
            return;
        }
        forget_requested.store(true);
    }, true);
}

void TeslaVehicle::start_task()
{
    instance = this;
    BaseType_t result = xTaskCreatePinnedToCore(task_entry, "tesla_ble", 12288, this, 1, &task_handle, 0);
    if (result != pdPASS) {
        task_handle = nullptr;
        state.get("last_error")->updateString("Failed to start Tesla BLE task");
        return;
    }
    state.get("running")->updateBool(true);
}

void TeslaVehicle::task_entry(void *context)
{
    static_cast<TeslaVehicle *>(context)->task_loop();
}

void TeslaVehicle::task_loop()
{
    BLEDevice::init("WARP4");
    BLEDevice::setMTU(517);

    transport = std::make_shared<TeslaBleTransportAdapter>(this);
    vehicle = std::make_unique<TeslaBLE::Vehicle>(transport, storage);
    vehicle->set_vin(vin);
    vehicle->set_charge_state_callback([this](const CarServer_ChargeState &charge_state) {
        if (!charge_state.which_optional_battery_level || in_flight_started_ms == 0) {
            return;
        }
        const float soc = static_cast<float>(charge_state.optional_battery_level.battery_level);
        if (policy.accept_result(in_flight_generation, in_flight_ev_index, soc)) {
            post_event(EventType::Soc, nullptr, soc, in_flight_generation, in_flight_ev_index);
        }
        in_flight_started_ms = 0;
    });

    while (!stop_requested.load()) {
        if (transport_disconnected.exchange(false)) {
            policy.on_transport_disconnected();
            in_flight_started_ms = 0;
            xQueueReset(rx_queue);
        }
        update_active_vehicle();
        process_rx();
        process_commands();
        vehicle->loop();

        if (in_flight_started_ms != 0 && millis() - in_flight_started_ms > RESPONSE_TIMEOUT_MS) {
            in_flight_started_ms = 0;
            post_event(EventType::Error, "Tesla SoC request timed out");
        }

        const bool matching_vehicle = active_ev_index.load() == configured_ev_index;
        if (matching_vehicle && static_cast<int32_t>(millis() - next_poll_ms) >= 0 && in_flight_started_ms == 0) {
            request_soc(false);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    disconnect_internal();
    BLEDevice::deinit(false);
    post_event(EventType::Stopped);
    task_handle = nullptr;
    vTaskDelete(nullptr);
}

void TeslaVehicle::update_active_vehicle()
{
    const uint32_t generation = active_generation.load();
    const int index = active_ev_index.load();
    if (generation == observed_generation && index == observed_ev_index) {
        return;
    }

    observed_generation = generation;
    observed_ev_index = index;
    policy.on_vehicle_changed(generation, index);
    in_flight_started_ms = 0;
    next_poll_ms = millis();

    if (index != configured_ev_index) {
        disconnect_internal();
    }
}

void TeslaVehicle::process_commands()
{
    if (forget_requested.exchange(false)) {
        disconnect_internal();
        vehicle.reset();
        const bool ok = storage->remove("private_key") &&
                        storage->remove("session_vcsec") &&
                        storage->remove("session_infotainment") &&
                        storage->remove("warp_ev_mac");
        bound_vehicle_mac.store(0);
        transport = std::make_shared<TeslaBleTransportAdapter>(this);
        vehicle = std::make_unique<TeslaBLE::Vehicle>(transport, storage);
        vehicle->set_vin(vin);
        vehicle->set_charge_state_callback([this](const CarServer_ChargeState &charge_state) {
            if (charge_state.which_optional_battery_level && in_flight_started_ms != 0) {
                const float soc = static_cast<float>(charge_state.optional_battery_level.battery_level);
                if (policy.accept_result(in_flight_generation, in_flight_ev_index, soc)) {
                    post_event(EventType::Soc, nullptr, soc, in_flight_generation, in_flight_ev_index);
                }
                in_flight_started_ms = 0;
            }
        });
        post_event(ok ? EventType::KeyRemoved : EventType::Error,
                   ok ? nullptr : "Could not remove Tesla key material");
    }

    if (pair_requested.exchange(false)) {
        const uint64_t current_mac = active_vehicle_mac.load();
        if (active_ev_index.load() != configured_ev_index || current_mac == 0) {
            post_event(EventType::Error, "Configured Tesla is not currently connected");
        } else if (!ensure_connected()) {
            post_event(EventType::Error, "Could not connect to Tesla for pairing");
        } else if (!storage->save("warp_ev_mac", encode_vehicle_mac(current_mac))) {
            post_event(EventType::Error, "Could not persist WARP vehicle MAC binding");
        } else {
            bound_vehicle_mac.store(current_mac);
            vehicle->pair(Keys_Role_ROLE_CHARGING_MANAGER);
            if (storage->has_private_key()) {
                post_event(EventType::PairStarted);
            } else {
                storage->remove("warp_ev_mac");
                bound_vehicle_mac.store(0);
                post_event(EventType::Error, "Could not persist Tesla pairing key");
            }
        }
    }

    if (poll_requested.exchange(false)) {
        request_soc(true);
    }
}

void TeslaVehicle::request_soc(bool manual)
{
    const uint32_t generation = active_generation.load();
    if (active_vehicle_mac.load() == 0 || active_vehicle_mac.load() != bound_vehicle_mac.load()) {
        if (manual) {
            post_event(EventType::Error, "Connected vehicle MAC is not bound to this Tesla key");
        }
        next_poll_ms = millis() + poll_interval_seconds * 1000U;
        return;
    }
    TeslaBleSocRequestDecision decision = policy.begin_request(generation, configured_ev_index, wake_once);
    if (!decision.allowed) {
        if (manual) {
            post_event(EventType::Error, "Configured EV profile is not currently connected");
        }
        next_poll_ms = millis() + poll_interval_seconds * 1000U;
        return;
    }

    if (!ensure_connected()) {
        post_event(EventType::Error, "Could not connect to Tesla over BLE");
        next_poll_ms = millis() + poll_interval_seconds * 1000U;
        return;
    }

    in_flight_generation = decision.generation;
    in_flight_ev_index = configured_ev_index;
    in_flight_started_ms = millis();
    vehicle->charge_state_poll(decision.allow_wake ? TeslaBLE::WakePolicy::WAKE_IF_NEEDED
                                                   : TeslaBLE::WakePolicy::NO_WAKE_SKIP);
    next_poll_ms = millis() + poll_interval_seconds * 1000U;
}

bool TeslaVehicle::ensure_connected()
{
    if (ble_client != nullptr && ble_client->isConnected() &&
        write_characteristic != nullptr && notify_characteristic != nullptr) {
        return true;
    }

    disconnect_internal();
    if (ble_client == nullptr) {
        ble_client = BLEDevice::createClient();
        if (ble_client == nullptr) {
            return false;
        }
        ble_client->setClientCallbacks(this);
    }

    bool ok = false;
    for (uint8_t address_type = 0; address_type <= 1 && !ok; ++address_type) {
        BLEAddress address(String(ble_address.c_str()), address_type);
        ok = ble_client->connect(address, address_type, CONNECT_TIMEOUT_MS);
    }
    if (!ok) {
        disconnect_internal();
        return false;
    }

    ble_client->setMTU(517);
    BLERemoteService *service = ble_client->getService(BLEUUID(TESLA_SERVICE_UUID));
    if (service == nullptr) {
        disconnect_internal();
        return false;
    }

    write_characteristic = service->getCharacteristic(BLEUUID(TESLA_WRITE_UUID));
    notify_characteristic = service->getCharacteristic(BLEUUID(TESLA_NOTIFY_UUID));
    if (write_characteristic == nullptr || notify_characteristic == nullptr ||
        !write_characteristic->canWrite() || !notify_characteristic->canNotify()) {
        disconnect_internal();
        return false;
    }

    notify_characteristic->registerForNotify(notification_callback, true, true);
    connected.store(true);
    vehicle->set_connected(true);
    post_event(EventType::Connected);
    return true;
}

void TeslaVehicle::disconnect_internal()
{
    policy.on_transport_disconnected();
    in_flight_started_ms = 0;
    if (rx_queue != nullptr) {
        xQueueReset(rx_queue);
    }
    connected.store(false);
    if (vehicle) {
        vehicle->set_connected(false);
    }
    write_characteristic = nullptr;
    notify_characteristic = nullptr;
    if (ble_client != nullptr) {
        if (ble_client->isConnected()) {
            ble_client->disconnect();
        }
    }
}

bool TeslaVehicle::transport_write(const std::vector<uint8_t> &data)
{
    if (!connected.load() || write_characteristic == nullptr || data.empty()) {
        return false;
    }

    const size_t mtu_payload = std::max<size_t>(20, ble_client->getMTU() - 3);
    for (size_t offset = 0; offset < data.size(); offset += mtu_payload) {
        const size_t length = std::min(mtu_payload, data.size() - offset);
        if (!write_characteristic->writeValue(const_cast<uint8_t *>(data.data() + offset), length, true)) {
            return false;
        }
    }
    return true;
}

void TeslaVehicle::transport_disconnect()
{
    disconnect_internal();
}

void TeslaVehicle::onConnect(BLEClient * /*client*/)
{
}

void TeslaVehicle::onDisconnect(BLEClient * /*client*/)
{
    connected.store(false);
    transport_disconnected.store(true);
    if (vehicle) {
        vehicle->set_connected(false);
    }
    post_event(EventType::Disconnected);
}

void TeslaVehicle::notification_callback(BLERemoteCharacteristic * /*characteristic*/,
                                     uint8_t *data, size_t length, bool /*is_notify*/)
{
    if (instance == nullptr || instance->rx_queue == nullptr || length == 0 || length > sizeof(RxFragment::data)) {
        return;
    }

    RxFragment fragment;
    fragment.length = static_cast<uint16_t>(length);
    memcpy(fragment.data, data, length);
    xQueueSend(instance->rx_queue, &fragment, 0);
}

void TeslaVehicle::process_rx()
{
    RxFragment fragment;
    while (xQueueReceive(rx_queue, &fragment, 0) == pdTRUE) {
        vehicle->on_rx_data(std::vector<uint8_t>(fragment.data, fragment.data + fragment.length));
    }
}

void TeslaVehicle::post_event(EventType type, const char *message, float soc,
                          uint32_t generation, int ev_index)
{
    if (event_queue == nullptr) {
        return;
    }

    ModuleEvent event;
    event.type = type;
    event.generation = generation;
    event.ev_index = static_cast<int8_t>(ev_index);
    event.soc = soc;
    if (message != nullptr) {
        strlcpy(event.message, message, sizeof(event.message));
    }
    xQueueSend(event_queue, &event, 0);
}

void TeslaVehicle::loop()
{
    const uint32_t generation = ev.get_session_generation();
    const int index = ev.get_active_ev_index();
    active_generation.store(generation);
    active_ev_index.store(index);
    active_vehicle_mac.store(ev.get_active_ev_mac());
    process_module_events();
}

void TeslaVehicle::process_module_events()
{
    ModuleEvent event;
    while (event_queue != nullptr && xQueueReceive(event_queue, &event, 0) == pdTRUE) {
        switch (event.type) {
        case EventType::Connected:
            state.get("connected")->updateBool(true);
            state.get("last_error")->updateString("");
            break;
        case EventType::Disconnected:
            state.get("connected")->updateBool(false);
            break;
        case EventType::Soc:
            if (event.generation == ev.get_session_generation() &&
                event.ev_index == ev.get_active_ev_index() &&
                event.ev_index == configured_ev_index &&
                std::isfinite(event.soc) && event.soc >= 0.0f && event.soc <= 100.0f) {
                ev.set_soc(event.soc);
                ev.session_updated(EVDataSource::Tesla);
                pairing_policy.on_authenticated_response();
                state.get("key_present")->updateBool(pairing_policy.key_present());
                state.get("paired")->updateBool(pairing_policy.paired());
                state.get("pairing_pending")->updateBool(pairing_policy.pairing_pending());
                state.get("last_soc")->updateFloat(event.soc);
                state.get("last_update")->updateUptime(now_us());
                state.get("last_error")->updateString("");
            }
            break;
        case EventType::Error:
            pairing_policy.on_pairing_error();
            state.get("last_error")->updateString(event.message);
            state.get("pairing_pending")->updateBool(pairing_policy.pairing_pending());
            break;
        case EventType::PairStarted:
            pairing_policy.on_pair_request_sent();
            state.get("key_present")->updateBool(pairing_policy.key_present());
            state.get("paired")->updateBool(pairing_policy.paired());
            state.get("pairing_pending")->updateBool(pairing_policy.pairing_pending());
            state.get("last_error")->updateString("");
            break;
        case EventType::KeyRemoved:
            pairing_policy.on_key_removed();
            state.get("key_present")->updateBool(pairing_policy.key_present());
            state.get("paired")->updateBool(pairing_policy.paired());
            state.get("connected")->updateBool(false);
            state.get("pairing_pending")->updateBool(pairing_policy.pairing_pending());
            break;
        case EventType::Stopped:
            state.get("running")->updateBool(false);
            state.get("connected")->updateBool(false);
            break;
        }
    }
}
