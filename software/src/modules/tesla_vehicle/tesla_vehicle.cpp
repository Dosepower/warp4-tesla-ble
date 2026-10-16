#include "tesla_vehicle.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <cctype>
#include "tesla_ble_iram_diagnostics.h"
#include "tesla_ble_bench_smoke.h"
#include "tesla_ble_bench_discovery.h"

#ifndef TESLA_BLE_MODULE_HOST_TEST
#include <BLEAddress.h>
#include <BLEDevice.h>

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp32-hal-bt.h>

#include "event_log_prefix.h"
#include "generated/module_dependencies.h"
#endif
#include "tesla_ble_storage.h"
#include "tesla_ble_transport.h"
#include "tesla_ble_validation.h"
#include "tesla_ble_config_policy.h"
#include "tesla_ble_result.h"
#include "tesla_ble_key_command.h"
#include "tesla_ble_presence.h"


static constexpr uint32_t RESPONSE_TIMEOUT_MS = 40000;

static String checked_error_string(const char *message)
{
    String text(message);
    if (!text) { return String("OOM"); } // SSO nonempty rejection.
    return text;
}

static std::array<uint8_t, 6> encode_vehicle_mac(uint64_t mac)
{
    std::array<uint8_t, 6> bytes{};
    for (int i = 5; i >= 0; --i) {
        bytes[i] = static_cast<uint8_t>(mac & 0xFF);
        mac >>= 8;
    }
    return bytes;
}


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
        char vin[18]{}, address[18]{};
        const auto &vin_text = update.get("vin")->asString();
        const auto &address_text = update.get("ble_address")->asString();
        if (vin_text.length() > 17 || address_text.length() > 17) { return String("identity size"); }
        strlcpy(vin, vin_text.c_str(), sizeof(vin));
        strlcpy(address, address_text.c_str(), sizeof(address));
        for (char &c : vin) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
        for (char &c : address) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
        if (!update.get("vin")->updateStringChecked(vin) || !update.get("ble_address")->updateStringChecked(address)) {
            return String("OOM");
        }

        if (update.get("enable")->asBool()) {
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
            return checked_error_string("Tesla runtime is unavailable in bench stack-smoke firmware");
#endif
            if (!tesla_ble_valid_vin(vin)) {
                return checked_error_string("VIN must contain exactly 17 valid characters");
            }
            if (!tesla_ble_valid_address(address)) {
                return checked_error_string("BLE address must use AA:BB:CC:DD:EE:FF format");
            }
            if (update.get("ev_index")->asInt() < 0) {
                return checked_error_string("A WARP EV profile must be selected");
            }
        }

        const bool changed = update.get("enable")->asBool() != config.get("enable")->asBool() ||
                             update.get("vin")->asString() != config.get("vin")->asString() ||
                             update.get("ble_address")->asString() != config.get("ble_address")->asString() ||
                             update.get("ev_index")->asInt() != config.get("ev_index")->asInt() ||
                             update.get("poll_interval")->asUint() != config.get("poll_interval")->asUint() ||
                             update.get("wake_once")->asBool() != config.get("wake_once")->asBool();
        const auto action = tesla_ble_config_update_action(source == ConfigSource::File,
                                                            runtime_owner.get() != nullptr,
                                                            changed);
        if (action == TeslaBleConfigUpdateAction::ApplyInLoop) {
            runtime_update_requested.store(true);
        }
        return "";
    }};

    state = Config::Object({
        {"running", Config::Bool(false)},
        {"stopping", Config::Bool(false)},
        {"connected", Config::Bool(false)},
        {"key_present", Config::Bool(false)},
        {"key_presence", Config::Str("unknown", 0, TESLA_BLE_PRESENCE_MAX_LENGTH)},
        {"paired", Config::Bool(false)},
        {"pairing_pending", Config::Bool(false)},
        // HTTP acceptance is not evidence of transmission or authentication.
        {"pair_request_sent", Config::Bool(false)},
        {"restart_required", Config::Bool(false)},
        {"last_soc", Config::Float(NAN)},
        {"last_update", Config::Uptime()},
        {"last_error", Config::Str("", 0, 96)},
    });

    empty_command = Config::Object({});
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
    tesla_ble_bench_smoke_pre_setup();
#endif
}

void TeslaVehicle::setup()
{
    if (!api.restorePersistentConfig("tesla_vehicle/config", &config) &&
        api.restorePersistentConfig("tesla_ble/config", &config)) {
        api.writeConfig("tesla_vehicle/config", &config);
        api.removeConfig("tesla_ble/config");
    }

    // Unknown until a worker explicitly inspects storage. Disabled setup must
    // neither probe the filesystem nor allocate Tesla runtime resources.
    pairing_policy.on_key_loaded(false);
    state.get("key_present")->updateBool(pairing_policy.key_present());
    state.get("paired")->updateBool(pairing_policy.paired());

    tesla_ble_iram_snapshot("module.setup", "boot");
    if (config.get("enable")->asBool()) {
        start_runtime();
    }

    initialized = true;
}

void TeslaVehicle::register_urls()
{
    api.addPersistentConfig("tesla_vehicle/config", &config, {}, {"vin", "ble_address"});
    api.addState("tesla_vehicle/state", &state);
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
    tesla_ble_bench_smoke_register_urls();
    tesla_ble_bench_discovery_register_urls();
#endif

    api.addCommand("tesla_vehicle/pair", &empty_command, {}, [this](Language, String &errmsg) {
        if (!accepts_commands()) {
            errmsg = checked_error_string("Tesla BLE is not enabled");
            return;
        }
        Runtime *r = runtime_owner.get();
        const auto identity = r->session.identity();
        if (!identity || identity->index != r->configured_ev_index || identity->mac == 0) {
            errmsg = checked_error_string("Configured Tesla is not currently connected");
            return;
        }
        if (!r->command_outcome.accept_pair(*identity)) {
            errmsg = checked_error_string("Tesla pairing is pending or deletion is active");
            return;
        }
        r->pair_requested.store(true);
        state.get("pair_request_sent")->updateBool(false);
        state.get("pairing_pending")->updateBool(true);
    }, true);

    api.addCommand("tesla_vehicle/poll", &empty_command, {}, [this](Language, String &errmsg) {
        if (!accepts_commands() || !runtime_owner.get()->command_outcome.forward_available()) {
            errmsg = checked_error_string("Tesla BLE is not enabled");
            return;
        }
        runtime_owner.get()->poll_requested.store(true);
    }, true);

    api.addCommand("tesla_vehicle/forget_key", &empty_command, {}, [this](Language, String &errmsg) {
        if (runtime_owner.get() == nullptr) {
            if (!start_runtime(true)) { errmsg = checked_error_string("Could not start Tesla storage worker; reboot may be required"); }
            return;
        }
        if (!accepts_commands()) {
            errmsg = checked_error_string("Tesla BLE is stopping; retry after teardown");
            return;
        }
        Runtime *r = runtime_owner.get();
        if (!r->command_outcome.accept_forget()) {
            errmsg = checked_error_string("Tesla key deletion is already active");
            return;
        }
        r->pair_requested.store(false);
        r->poll_requested.store(false);
        r->forget_requested.store(true);
    }, true);
}

