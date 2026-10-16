#pragma once
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "tesla_ble_bench_pair_disk.h"

class TeslaBleBenchDiscovery final
{
public:
    static constexpr unsigned MIN_INTERNAL8_FREE=8192, MIN_INTERNAL8_LARGEST=4096, MIN_INTERNAL8_MIN=8192, MIN_WORKER_WATERMARK=2048;
    static constexpr unsigned PAIR_WORKER_BYTES=8192, REHEARSAL_WORKER_BYTES=8192, DISCOVERY_WORKER_BYTES=4096;
    struct Target { uint8_t address[6]{}; uint8_t address_type = 0; int rssi = 0; char local_name[19]{}; bool connectable = false; };
    struct ResourceSample { const char *stage="none", *phase="none"; int code=0; unsigned internal8_free=0,internal8_largest=0,internal8_min=0,stack_min_bytes=0; };
    struct Report {
        const char *action = "none", *status = "idle", *stage = "idle";
        const char *failure_stage = "none", *teardown_stage = "none";
        int failure_code = 0, teardown_code = 0, task_create_code = 0;
        bool worker_active = false, teardown_complete = false, restart_required = false, transport_ready = false;
        bool scan_attempted = false, probe_attempted = false;
        unsigned target_count = 0, dropped_targets = 0;
        int selected_target = -1;
        bool security_request_seen = false;
        int security_reply_code = 0;
        int ble_bond_count = -1; // -1 until queried; a negative query result is also retained.
        Target targets[8]{};
        const char *pair_state="no_key";
        bool vin_bound=false, local_key_present=false, request_may_have_reached_tesla=false, tesla_removal_user_claim=false;
        bool pair_start_attempted=false, pair_verify_attempted=false, pair_delete_attempted=false;
        bool pair_wait_expired=false;
        bool pair_rehearsal_attempted=false;
        unsigned resource_samples_dropped=0;
        bool resource_gate_failed=false;
        unsigned resource_gate_mask=0, worker_stack_bytes=0;
        ResourceSample resource_failure; // First breach, never replaced by teardown recovery.
        uint32_t pair_information=0;
        char public_key_hex[131]{};
        uint16_t write_handle = 0, notify_handle = 0, cccd_handle = 0;
        unsigned stack_min_bytes = 0;
        bool measurements_valid = false;
        unsigned exec_free = 0, exec_largest = 0, exec_min = 0;
        unsigned internal8_free = 0, internal8_largest = 0, internal8_min = 0;
        unsigned psram_free = 0, psram_largest = 0, psram_min = 0;
    };
    int request_scan(); // HTTP-style result: 202/409/503. No retries per boot.
    int request_probe(unsigned selected); // Explicit fresh discovery index only.
    int request_pair_prepare(unsigned selected,const char *vin);
    int request_pair_start();
    int request_pair_verify();
    int request_pair_rehearsal();
    bool resource_sample(unsigned index,ResourceSample &);
    int request_pair_delete();
    int confirm_pair_revoke();
    Report snapshot();
    void supervise();
private:
    enum Event { Params, ScanStart, ScanDone, ScanStop, Register, Open, Discover, Search, Close, Cancel, Unregister, Subscribe, Descriptor, Write, EventCount };
    struct Outcome { bool seen = false; int code = 0; };
    static constexpr uint16_t APP_ID = 0x424d;
    static void task_entry(void *);
    static void gap_callback(esp_gap_ble_cb_event_t, esp_ble_gap_cb_param_t *);
    static void gatt_callback(esp_gattc_cb_event_t, esp_gatt_if_t, esp_ble_gattc_cb_param_t *);
    int launch();
    void run();
    bool stage(const char *, bool teardown = false);
    bool result(const char *, int, bool teardown = false);
    bool wait(Event, const char *, unsigned ms = 2000, bool teardown = false);
    bool measure(const char *, const char *, int, bool gate);
    void scan();
    void probe();
    void pair_transport();
    void drain_pair_rx();
    static bool pair_send(void *,tesla_clean::Bytes);
    static bool pair_write(void *,tesla_clean::Bytes,uint64_t);
    static bool pair_owns(void *,uint64_t);
    static void pair_frame(void *,const tesla_clean::Frame &);
    bool pair_action() const { return !strcmp(report.action,"pair_start") || !strcmp(report.action,"pair_verify"); }
    BenchPairDisk pair_disk;
    std::unique_ptr<BenchPairing> pairing;
    struct PairRx { uint8_t incoming[2048]{}, detached[2048]{}; size_t length=0; uint32_t received_ms=0, detached_ms=0; tesla_clean::FrameReceiver frames; tesla_clean::Frame completed; ResourceSample samples[32]{}; unsigned sample_count=0; };
    std::unique_ptr<PairRx> pair_rx;
    void observe(const uint8_t *, size_t, size_t, const uint8_t *, uint8_t, int, esp_ble_evt_type_t);
    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    Report report;
    Outcome events[EventCount]{};
    bool accept_events = false, scanning = false, opening = false, link_open = false;
    bool timed_out = false, teardown_stage = false, security_requested = false;
    int security_code = 0;
    esp_gatt_if_t interface = ESP_GATT_IF_NONE;
    uint16_t connection = 0;
    TickType_t stage_started = 0, attempt_started = 0, scan_finished = 0;
    unsigned stage_budget_ms = 5000;
};
extern TeslaBleBenchDiscovery tesla_ble_bench_discovery;
void tesla_ble_bench_discovery_register_urls();
#endif
