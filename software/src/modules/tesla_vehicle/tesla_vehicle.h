#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <functional>
#include "bluetooth_support.h"
#ifdef TESLA_BLE_MODULE_HOST_TEST
#include "module_platform.h"
#else
#if WARP_BLUETOOTH_COMPILED && !TESLA_BLE_USE_NIMBLE
#include <esp_gattc_api.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "config.h"
#include "module.h"
#endif
#include <tesla_clean/vehicle.hpp>
#include "tesla_ble_clean_platform.h"
#include "tesla_ble_soc_policy.h"
#include "tesla_ble_pairing_policy.h"
#include "tesla_ble_runtime.h"
#include "tesla_ble_result.h"
#include "tesla_ble_operation.h"
#include "tesla_ble_command_outcome.h"
#include "tesla_ble_storage.h"
#include "tesla_ble_transport.h"
#include "bluetooth_config_store.h"
#include "bluetooth_master_bench.h"
#include "bluetooth_lifecycle.h"

class TeslaBleStorageAdapter;
class TeslaBleTransportAdapter;

class TeslaVehicle final : public IModule
{
public:
    TeslaVehicle() = default;

    void pre_setup() override;
    void setup() override;
    void register_urls() override;
    void loop() override;

    bool transport_write(const std::vector<uint8_t> &data) { return transport_write_bytes(data.data(), data.size()); }
    bool transport_write_bytes(const uint8_t *data, size_t size);
    void transport_disconnect();
    bool transport_owns(uint64_t generation) const;
    bool transport_recover_frame(uint64_t generation);


private:
    friend struct TeslaBleModuleTest;
    struct Runtime;
    enum class EventType : uint8_t {
        Connected,
        Disconnected,
        Soc,
        Error,
        PairStarted,
        PairApproved,
        KeyPresence,
        KeyRemoved
    };

    struct ModuleEvent {
        EventType type = EventType::Error;
        uint64_t runtime_epoch = 0;
        uint64_t order = 0;
        TeslaBleRequestIdentity request;
        bool authenticated = false;
        bool correlated = false;
        float soc = NAN;
        char message[96] = {};
    };

    struct RxFragment {
        uint64_t connection_generation = 0;
        uint16_t length = 0;
        uint8_t data[512] = {};
    };

    static void task_entry(void *context);
    void task_loop();
    bool ensure_connected();
    void disconnect_internal(bool cancel_pair = true);
    void process_rx();
    void process_disconnect();
    void post_event(EventType type, const char *message = nullptr, float soc = NAN,
                    TeslaBleRequestIdentity request = {}, bool authenticated = false, bool correlated = false);
    void request_soc(bool manual);
    void process_commands();
    void process_pair_verification();
    void cancel_pairing(const char *message, uint64_t command);
    void publish_pairing_state();
    void process_module_events();
    bool check_worker_exhaustion();
    void publish_error(const char *message);
    void flush_error();
    void update_active_vehicle();
    bool start_runtime(bool storage_only = false);
    bool create_vehicle();
    bool accepts_commands() const;
    void stop_runtime();
    bool release_bluetooth(Runtime &runtime);
    static void teardown_entry(void *context);

    enum GattOperation : uint8_t { Register = 1, Open, Discover, Cache, Subscribe, Descriptor, Write, Close };
    bool gatt_operation(GattOperation kind, uint16_t handle, tesla_warp::FunctionRef<bool()> submit);
#if TESLA_BLE_USE_NIMBLE
    static void gatt_callback(const TeslaNimbleEvent &event);
#elif WARP_BLUETOOTH_COMPILED
    static void gatt_callback(esp_gattc_cb_event_t event, esp_gatt_if_t interface, esp_ble_gattc_cb_param_t *param);
#endif
    static std::atomic<TeslaVehicle *> gatt_owner;