bool TeslaVehicle::accepts_commands() const
{
    const Runtime *r = runtime_owner.get();
    return !runtime_owner.restart_required() && !ev.session_exhausted() && config.get("enable")->asBool() && r != nullptr &&
           !r->identity_exhausted.load() &&
           !runtime_update_requested.load() && !r->stop_requested.load();
}

bool TeslaVehicle::start_runtime(bool storage_only)
{
    if (ev.session_exhausted()) {
        runtime_owner.require_restart(); state.get("restart_required")->updateBool(true); return false;
    }
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
    (void)storage_only;
    return false; // Bench smoke must never create a Tesla runtime.
#else
    start_attempted = true;
    return runtime_owner.start(storage_only || config.get("enable")->asBool(), [this, storage_only]() -> std::unique_ptr<Runtime> {
        auto r = std::unique_ptr<Runtime>(new (std::nothrow) Runtime(this));
        if (!r) {
            publish_error("Failed to allocate Tesla runtime");
            return nullptr;
        }
        if (!r->session.synchronization_initialized() || !r->command_outcome.synchronization_initialized() ||
            !r->pending_disconnect.synchronization_initialized()) {
            publish_error("Failed to initialize Tesla synchronization");
            return nullptr;
        }
        bool rx = true;
        if (!r->queues.create([&rx]() {
                const size_t size = rx ? sizeof(RxFragment) : sizeof(ModuleEvent);
                rx = false;
                return xQueueCreate(8, size);
            })) {
            publish_error("Failed to allocate BLE queues");
            return nullptr;
        }
        const auto &vin = config.get("vin")->asString();
        const auto &address = config.get("ble_address")->asString();
        if (!storage_only && (!tesla_ble_valid_vin(vin.c_str()) || !tesla_ble_valid_address(address.c_str()))) {
            publish_error("Invalid or unavailable Tesla BLE identity");
            return nullptr;
        }
        r->vin.assign(vin.c_str());
        if (!storage_only) {
            unsigned bytes[6]{};
            if (sscanf(address.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x", &bytes[0], &bytes[1], &bytes[2],
                       &bytes[3], &bytes[4], &bytes[5]) != 6) { return nullptr; }
            for (size_t i = 0; i < 6; ++i) { r->remote_address[i] = static_cast<uint8_t>(bytes[i]); }
        }
        r->configured_ev_index = config.get("ev_index")->asInt();
        r->poll_interval_seconds = config.get("poll_interval")->asUint();
        r->wake_once = config.get("wake_once")->asBool();
        r->storage_only = storage_only;
        return r;
    }, [this](Runtime &r) {
        r.epoch = runtime_owner.epoch();
        r.session.publish({ev.get_session_generation(), ev.get_active_ev_index(), ev.get_active_ev_mac()});
        callback_gate.open();
        if (xTaskCreatePinnedToCore(task_entry, "tesla_ble", 12288, this, 1, &r.task_handle, 0) != pdPASS) {
            callback_gate.close();
            r.task_handle = nullptr;
            publish_error("Failed to start Tesla BLE task");
            return false;
        }
        state.get("running")->updateBool(true);
        return true;
    });
#endif
}

bool TeslaVehicle::create_vehicle()
{
    Runtime *r = runtime_owner.get();
    r->vehicle.reset();
    r->keys.reset(new (std::nothrow) tesla_clean::IdentityStore);
    if (!r->keys || !r->storage->initialize_identity(*r->keys, {TeslaBleTransportAdapter::random, nullptr}, false) ||
        TeslaBleTransportAdapter::random(nullptr, r->routing_address.data(), r->routing_address.size()) != 0) { return false; }
    r->vehicle.reset(new (std::nothrow) tesla_clean::Vehicle(*r->keys,
        {reinterpret_cast<const uint8_t *>(r->vin.c_str()), 17},
        {r->routing_address.data(), r->routing_address.size()}, r->transport_adapter.ports()));
    return r->vehicle != nullptr;
}

void TeslaVehicle::task_entry(void *context)
{
    static_cast<TeslaVehicle *>(context)->task_loop();
}

