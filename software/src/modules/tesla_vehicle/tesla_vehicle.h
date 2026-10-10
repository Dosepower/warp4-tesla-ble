#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <BLEClient.h>
#include <BLERemoteCharacteristic.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <vehicle.h>

#include "config.h"
#include "module.h"
#include "tesla_ble_soc_policy.h"
#include "tesla_ble_pairing_policy.h"

class TeslaBleStorageAdapter;
class TeslaBleTransportAdapter;

class TeslaVehicle final : public IModule, public BLEClientCallbacks
{
public:
    TeslaVehicle() = default;

    void pre_setup() override;
    void setup() override;
    void register_urls() override;
    void loop() override;

    bool transport_write(const std::vector<uint8_t> &data);
    void transport_disconnect();

    void onConnect(BLEClient *client) override;
    void onDisconnect(BLEClient *client) override;

private:
    enum class EventType : uint8_t {
        Connected,
        Disconnected,
        Soc,
        Error,
        PairStarted,
        KeyRemoved,
        Stopped
    };

    struct ModuleEvent {
        EventType type = EventType::Error;
        uint32_t generation = 0;
        int8_t ev_index = -1;
        float soc = NAN;
        char message[96] = {};
    };

    struct RxFragment {
        uint16_t length = 0;
        uint8_t data[512] = {};
    };

    static void task_entry(void *context);
    void task_loop();
    bool ensure_connected();
    void disconnect_internal();
    void process_rx();
    void post_event(EventType type, const char *message = nullptr, float soc = NAN,
                    uint32_t generation = 0, int ev_index = -1);
    void request_soc(bool manual);
    void process_commands();
    void process_module_events();
    void update_active_vehicle();
    void start_task();

    static void notification_callback(BLERemoteCharacteristic *characteristic,
                                      uint8_t *data, size_t length, bool is_notify);

    ConfigRoot config;
    ConfigRoot state;
    ConfigRoot empty_command;

    QueueHandle_t rx_queue = nullptr;
    QueueHandle_t event_queue = nullptr;
    TaskHandle_t task_handle = nullptr;

    std::shared_ptr<TeslaBleStorageAdapter> storage;
    std::shared_ptr<TeslaBleTransportAdapter> transport;
    std::unique_ptr<TeslaBLE::Vehicle> vehicle;

    BLEClient *ble_client = nullptr;
    BLERemoteCharacteristic *write_characteristic = nullptr;
    BLERemoteCharacteristic *notify_characteristic = nullptr;

    std::string vin;
    std::string ble_address;
    int configured_ev_index = -1;
    uint32_t poll_interval_seconds = 300;
    bool wake_once = false;

    std::atomic<bool> pair_requested{false};
    std::atomic<bool> poll_requested{false};
    std::atomic<bool> forget_requested{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> connected{false};
    std::atomic<bool> transport_disconnected{false};
    std::atomic<int> active_ev_index{-1};
    std::atomic<uint32_t> active_generation{0};
    std::atomic<uint64_t> active_vehicle_mac{0};
    std::atomic<uint64_t> bound_vehicle_mac{0};

    TeslaBleSocPolicy policy;
    TeslaBlePairingPolicy pairing_policy;
    uint32_t observed_generation = UINT32_MAX;
    int observed_ev_index = -1;
    uint32_t in_flight_generation = 0;
    int in_flight_ev_index = -1;
    uint32_t in_flight_started_ms = 0;
    uint32_t next_poll_ms = 0;

    static TeslaVehicle *instance;
};