    ConfigRoot empty_command;
    // This module remains the SOLE controller/host lifecycle owner. These are
    // separate from the legacy Tesla service config and never grant VIN access.
    ConfigRoot bluetooth_config, bluetooth_update, bluetooth_state;
    // Path/transaction work buffers live on the caller stack, never in BSS.
    // A failed close's quarantine remains permanent across those transactions.
    bool bluetooth_storage_quarantined = false;
    bool load_bluetooth(bool &desired);
    bool persist_bluetooth(bool desired);
    bool bluetooth_boot_enabled = false;
    bool bluetooth_commit_ok = true;
    void publish_bluetooth_state();
    void register_bluetooth_urls();
    bool save_bluetooth(bool desired, String &errmsg);
    bool restart_bluetooth(String &errmsg);
    BluetoothLifecycle bluetooth;
    bool bluetooth_restart_pending = false;
    bool service_start_requested = false; // Explicit action only; never restored.
    bool prepare_tesla();
    void register_tesla_urls();
    void request_pair(int index, String &errmsg);
    void request_poll(int index, String &errmsg);
#if TESLA_BLE_MANUAL_VEHICLE_UI
    void publish_discovery();
    bool discovery_allowed() const;
    bool fresh_binding(int index, uint64_t mac, const char *vin, const char *address, uint8_t type, uint32_t revision);
    bool runtime_binding_matches();
    Config make_profile();
    String validate_profiles(Config &update, ConfigSource source);
    Config *find_profile(int index) const;
    bool select_profile(int index, bool deleting, String &errmsg);
    bool live_profile(int index) const;
    void apply_profile(); // Worker only, no connection or generation of keys.
    void publish_profile(); // Main only, consumes worker acknowledgement.
    bool active_identity_matches(const TeslaBleIdentity &identity) const;
    void refresh_live_session(); // Tesla worker task; snapshot serializes with main publication.
    void guided_command(String &errmsg);
    void publish_guidance();
    bool guided_current() const;
    void clear_guided();
#endif
#if TESLA_BLE_R13_MASTER_BENCH
    BluetoothMasterBench master_bench;
    ConfigRoot bench_state;
    bool bench_restart_pending = false;
    char bench_line[16]{};
    unsigned bench_line_size = 0;
    bool bench_line_overflow = false;
    void bench_serial();
    void bench_emit(const char *request, bool ok);
#endif

    // Every worker-owned allocation and request flag lives in this optional
    // RAII aggregate, not in the permanent module. Vehicle's adapter pointers
    // alias the embedded adapters: no independent shared_ptr control blocks.
    struct Runtime {
        explicit Runtime(TeslaVehicle *owner)
            : transport_adapter(owner),
              storage(std::shared_ptr<TeslaBleStorageAdapter>{}, &storage_adapter),
              transport(std::shared_ptr<TeslaBleTransportAdapter>{}, &transport_adapter) {}

        TeslaBleQueues<QueueHandle_t, vQueueDelete> queues;
        TaskHandle_t task_handle = nullptr;
        TaskHandle_t teardown_handle = nullptr;
        TeslaBleTeardown teardown;
        TeslaBleShutdownResult shutdown_result;
        TeslaBleCommandOutcome command_outcome;
        // Main-only field versions: connection events must not suppress an
        // older key transition, and a deferred outcome cannot undo newer SoC.
        uint64_t pairing_order = 0, error_order = 0, connection_order = 0;
        std::optional<ModuleEvent> deferred_soc; // Main-only, no event loss on try-lock contention.
        bool shutdown_presence_applied = false; // Main-only immutable ACK snapshot.
        unsigned deferred_attempts = 0; // Bounded, one retry per main pass.
        std::atomic<bool> identity_exhausted{false};
        uint64_t terminal_error_order = 0; // Worker-only, copied with shutdown ACK.
        char terminal_error[96] = {};
        char teardown_error[96] = {};
        TeslaBleStorageAdapter storage_adapter;
        TeslaBleTransportAdapter transport_adapter;
        std::shared_ptr<TeslaBleStorageAdapter> storage;
        std::shared_ptr<TeslaBleTransportAdapter> transport;
        std::unique_ptr<tesla_clean::IdentityStore> keys;
        std::array<uint8_t, 16> routing_address{};
        std::unique_ptr<tesla_clean::Vehicle> vehicle;
        TeslaBleOperation operation;
        TeslaBleOperation retirement_operation;
        TeslaBleWorkWatch worker_watch;
#if WARP_BLUETOOTH_COMPILED && !TESLA_BLE_USE_NIMBLE
        std::atomic<esp_gatt_if_t> gatt_if{ESP_GATT_IF_NONE};
#endif
        std::atomic<uint16_t> gatt_connection{UINT16_MAX};
        std::atomic<bool> link_open{false};
        std::atomic<bool> closing_link{false};
        std::atomic<unsigned> close_events{0}; // CLOSE=1, DISCONNECT=2.
        std::atomic<bool> discovery_ready{false};
        uint16_t write_handle = 0;
        std::atomic<uint16_t> notify_handle{0};
        uint8_t remote_address[6] = {};
        tesla_warp::FixedVin vin;
        std::atomic<int> configured_ev_index{-1};
#if TESLA_BLE_MANUAL_VEHICLE_UI
        struct ProfileRequest {
            int index = -1;
            uint64_t mac = 0;
            char vin[18]{}, address[18]{};
            uint32_t discovery_revision = 0;
            uint8_t address_type = 0;
            bool deleting = false;
        } profile_request;
        std::atomic<bool> profile_pending{false}, profile_ready{false};
        std::atomic<bool> authorized{false};
        std::atomic<bool> profile_key_present{false};
        std::atomic<bool> profile_actionable{false};
        uint64_t profile_mac = 0; // Immutable between acknowledged switches.
        uint32_t binding_revision = 0; // Exact scan chosen, never replaced by a newer result.
        uint8_t remote_address_type = 0;
        bool deleting_profile = false; // Worker-only local erase scope.
#endif
        uint32_t poll_interval_seconds = 300;
        bool wake_once = false;
        std::atomic<bool> pair_requested{false};
        std::atomic<bool> poll_requested{false};
        // HTTP acceptance through terminal worker outcome, not HTTP latency.
        std::atomic<bool> manual_poll_pending{false};
        std::atomic<bool> forget_requested{false};
        std::atomic<bool> stop_requested{false};
        std::atomic<bool> connected{false};
        TeslaBlePendingDisconnect pending_disconnect;
        std::atomic<bool> worker_stopped{false};
        std::atomic<bool> ble_ready{false};