void TeslaVehicle::task_loop()
{
    Runtime *r = runtime_owner.get();
    if (!r->stop_requested.load()) {
        r->worker_watch.begin(millis());
        const auto initial = r->session.identity();
        if (initial) { r->observed_identity = *initial; r->policy.on_vehicle_changed(initial->generation, initial->index); }
        if (!r->storage->set_mutation_guard([r]() {
            const auto identity = r->session.identity();
            return !r->stop_requested.load() && !r->operation.retired() &&
                (r->storage_only || (identity && *identity == r->observed_identity));
        })) {
            post_event(EventType::Error, "Failed to allocate Tesla storage guard");
            r->stop_requested.store(true);
        }
        if (!r->stop_requested.load() && r->storage_only) {
            if (!r->storage->forget_material()) { post_event(EventType::Error, "Could not remove Tesla key material"); }
            r->stop_requested.store(true);
        } else if (!r->stop_requested.load()) {
            uint8_t stored_vehicle_mac[6]{};
            size_t stored_size = 0;
            const auto loaded = r->storage->load_into("warp_ev_mac", stored_vehicle_mac, sizeof(stored_vehicle_mac), stored_size);
            if (loaded == tesla_clean::LoadResult::Error || loaded == tesla_clean::LoadResult::RestartRequired ||
                (loaded == tesla_clean::LoadResult::Loaded && stored_size != 6)) {
                post_event(EventType::Error, "Could not load WARP vehicle MAC binding");
                r->stop_requested.store(true);
            } else if (loaded == tesla_clean::LoadResult::Loaded && stored_size == 6) {
                uint64_t mac = 0;
                for (uint8_t byte : stored_vehicle_mac) { mac = (mac << 8) | byte; }
                r->bound_vehicle_mac.store(mac);
            }
            r->session.bind(r->bound_vehicle_mac.load());
            const auto presence = r->storage->private_key_presence();
            post_event(EventType::KeyPresence, nullptr, presence == TeslaBleStorageAdapter::KeyPresence::Unknown ? NAN :
                       presence == TeslaBleStorageAdapter::KeyPresence::CleanupRequired ? 2.0f :
                       presence == TeslaBleStorageAdapter::KeyPresence::Present ? 1.0f : 0.0f);
            if (r->stop_requested.load() || !create_vehicle()) {
                post_event(EventType::Error, "Tesla protocol initialization failed or cancelled");
                r->stop_requested.store(true);
            } else {
                r->controller_used = true;
                if (BLEDevice::init("WARP4")) {
                    gatt_owner.store(this);
                    BLEDevice::setCustomGattcHandler(gatt_callback);
                    r->ble_ready.store(true);
                } else {
                    post_event(EventType::Error, "Failed to initialize Bluetooth");
                    r->stop_requested.store(true);
                }
            }
        }
        r->worker_watch.finish();
    }

    while (!r->stop_requested.load()) {
        r->worker_watch.begin(millis());
        if (r->storage->restart_required()) {
            post_event(EventType::Error, "Tesla persistence close failed; storage quarantined until reboot");
            r->stop_requested.store(true);
            r->worker_watch.finish();
            break;
        }
        process_disconnect();
        update_active_vehicle();
        process_rx();
        process_commands();
        process_pair_verification();
        if (r->stop_requested.load()) {
            r->worker_watch.finish();
            break;
        }
        r->vehicle->tick();
        check_worker_exhaustion();

        if (r->in_flight_started_ms != 0 && millis() - r->in_flight_started_ms > RESPONSE_TIMEOUT_MS) {
            r->in_flight_started_ms = 0;
            post_event(EventType::Error, "Tesla SoC request timed out");
        }

        const auto identity = r->session.identity();
        const bool matching_vehicle = identity && identity->index == r->configured_ev_index;
        if (matching_vehicle && static_cast<int32_t>(millis() - r->next_poll_ms) >= 0 && r->in_flight_started_ms == 0) {
            request_soc(false);
        }

        r->worker_watch.finish();
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    r->worker_watch.begin(millis());
    disconnect_internal();
    if (r->storage->restart_required()) {
        post_event(EventType::Error, "Tesla persistence close failed; storage quarantined until reboot");
    }
    const auto final_presence = r->storage->private_key_presence();
    const auto final_order = r->command_outcome.next_order();
    r->shutdown_result.publish(final_presence == TeslaBleStorageAdapter::KeyPresence::Present, r->terminal_error,
                               final_presence != TeslaBleStorageAdapter::KeyPresence::Unknown,
                               final_presence == TeslaBleStorageAdapter::KeyPresence::CleanupRequired,
                               final_order, r->terminal_error_order, true, final_order == 0);
    r->worker_watch.finish();
    // ACK is independent of the lossy event queue. After publishing it, this
    // task must never touch runtime again. Main deletes this parked task.
    r->worker_stopped.store(true, std::memory_order_release);
    for (;;) {
        vTaskSuspend(nullptr);
    }
}

bool TeslaVehicle::release_bluetooth(Runtime &r)
{
    if (!r.controller_used) {
        return true;
    }

    // The pinned Arduino fork deletes clients BEFORE stopping Bluedroid in
    // BLEDevice::deinit. Quiesce the IDF host first so copied raw client and
    // characteristic pointers in GATT dispatch cannot outlive those objects.
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED && esp_bluedroid_disable() != ESP_OK) {
        strlcpy(r.teardown_error, "Bluetooth host disable failed; restart required", sizeof(r.teardown_error));
        return false;
    }
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED && esp_bluedroid_deinit() != ESP_OK) {
        strlcpy(r.teardown_error, "Bluetooth host teardown failed; restart required", sizeof(r.teardown_error));
        return false;
    }
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED && esp_bt_controller_disable() != ESP_OK) {
        strlcpy(r.teardown_error, "Bluetooth controller disable failed; restart required", sizeof(r.teardown_error));
        return false;
    }
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE && esp_bt_controller_deinit() != ESP_OK) {
        strlcpy(r.teardown_error, "Bluetooth controller teardown failed; restart required", sizeof(r.teardown_error));
        return false;
    }
    // This owns/deletes the single BLEDevice client; do not delete it twice.
    BLEDevice::deinit(false);
    BLEDevice::setCustomGattcHandler(nullptr);
    gatt_owner.store(nullptr);
    // Also covers a partially failed BLEDevice::init, where deinit returns
    // early because its private 'initialized' flag was never set.
    if (!btMemRelease(BT_MODE_BLE)) {
        strlcpy(r.teardown_error, "Bluetooth BLE memory release failed; restart required", sizeof(r.teardown_error));
        return false;
    }
    return btMemReleased(BT_MODE_BLE);
}

void TeslaVehicle::teardown_entry(void *context)
{
    auto *owner = static_cast<TeslaVehicle *>(context);
    Runtime *r = owner->runtime_owner.get();
    const bool ok = owner->release_bluetooth(*r);
    r->teardown.complete(ok);
    // No runtime/BLE access after completion; only an acknowledged task is deleted.
    for (;;) { vTaskSuspend(nullptr); }
}

