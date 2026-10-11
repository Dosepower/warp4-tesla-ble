#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <functional>
#ifdef TESLA_BLE_MODULE_HOST_TEST
#include "module_platform.h"
#else
#include <esp_gattc_api.h>
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
    void disconnect_internal();
    void process_rx();
    void process_disconnect();
    void post_event(EventType type, const char *message = nullptr, float soc = NAN,
                    TeslaBleRequestIdentity request = {}, bool authenticated = false, bool correlated = false);
    void request_soc(bool manual);
    void process_commands();
    void process_pair_verification();
    void process_module_events();
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
    static void gatt_callback(esp_gattc_cb_event_t event, esp_gatt_if_t interface, esp_ble_gattc_cb_param_t *param);
    static std::atomic<TeslaVehicle *> gatt_owner;

    ConfigRoot config;
    ConfigRoot state;
    ConfigRoot empty_command;

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
        std::atomic<esp_gatt_if_t> gatt_if{ESP_GATT_IF_NONE};
        std::atomic<uint16_t> gatt_connection{UINT16_MAX};
        std::atomic<bool> link_open{false};
        std::atomic<bool> discovery_ready{false};
        uint16_t write_handle = 0;
        std::atomic<uint16_t> notify_handle{0};
        uint8_t remote_address[6] = {};
        tesla_warp::FixedVin vin;
        int configured_ev_index = -1;
        uint32_t poll_interval_seconds = 300;
        bool wake_once = false;
        std::atomic<bool> pair_requested{false};
        std::atomic<bool> poll_requested{false};
        std::atomic<bool> forget_requested{false};
        std::atomic<bool> stop_requested{false};
        std::atomic<bool> connected{false};
        TeslaBlePendingDisconnect pending_disconnect;
        std::atomic<bool> worker_stopped{false};
        std::atomic<bool> ble_ready{false};

        std::atomic<uint64_t> connection_generation{0};
        std::atomic<uint64_t> physical_generation{0};
        uint64_t epoch = 0;
        bool controller_used = false; // Read by main only after worker ACK.
        bool storage_only = false;
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
    };

    TeslaBleRuntimeOwner<Runtime> runtime_owner;
    TeslaBlePairingPolicy pairing_policy;
    std::atomic<bool> runtime_update_requested{false};
    TeslaBleCallbackGate callback_gate;
    bool start_attempted = false;
    // Main-only coalescing retains failed publication beyond Runtime lifetime.
    char pending_error[96] = {};
    bool error_pending = false;
};