        std::atomic<uint64_t> connection_generation{0};
        bool advance_connection() {
            auto old = connection_generation.load();
            while (old != UINT64_MAX) {
                if (connection_generation.compare_exchange_weak(old, old + 1)) { return true; }
            }
            identity_exhausted.store(true); stop_requested.store(true); return false;
        }
        std::atomic<uint64_t> physical_generation{0};
        uint64_t epoch = 0;
        bool controller_used = false; // Read by main only after worker ACK.
        bool storage_only = false;
        bool service_enabled = false;
        bool supervision_failed = false; // Main-only terminal latch.
        TeslaBleSession session;
        std::atomic<uint64_t> bound_vehicle_mac{0};
        TeslaBleSocPolicy policy;
        TeslaBleIdentity observed_identity{UINT64_MAX, -1, 0};
        TeslaBleRequestIdentity in_flight;
        uint32_t in_flight_started_ms = 0;
        uint32_t next_poll_ms = 0;
        bool pair_sending = false, pair_verifying = false, pair_status_active = false;
        std::atomic<bool> pair_approval_waiting{false};
        uint32_t pair_started_ms = 0, pair_next_ms = 0;
        TeslaBleRequestIdentity pair_request;
        uint64_t pair_command = 0; // Worker-owned, not a UI/publication latch.
    };

    struct Tesla {
        ConfigRoot config, state;
#if TESLA_BLE_MANUAL_VEHICLE_UI
        Config profile;
        ConfigRoot profiles, profile_command;
        bool legacy_blocked = false;
        int selected_index = -1; // Main's admitted profile, not a second EV registry.
        int published_index = -1;
        Config discovery_target;
        ConfigRoot discovery_state, scan_command;
        uint32_t discovery_revision=UINT32_MAX;
        ConfigRoot guided_command, guidance;
        Config guidance_profile;
        // Main-task-only consent lease; worker never reads Config trees.
        struct Guided {
            char request[33]{};
            TeslaBleIdentity identity{};
            uint64_t runtime_epoch = 0;
            uint32_t config_revision = 0, started_ms = 0;
            bool pair = false;
        } guided;
        uint32_t config_revision = 0;
        uint32_t reset_discovery_revision[TeslaBleStorageAdapter::MAX_PROFILES]{};
        bool guidance_refresh = true;
        uint64_t guidance_macs[TeslaBleStorageAdapter::MAX_PROFILES]{};
        int guidance_key_signature = -1;
#endif
        TeslaBleRuntimeOwner<Runtime> runtime_owner;
        TeslaBlePairingPolicy pairing_policy;
        std::atomic<bool> runtime_update_requested{false};
        TeslaBleCallbackGate callback_gate;
        bool start_attempted = false;
        // Coalescing retains failed publication beyond Runtime lifetime.
        char pending_error[96] = {};
        bool error_pending = false;
    };
#if TESLA_BLE_R13_MASTER_BENCH
    Tesla *const tesla = nullptr; // No Tesla destructor/protocol closure in peer-free bench.
#else
    std::unique_ptr<Tesla> tesla; // OFF never constructs any Tesla object.
#endif
};