void TeslaVehicle::stop_runtime()
{
    runtime_owner.begin_stop([this](Runtime &r) {
        callback_gate.close();
        r.stop_requested.store(true);
        r.deferred_soc.reset(); r.deferred_attempts = 0;
        r.teardown.begin(millis());
        state.get("stopping")->updateBool(true);
        state.get("connected")->updateBool(false);
        state.get("pairing_pending")->updateBool(false);
        state.get("pair_request_sent")->updateBool(false);
    });
    Runtime *r = runtime_owner.get();
    if (r == nullptr) {
        return;
    }
    const bool stopped = r->worker_stopped.load(std::memory_order_acquire);
    // Cancellation can hold outcome while waiting for session; never wait here.
    // Unknown classification stays pending, but teardown supervision continues.
    const auto stop_state = r->command_outcome.stop_state();
    if (r->storage->restart_required() || (stop_state && (stop_state->deletion_incomplete || stop_state->exhausted))) {
        runtime_owner.require_restart();
        state.get("restart_required")->updateBool(true);
    }
    if (stopped) {
        // Completed erase is an authoritative negative pairing fact even when
        // MAX-order KeyRemoved was not consumed and the final probe is unknown.
        if (stop_state && stop_state->deletion_complete) {
            state.get("paired")->updateBool(false);
        }
        const auto *outcome = r->shutdown_result.read();
        if (outcome && !r->shutdown_presence_applied &&
            (outcome->final_presence || tesla_ble_publication_newer(outcome->presence_order, r->pairing_order))) {
            r->shutdown_presence_applied = true;
            if (outcome->presence_order > r->pairing_order) { r->pairing_order = outcome->presence_order; }
            state.get("key_present")->updateBool(outcome->key_present);
            tesla_ble_publish_presence(state.get("key_presence"), outcome->key_present, outcome->key_known, outcome->cleanup_required);
            // Final negative material authority invalidates possession now,
            // even if erase/reconstruction or retained teardown later fails.
            // Present/Unknown cannot revoke an authenticated pairing fact.
            if (outcome->cleanup_required || (outcome->key_known && !outcome->key_present)) {
                pairing_policy.on_key_loaded(false);
                state.get("paired")->updateBool(false);
            }
        }
        if (outcome && !r->supervision_failed && tesla_ble_publication_newer(outcome->error_order, r->error_order)) {
            r->error_order = outcome->error_order; publish_error(outcome->error);
        }
        if (outcome && outcome->publication_exhausted) {
            runtime_owner.require_restart(); state.get("restart_required")->updateBool(true);
            if (!r->supervision_failed) {
                r->error_order = UINT64_MAX; // Terminal fence, not an emitted identity.
                publish_error("Tesla identity/publication sequence exhausted; reboot required");
            }
        }
    }
    const auto completion = r->teardown.poll(millis(), stopped, callback_gate.drained(), [this, r]() {
        vTaskDelete(r->task_handle);
        r->task_handle = nullptr;
        return xTaskCreatePinnedToCore(teardown_entry, "tesla_teardown", 4096, this, 1,
                                       &r->teardown_handle, 0) == pdPASS;
    });
    if (completion == TeslaBleTeardown::Result::Failed) {
        runtime_owner.require_restart();
        state.get("restart_required")->updateBool(true);
        if (r->teardown.completed_result() == TeslaBleTeardown::Result::Failed && r->teardown_error[0] != '\0') {
            // Producer is quiescent. Main teardown failure needs no mailbox lock.
            r->error_order = UINT64_MAX;
            r->supervision_failed = true;
            publish_error(r->teardown_error);
        } else if (!r->supervision_failed) {
            // A teardown timeout is authoritative even without an earlier
            // worker-watch failure. Fence later exhaustion/worker diagnostics.
            r->error_order = UINT64_MAX;
            r->supervision_failed = true;
            publish_error("Tesla BLE stop failed or timed out; ownership retained; reboot required");
        }
        return;
    }
    if (completion != TeslaBleTeardown::Result::Succeeded) { return; }
    if (!stop_state) { return; } // Retain ownership; retry classification without waiting.
    if (r->teardown_handle != nullptr) {
        vTaskDelete(r->teardown_handle);
        r->teardown_handle = nullptr;
    }
    if (runtime_owner.finish_stop(stopped, callback_gate.drained(), r->controller_used || r->supervision_failed,
                                  [](Runtime &runtime) { return runtime.teardown.succeeded(); })) {
        // Destroying Runtime releases protocol, adapter references, strings,
        // flags and both queues. Keys/configuration remain on disk untouched.
        pairing_policy.on_key_loaded(state.get("key_present")->asBool());
        state.get("paired")->updateBool(false);
        state.get("running")->updateBool(false);
        state.get("stopping")->updateBool(false);
        state.get("restart_required")->updateBool(runtime_owner.restart_required());
    }
}

void TeslaVehicle::update_active_vehicle()
{
    Runtime *r = runtime_owner.get();
    const auto identity = r->session.identity();
    if (!identity) { return; } // Contention is not unplugging; retry next iteration.
    if (identity == r->observed_identity) {
        return;
    }

    r->observed_identity = *identity;
    r->policy.on_vehicle_changed(identity->generation, identity->index);
    r->in_flight_started_ms = 0;
    r->next_poll_ms = millis();

    // Clear pending protocol work on every identity change, same profile too.
    disconnect_internal();
}

void TeslaVehicle::process_commands()
{
    if (check_worker_exhaustion()) { return; }
    Runtime *r = runtime_owner.get();
    if (!r->session.identity()) { return; } // Leave command flags pending on contention.
    if (r->forget_requested.exchange(false)) {
        r->pair_requested.store(false);
        r->poll_requested.store(false);
        const auto cancelled = r->command_outcome.retire_pair();
        if (cancelled) { cancel_pairing("Pairing cancelled by local deletion; approval not authenticated", cancelled); }
        bool reconstructed = false;
        const auto result = tesla_ble_forget(
            [r]() { return r->stop_requested.load() || r->operation.retired(); },
            [this]() { disconnect_internal(); }, [r]() { r->vehicle.reset(); },
            [r]() { return r->storage->forget_material(); }, [this, r, &reconstructed]() {
                r->bound_vehicle_mac.store(0);
                r->session.bind(0);
                reconstructed = create_vehicle();
            });
        if (result != TeslaBleForgetResult::deleted || !reconstructed) {
            post_event(EventType::Error, "Tesla key deletion failed/cancelled or protocol allocation failed; remains detached");
            r->stop_requested.store(true);
            return;
        }
        post_event(EventType::KeyRemoved);
        r->command_outcome.finish_forget();
        return; // Never execute retained forward commands in the deletion pass.
    }

    if (r->command_outcome.forgetting()) { return; }

    if (r->pair_requested.load() && (r->pair_sending || r->pair_verifying || r->pair_status_active || r->pair_approval_waiting.load() ||
                                    r->in_flight_started_ms != 0)) { return; }
    if (r->pair_requested.exchange(false)) {
        const auto captured = r->session.identity();
        if (!captured) { r->pair_requested.store(true); return; }
        const auto accepted = r->command_outcome.claim_pair();
        if (!accepted) { return; }
        r->pair_command = accepted->command;
        const uint64_t current_mac = captured->mac;
        tesla_clean::IdentityLease current_key;
        const auto encoded_mac = encode_vehicle_mac(current_mac);
        if (*captured != accepted->vehicle || captured->index != r->configured_ev_index || current_mac == 0) {
            cancel_pairing("Configured Tesla is not currently connected", r->pair_command);
        } else if (!r->keys || (!r->keys->acquire(current_key) &&
                   !r->storage->initialize_identity(*r->keys, {TeslaBleTransportAdapter::random, nullptr}))) {
            cancel_pairing("Could not durably initialize Tesla pairing key", r->pair_command);
        } else if (!ensure_connected()) {
            cancel_pairing("Could not connect to Tesla for pairing", r->pair_command);
        } else if (r->stop_requested.load() || r->session.identity() != captured) {
            cancel_pairing("Vehicle changed during pairing connection", r->pair_command);
        } else if (!tesla_clean::KeyStorage::committed(r->storage->save_transaction("warp_ev_mac", encoded_mac.data(), encoded_mac.size()))) {
            cancel_pairing("Could not persist WARP vehicle MAC binding", r->pair_command);
        } else {
            if (r->stop_requested.load() || r->operation.retired() || r->session.identity() != captured ||
                !r->command_outcome.pair_current(r->pair_command)) {
                cancel_pairing("Pairing cancelled before submission; vehicle or command changed", r->pair_command); return;
            }
            r->bound_vehicle_mac.store(current_mac);
            r->session.bind(current_mac);
            if (r->stop_requested.load() || r->session.identity() != captured || !r->connected.load()) {
                cancel_pairing("Pairing cancelled before submission; link or vehicle changed", r->pair_command); return;
            }
            r->pair_request = r->session.arm(r->epoch, *captured, r->configured_ev_index);
            const auto request = r->pair_request;
            const auto command = r->pair_command;
            tesla_clean::VehicleCompletion completion;
            const bool ready = request.request != 0 && completion.assign([this, r, request, command](const tesla_clean::VehicleResult &result) {
                if (!r->command_outcome.pair_current(command)) { return; }
                if (r->stop_requested.load() || result.context != request.request ||
                    result.generation != r->connection_generation.load() || !r->session.matches(request)) {
                    cancel_pairing("Pairing link/session lost; transmission may have occurred; approval not authenticated", command); return;
                }
                r->pair_sending = false;
                if (result.code == tesla_clean::VehicleResultCode::PairRequestSent) {
                    r->command_outcome.pair_sent(command);
                    post_event(EventType::PairStarted);
                    r->pair_verifying = true;
                    r->pair_started_ms = millis(); r->pair_next_ms = millis() + 1000;
                } else { cancel_pairing("Tesla pairing transmission failed; approval not authenticated", command); }
            });
            r->pair_sending = ready && r->vehicle->pair(true, request.request, std::move(completion));
            if (!r->pair_sending) {
                r->session.disarm(request);
                cancel_pairing("Tesla pairing command refused; no pairing is pending", command);
            }
        }
    }

    if (r->poll_requested.exchange(false)) {
        request_soc(true);
    }
    check_worker_exhaustion();
}

void TeslaVehicle::request_soc(bool manual)
{
    if (check_worker_exhaustion()) { return; }
    Runtime *r = runtime_owner.get();
    if (!r->command_outcome.forward_available()) { if (manual) { r->poll_requested.store(true); } return; }
    if (r->in_flight_started_ms != 0 || r->pair_sending || r->pair_verifying || r->pair_status_active || r->pair_approval_waiting.load()) {
        if (manual) { r->poll_requested.store(true); }
        return;
    }
    const auto captured = r->session.identity();
    if (!captured) { if (manual) { r->poll_requested.store(true); } return; }
    if (captured->mac == 0 || captured->mac != r->bound_vehicle_mac.load() ||
        captured->index != r->configured_ev_index) {
        if (manual) {
            post_event(EventType::Error, "Connected vehicle MAC is not bound to this Tesla key");
        }
        r->next_poll_ms = millis() + r->poll_interval_seconds * 1000U;
        return;
    }
    r->in_flight = tesla_ble_prepare_request(r->session, r->epoch, *captured, r->configured_ev_index,
        [this, r]() { return ensure_connected() && !r->stop_requested.load(); });
    if (r->in_flight.request == 0) {
        if (check_worker_exhaustion()) { return; }
        post_event(EventType::Error, "Could not connect to current Tesla over BLE");
        r->next_poll_ms = millis() + r->poll_interval_seconds * 1000U;
        return;
    }
    TeslaBleSocRequestDecision decision = r->policy.begin_request(captured->generation, r->configured_ev_index, manual || r->wake_once);
    if (!decision.allowed || r->in_flight.request == 0) {
        r->session.disarm(r->in_flight);
        r->in_flight = {};
        r->in_flight_started_ms = 0;
        if (manual) {
            post_event(EventType::Error, "Configured EV profile is not currently connected");
        }
        r->next_poll_ms = millis() + r->poll_interval_seconds * 1000U;
        return;
    }


    r->in_flight_started_ms = millis() ? millis() : 1;
    const auto request = r->in_flight;
    tesla_clean::VehicleCompletion completion;
    const bool ready = completion.assign([this, r, request](const tesla_clean::VehicleResult &result) {
        if (result.context != request.request || result.generation != r->connection_generation.load() ||
            r->stop_requested.load() || !r->session.matches(request)) { return; }
        const bool soc = result.code == tesla_clean::VehicleResultCode::Soc && result.authenticated;
        if (soc && r->policy.accept_result(request.vehicle.generation, request.vehicle.index, result.battery)) {
            post_event(EventType::Soc, nullptr, result.battery, request, true, true);
        } else {
            r->session.disarm(request);
            r->policy.on_transport_disconnected();
            if (result.code != tesla_clean::VehicleResultCode::SkippedAsleep) { post_event(EventType::Error, "Tesla authenticated SoC request failed"); }
        }
        r->in_flight_started_ms = 0;
    });
    const bool accepted = ready && r->vehicle->charge_state_poll(decision.allow_wake ? tesla_clean::WakePolicy::WakeIfNeeded
                                                   : tesla_clean::WakePolicy::NoWakeSkip, r->in_flight.request, std::move(completion),
                                                   {RESPONSE_TIMEOUT_MS, 5000, 10000});
    if (!accepted) {
        r->policy.request_refused(decision);
        check_worker_exhaustion();
        r->session.disarm(r->in_flight);
        r->in_flight = {};
        r->in_flight_started_ms = 0;
        post_event(EventType::Error, "Tesla SoC command refused; no request is in flight");
    }
    r->next_poll_ms = millis() + r->poll_interval_seconds * 1000U;
}

void TeslaVehicle::transport_disconnect()
{
    disconnect_internal();
}

void TeslaVehicle::process_pair_verification()
{
    if (check_worker_exhaustion()) { return; }
    Runtime *r = runtime_owner.get();
    if (!r->pair_verifying) { return; }
    if (r->stop_requested.load() || !r->session.matches(r->pair_request) ||
        millis() - r->pair_started_ms >= 60000) {
        // Retire queued/in-flight status before terminal ACK reopens admission.
        r->command_outcome.retire_pair();
        disconnect_internal(false);
        cancel_pairing("Approval deadline/session lost; request may have been sent; approval not authenticated", r->pair_command);
        return;
    }
    if (r->pair_status_active || r->command_outcome.forgetting()) { return; }
    if (static_cast<int32_t>(millis() - r->pair_next_ms) < 0) { return; }
    const auto request = r->pair_request;
    const auto command = r->pair_command;
    tesla_clean::VehicleCompletion completion;
    if (!completion.assign([this, r, request, command](const tesla_clean::VehicleResult &result) {
        if (!r->command_outcome.pair_current(command)) { return; }
        // Old completions cannot clear a successor's status flags.
        if (result.context != request.request || result.generation != r->connection_generation.load() ||
            !r->session.matches(request) || r->stop_requested.load()) {
            cancel_pairing("Pairing verification link/session lost; approval not authenticated", command); return;
        }
        r->pair_status_active = false;
        if (result.code == tesla_clean::VehicleResultCode::Status && result.authenticated) {
            r->pair_verifying = false;
            r->pair_approval_waiting.store(true);
            post_event(EventType::PairApproved, nullptr, NAN, request, true, true);
        } else {
            // Authenticated key denial means the physical approval is not present
            // yet. Keep the original pairing deadline; never re-send PRESENT_KEY.
            r->pair_next_ms = millis() + 1000;
        }
    })) {
        r->pair_next_ms = millis() + 1000;
        return;
    }
    r->pair_status_active = r->vehicle->status(request.request, std::move(completion), {5000, 1000, 10000});
    if (!r->pair_status_active) { r->pair_next_ms = millis() + 1000; }
}

void TeslaVehicle::cancel_pairing(const char *message, uint64_t command)
{
    Runtime *r = runtime_owner.get();
    const auto presence = r->storage->private_key_presence();
    const auto order = r->command_outcome.cancel_pair(command, message,
        presence == TeslaBleStorageAdapter::KeyPresence::Present,
        presence != TeslaBleStorageAdapter::KeyPresence::Unknown,
        presence == TeslaBleStorageAdapter::KeyPresence::CleanupRequired, [r, message]() {
        r->session.disarm(r->pair_request);
        r->pair_requested.store(false);
        r->pair_sending = r->pair_verifying = r->pair_status_active = false;
        r->pair_approval_waiting.store(false);
        strlcpy(r->terminal_error, message, sizeof(r->terminal_error));
    });
    if (order) { r->terminal_error_order = order; }
}

void TeslaVehicle::publish_pairing_state()
{
    Runtime *r = runtime_owner.get();
    const auto pair = r->command_outcome.take_pair_state([r](const TeslaBleRequestIdentity &approval) {
        return r->session.consume(approval,
            {ev.get_session_generation(), ev.get_active_ev_index(), ev.get_active_ev_mac()},
            r->configured_ev_index, true, true, 50, [](float) {});
    });
    if (!pair) { return; } // Contention never means idle.
    using Phase = TeslaBleCommandOutcome::PairPhase;
    const bool terminal = pair->completed;
    if (terminal) {
        const bool approved = pair->phase == Phase::Approval;
        // ACK ownership regardless of display order; an older terminal must not
        // undo a newer authenticated SoC already consumed from the event queue.
        if (tesla_ble_publication_newer(pair->order, r->pairing_order)) {
            r->pairing_order = pair->order;
            pairing_policy.on_key_loaded(pair->key_present);
            if (approved) { pairing_policy.on_authenticated_response(); }
            else { pairing_policy.on_pairing_error(); }
            state.get("key_present")->updateBool(pair->key_present);
            tesla_ble_publish_presence(state.get("key_presence"), pair->key_present, pair->key_known, pair->cleanup_required);
            state.get("paired")->updateBool(approved);
        }
        if (tesla_ble_publication_newer(pair->order, r->error_order)) {
            r->error_order = pair->order;
            publish_error(approved ? "" : pair->error[0] ? pair->error :
                "Pairing approval cancelled by session change; request may have been sent; not authenticated");
        }
        r->pair_approval_waiting.store(false);
    }
    const bool pending = pair->command && !terminal;
    state.get("pairing_pending")->updateBool(pending);
    state.get("pair_request_sent")->updateBool(pending && pair->sent);
    if (r->command_outcome.take_deleted()) {
        pairing_policy.on_key_removed();
        state.get("key_present")->updateBool(false);
        state.get("key_presence")->updateString("absent");
        state.get("paired")->updateBool(false);
        state.get("pairing_pending")->updateBool(false);
        state.get("pair_request_sent")->updateBool(false);
    }
}


void TeslaVehicle::process_rx()
{
    Runtime *r = runtime_owner.get();
    RxFragment fragment;
    for (size_t batch = 0; batch < 8 && xQueueReceive(r->queues.rx(), &fragment, 0) == pdTRUE; ++batch) {
        if (r->stop_requested.load() || fragment.connection_generation != r->connection_generation.load()) {
            continue;
        }
        r->vehicle->receive({fragment.data, fragment.length}, fragment.connection_generation);
    }
    check_worker_exhaustion();
}

bool TeslaVehicle::check_worker_exhaustion()
{
    Runtime *r = runtime_owner.get();
    if (!r) { return true; }
    if (r->session.exhausted() || r->command_outcome.publication_exhausted() ||
        (r->vehicle && r->vehicle->exhausted())) { r->identity_exhausted.store(true); }
    if (!r->identity_exhausted.load()) { return false; }
    r->stop_requested.store(true);
    return true; // Main sees the atomic latch, never worker-only counters.
}

void TeslaVehicle::post_event(EventType type, const char *message, float soc,
                          TeslaBleRequestIdentity request, bool authenticated, bool correlated)
{
    Runtime *r = runtime_owner.get();
    if (r == nullptr) {
        return;
    }

    ModuleEvent event;
    // Key/pair outcomes are authoritative and acknowledged, independently of
    // the notification queue. Never let queue pressure lose command state.
    using OutcomeKind = TeslaBleCommandOutcome::Kind;
    if (type == EventType::PairApproved && authenticated && correlated) {
        r->command_outcome.pair_approved(r->pair_command, request);
        return;
    }
    if (type == EventType::KeyPresence) {
        r->command_outcome.publish(OutcomeKind::KeyLoaded, soc == 1.0f, nullptr, std::isfinite(soc), soc == 2.0f);
        return;
    }
    if (type == EventType::KeyRemoved || type == EventType::PairStarted) {
        r->terminal_error[0] = '\0';
        r->command_outcome.publish(type == EventType::KeyRemoved ? OutcomeKind::KeyRemoved : OutcomeKind::PairStarted,
                                   type == EventType::PairStarted);
        return;
    }
    if (type == EventType::Error) {
        if (message) { strlcpy(r->terminal_error, message, sizeof(r->terminal_error)); }
        const auto presence = r->storage->private_key_presence();
        r->terminal_error_order = r->command_outcome.publish(OutcomeKind::Error, presence == TeslaBleStorageAdapter::KeyPresence::Present, message,
                                   presence != TeslaBleStorageAdapter::KeyPresence::Unknown,
                                   presence == TeslaBleStorageAdapter::KeyPresence::CleanupRequired);
        return;
    }
    event.type = type;
    event.order = r->command_outcome.next_order();
    if (!event.order) { r->stop_requested.store(true); return; }
    event.runtime_epoch = r->epoch;
    event.request = request;
    event.authenticated = authenticated;
    event.correlated = correlated;
    event.soc = soc;
    if (message != nullptr) {
        strlcpy(event.message, message, sizeof(event.message));
        if (type == EventType::Error) { strlcpy(r->terminal_error, message, sizeof(r->terminal_error)); }
    }
    if (xQueueSend(r->queues.events(), &event, 0) != pdTRUE && type == EventType::Soc) {
        // Not reliable delivery: abandon this request and publish a coalesced
        // diagnostic outside the full queue. No SoC/authentication success ACK.
        r->session.disarm(request);
        r->policy.on_transport_disconnected();
        post_event(EventType::Error, "Tesla SoC result not delivered: event queue full; retry required");
    }
}

void TeslaVehicle::publish_error(const char *message)
{
    strlcpy(pending_error, message ? message : "", sizeof(pending_error));
    error_pending = true;
    flush_error();
}

void TeslaVehicle::flush_error()
{
    if (error_pending && state.get("last_error")->updateStringChecked(pending_error)) { error_pending = false; }
}

void TeslaVehicle::loop()
{
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
    tesla_ble_bench_smoke_loop();
    tesla_ble_bench_discovery.supervise();
#endif
    flush_error();
    const bool changed = runtime_update_requested.exchange(false);
    if (changed) {
        start_attempted = false;
    }
    Runtime *r = runtime_owner.get();
    if (ev.session_exhausted()) { runtime_owner.require_restart(); }
    if (runtime_owner.restart_required()) { state.get("restart_required")->updateBool(true); }
    if (r && (r->command_outcome.publication_exhausted() || r->session.exhausted() ||
              r->identity_exhausted.load() || ev.session_exhausted())) {
        runtime_owner.require_restart();
        r->stop_requested.store(true);
        state.get("restart_required")->updateBool(true);
        if (!r->supervision_failed) {
            publish_error("Tesla identity/publication sequence exhausted; reboot required");
        }
    }
    if (r != nullptr && (r->operation.supervise(millis()) || r->retirement_operation.supervise(millis()) ||
                         r->worker_watch.expired(millis(), 30000))) {
        // Detect/quarantine only: this cannot interrupt an SDK queue/future wait.
        runtime_owner.require_restart();
        r->supervision_failed = true;
        r->stop_requested.store(true);
        state.get("restart_required")->updateBool(true);
        publish_error("Tesla BLE worker/submission timed out; ownership retained; reboot required");
    }
    if (r != nullptr && (changed || (!config.get("enable")->asBool() && !r->storage_only) ||
                         r->stop_requested.load() || runtime_owner.is_stopping())) {

        stop_runtime();
        return;
    }
    if (r == nullptr && config.get("enable")->asBool() && !start_attempted && !runtime_owner.restart_required()) {
        start_runtime();
        r = runtime_owner.get();
    }
    if (r == nullptr) {
        return;
    }
    const auto generation = ev.get_session_generation();
    const int index = ev.get_active_ev_index();
    r->session.publish({generation, index, ev.get_active_ev_mac()});
    process_module_events();
}

void TeslaVehicle::process_module_events()
{
    Runtime *r = runtime_owner.get();
    ModuleEvent event;
    for (size_t batch = 0; r != nullptr && batch < 8; ++batch) {
        if (r->deferred_soc) { event = *r->deferred_soc; r->deferred_soc.reset(); }
        else { r->deferred_attempts = 0; if (xQueueReceive(r->queues.events(), &event, 0) != pdTRUE) { break; } }
        if (!runtime_owner.accepts_event(event.runtime_epoch) || !accepts_commands()) {
            continue;
        }
        switch (event.type) {
        case EventType::PairApproved:
            // Authenticated status proves possession/approval, not SoC. Keep the
            // same final SLAC/runtime/request gate without invoking ev.set_soc.
            if (r->session.consume(event.request,
                    {ev.get_session_generation(), ev.get_active_ev_index(), ev.get_active_ev_mac()},
                    r->configured_ev_index, event.authenticated, event.correlated, 50,
                    [](float) {}) && tesla_ble_publication_newer(event.order, r->pairing_order)) {
                r->pairing_order = event.order;
                // Correlated authenticated possession may precede main's first
                // coalesced key-presence notification. Do not lose that proof.
                if (!pairing_policy.key_present()) { pairing_policy.on_key_loaded(true); }
                pairing_policy.on_authenticated_response();
                state.get("key_present")->updateBool(pairing_policy.key_present());
                state.get("key_presence")->updateString("present");
                state.get("paired")->updateBool(pairing_policy.paired());
                state.get("pairing_pending")->updateBool(pairing_policy.pairing_pending());
            }
            break;
        case EventType::KeyPresence:
            pairing_policy.on_key_loaded(event.soc == 1.0f);
            state.get("key_present")->updateBool(pairing_policy.key_present());
            break;
        case EventType::Connected:
        case EventType::Disconnected:
            if (tesla_ble_publication_newer(event.order, r->connection_order)) {
                r->connection_order = event.order;
                state.get("connected")->updateBool(r->connected.load());
            }
            if (event.type == EventType::Connected && tesla_ble_publication_newer(event.order, r->error_order)) {
                r->error_order = event.order;
                publish_error("");
            }
            break;
        case EventType::Soc: {
            bool retry = false;
            const auto forward = r->command_outcome.forward_state();
            if (forward == TeslaBleCommandOutcome::Forward::Contended) {
                retry = true;
            } else if (forward != TeslaBleCommandOutcome::Forward::Available) {
                // Worker owns scoped session invalidation, never wait in main.
                r->deferred_attempts = 0;
                break;
            } else if (r->session.consume(event.request,
                    {ev.get_session_generation(), ev.get_active_ev_index(), ev.get_active_ev_mac()},
                    r->configured_ev_index, event.authenticated, event.correlated, event.soc,
                    [](float soc) { ev.set_soc(soc); }, &retry)) {
                ev.session_updated(EVDataSource::Tesla);
                if (tesla_ble_publication_newer(event.order, r->pairing_order)) {
                    r->pairing_order = event.order;
                    if (!pairing_policy.key_present()) { pairing_policy.on_key_loaded(true); }
                    pairing_policy.on_authenticated_response();
                    state.get("key_present")->updateBool(pairing_policy.key_present());
                    state.get("key_presence")->updateString("present");
                    state.get("paired")->updateBool(pairing_policy.paired());
                    state.get("pairing_pending")->updateBool(pairing_policy.pairing_pending());
                }
                state.get("last_soc")->updateFloat(event.soc);
                state.get("last_update")->updateUptime(now_us());
                if (tesla_ble_publication_newer(event.order, r->error_order)) {
                    r->error_order = event.order;
                    publish_error("");
                }
            }
            if (retry) {
                if (++r->deferred_attempts < 8) { r->deferred_soc = event; }
                else {
                    // Do not block main disarming a held session lock. Close
                    // admission and retain ownership for worker stop instead.
                    runtime_owner.require_restart(); r->stop_requested.store(true);
                    state.get("restart_required")->updateBool(true);
                    if (tesla_ble_publication_newer(event.order, r->error_order)) {
                        r->error_order = event.order;
                        publish_error("Tesla result consumption unavailable; reboot required");
                    }
                }
                batch = 8; // One retry per main pass, never spin here.
            }
            break;
        }
        case EventType::Error:
            pairing_policy.on_pairing_error();
            publish_error(event.message);
            state.get("pairing_pending")->updateBool(pairing_policy.pairing_pending());
            break;
        case EventType::PairStarted:
            pairing_policy.on_pair_request_sent();
            state.get("key_present")->updateBool(pairing_policy.key_present());
            state.get("paired")->updateBool(pairing_policy.paired());
            state.get("pairing_pending")->updateBool(pairing_policy.pairing_pending());
            publish_error("");
            break;
        case EventType::KeyRemoved:
            pairing_policy.on_key_removed();
            state.get("key_present")->updateBool(pairing_policy.key_present());
            state.get("paired")->updateBool(pairing_policy.paired());
            state.get("connected")->updateBool(false);
            state.get("pairing_pending")->updateBool(pairing_policy.pairing_pending());
            break;
        }
    }
    if (r == nullptr || !runtime_owner.accepts_event(r->epoch) || !accepts_commands()) { return; }
    if (const auto outcome = r->command_outcome.take()) {
        using Kind = TeslaBleCommandOutcome::Kind;
        if (outcome->kind == Kind::PairApproved) {
            if (r->session.consume(outcome->request,
                    {ev.get_session_generation(), ev.get_active_ev_index(), ev.get_active_ev_mac()},
                    r->configured_ev_index, true, true, 50, [](float) {}) &&
                tesla_ble_publication_newer(outcome->order, r->pairing_order)) {
                r->pairing_order = outcome->order;
                if (!pairing_policy.key_present()) { pairing_policy.on_key_loaded(true); }
                pairing_policy.on_authenticated_response();
                state.get("key_present")->updateBool(pairing_policy.key_present());
                state.get("key_presence")->updateString("present");
                state.get("paired")->updateBool(pairing_policy.paired());
                state.get("pairing_pending")->updateBool(pairing_policy.pairing_pending());
            }
            r->pair_approval_waiting.store(false);
            publish_pairing_state();
            return;
        }
        if (outcome->kind == Kind::KeyRemoved && tesla_ble_publication_newer(outcome->order, r->connection_order)) {
            r->connection_order = outcome->order;
            state.get("connected")->updateBool(false);
        }
        if (tesla_ble_publication_newer(outcome->order, r->pairing_order)) {
            r->pairing_order = outcome->order;
            if (outcome->kind == Kind::KeyRemoved) {
                pairing_policy.on_key_removed();
            } else if (outcome->kind == Kind::PairStarted) {
                pairing_policy.on_pair_request_sent();
            } else {
                pairing_policy.on_key_loaded(outcome->key_present);
            }
            state.get("key_present")->updateBool(pairing_policy.key_present());
            state.get("paired")->updateBool(pairing_policy.paired());
            state.get("pairing_pending")->updateBool(pairing_policy.pairing_pending());
            tesla_ble_publish_presence(state.get("key_presence"), outcome->key_present, outcome->key_known, outcome->cleanup_required);
        }
        if (tesla_ble_publication_newer(outcome->order, r->error_order)) {
            r->error_order = outcome->order;
            publish_error(outcome->error);
        }
    }
    publish_pairing_state();
}
