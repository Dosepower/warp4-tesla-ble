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

// Pair-trace interleaving points (1 reader after cause load, 2 writer admitted,
// 3 writer claimed). Empty in firmware; host tests may pause a task there.
#ifndef TESLA_PAIR_TRACE_POINT
#define TESLA_PAIR_TRACE_POINT(point) do {} while (0)
#endif

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

    // RX transport queue element: one fixed chunk of a notification byte stream.
    // Framing is stream based (FrameReceiver), so splitting a notification into
    // ordered chunks of the same connection generation is semantically neutral.
    // Bounded intake contract. Every notification of 1..RX_NOTIFICATION_MAX bytes
    // is admitted while space remains (no valid size is filtered); n bytes cost
    // ceil(n / RX_CHUNK_BYTES) slots because each notification starts a new
    // chunk, and are admitted all-or-nothing. GUARANTEED without any consumer
    // progress (one synchronous submit or one scheduler yield): any notification
    // sequence of at most RX_GUARANTEE_BYTES stream bytes (one maximum framed
    // message 1024+2 plus 256 accompanying bytes) whose notifications carry at
    // least RX_FULL_PAYLOAD_MIN bytes (ATT minimum MTU 23 - 3), except at most
    // RX_GUARANTEE_SHORT shorter ones (Vehicle defers at most 4 complete frames
    // per transmission, plus one frame in progress). Lemma (checked for every
    // admissible size in tesla_vehicle.cpp): ceil(n/22) <= 2n/23 for n >= 20,
    // tight at n = 23; hence such a burst needs at most
    // floor(2 * RX_GUARANTEE_BYTES / 23) + RX_GUARANTEE_SHORT = 116 slots.
    // Tiny notifications are bounded by slots (116 one-byte notifications), not
    // promised unbounded. Beyond capacity the session is invalidated (fail-safe).
    // Queue data: 116 x 32 = 3712 bytes (8 x 528 = 4224 before the RX fix).
    static constexpr size_t RX_CHUNK_BYTES = 22;
    static constexpr size_t RX_NOTIFICATION_MAX = 512; // ATT attribute maximum; unchanged admission bound.
    static constexpr size_t RX_FULL_PAYLOAD_MIN = 20;
    static constexpr size_t RX_GUARANTEE_BYTES = 1024 + 2 + 256;
    static constexpr size_t RX_GUARANTEE_SHORT = 5;
    static constexpr size_t RX_QUEUE_DEPTH = 2 * RX_GUARANTEE_BYTES / 23 + RX_GUARANTEE_SHORT;
    struct RxFragment {
        uint64_t connection_generation = 0;
        uint16_t length = 0;
        uint8_t data[RX_CHUNK_BYTES] = {};
    };
    static_assert(sizeof(RxFragment) == 32, "RX queue element budget");
    static_assert(RX_QUEUE_DEPTH == 116, "RX queue depth derived from the fragmentation contract");
    // Producer side (BLE host callback only): all chunks of one notification or none.
    static bool enqueue_rx(QueueHandle_t queue, uint64_t generation, const uint8_t *data, size_t size, uint32_t *peak = nullptr);
    size_t drain_rx();
    void service_rx_during_write(uint64_t generation, const TeslaBleIdentity &identity);

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
    void cancel_pairing(const char *message, uint64_t command, TeslaBleCommandOutcome::PairOutcome reason = TeslaBleCommandOutcome::PairOutcome::InternalFailure);
    void queue_pair_trace(uint64_t command, const char *outcome); // worker-only, nonsecret
    void flush_pair_trace(); // worker-only, shallow stack
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
    // worker=true only from the Tesla worker task (never main, never under the
    // outcome/session/discovery locks): waits briefly for main's zero-wait
    // publication instead of treating lock contention as revoked authority.
    bool runtime_binding_matches(bool worker = false);
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
        bool pair_terminal_fault = false; // Worker-owned; first pair error survives cleanup.
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
        // Nonsecret event code paired with operation generation; callback writes,
        // worker consumes. 1=security, 2=RX overflow. No identities or payloads.
        std::atomic<uint64_t> transport_cancel_event{0};
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
        std::atomic<TeslaBleStorageAdapter::KeyPresence> profile_key_presence{TeslaBleStorageAdapter::KeyPresence::Unknown};
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
        // Sampled nonsecret lower-bound evidence, not authentication authority.
        std::atomic<uint32_t> pair_tx_total{0}, pair_tx_attempted{0}, pair_tx_acked{0};
        std::atomic<int> pair_result{-1};
        // Nonsecret RX transport diagnostics (counts only, no payload/identifier).
        std::atomic<uint32_t> rx_peak{0}, rx_overflows{0}; // producer-written
        uint32_t rx_write_serviced = 0; // worker-only: chunks consumed inside WRITE waits
        bool rx_dispatching = false;    // worker-only: drain_rx() active (no nested drain)
        void reset_pair_diagnostics() { pair_tx_total=0;pair_tx_attempted=0;pair_tx_acked=0;pair_result=-1;rx_peak=0;rx_overflows=0;rx_write_serviced=0;trace_logging=false;trace.reset(); }
        // Worker-only log lifecycle, deliberately separate from the write gate:
        // true from trace_arm() (explicit claimed attempt) until its terminal
        // entry is queued or diagnostics are reset. A DIRTY successor is active
        // but never ARMED, so it logs (as unavailable) without admitting writers.
        bool trace_logging = false;
        // Nonsecret per-attempt pairing trace (numbers only: no MAC/VIN/address/
        // key/payload). The FIRST loss cause is frozen; later cleanup, our own
        // CLOSE or secondary callbacks cannot overwrite it. Diagnostics only,
        // never authority for enrollment, authorization or charging.
        enum class TraceStage : uint32_t { None, Claimed, Connected, Sent, Verifying, Response, Approved };
        // Unavailable is never written as a cause: snapshots report it for an
        // attempt whose record may contain a write that overlapped freeze/reset.
        enum class TraceCause : uint32_t { None, RemoteDisconnect, Security, RxOverflow, FrameRefused, SendCompletion,
            VerifySession, IdentityChange, ProfileReload, Stop, OperationTimeout, OperationCancelled, Unavailable };
        // Coherent copy of one attempt's record (numbers only, 48 bytes).
        struct TraceSnapshot {
            uint32_t stage = 0, cause = 0, loss_stage = 0, elapsed = 0, polls = 0, denied = 0;
            uint32_t notifications = 0, frames = 0, pair_replies = 0, session_replies = 0;
            int32_t detail = 0, disconnect = -1;
        };
        struct PairTrace {
            // One 32-bit gate word: epoch | DIRTY | RESETTING | ARMED | admitted writers.
            // Every record mutation (any task) is admitted by a CAS that requires
            // ARMED and increments the writer count; it leaves afterwards. The
            // worker alone resets (epoch+1, RESETTING while zeroing), arms and
            // freezes (clears ARMED). Writers still admitted at freeze/reset make
            // that record DIRTY: it is then reported Unavailable, never mixed.
            // Readers take one bounded snapshot (no retry loop) and discard it if
            // the epoch/RESETTING/DIRTY bits changed meanwhile.
            static constexpr uint32_t WRITERS = 0xffu, ARMED = 1u << 8, RESETTING = 1u << 9, DIRTY = 1u << 10, EPOCH_ONE = 1u << 11;
            std::atomic<uint32_t> gate{0};
            // loss_claim elects the single first-cause writer; loss_cause is
            // published LAST (release), so any reader that observes a cause also
            // observes that same writer's detail/stage/time (never a torn mix).
            std::atomic<uint32_t> stage{0}, loss_claim{0}, loss_cause{0}, loss_stage{0}, sent_at{0}, loss_at{0};
            std::atomic<int32_t> loss_detail{0}, disconnect_reason{-1}, write_status{0};
            std::atomic<uint32_t> polls{0}, denied{0}, rx_notifications{0}, frames{0}, pair_replies{0}, session_replies{0};
            uint32_t frames_base = 0, pair_replies_base = 0, session_replies_base = 0, deferred_base = 0; // worker-only
            bool armed() const { return gate.load() & ARMED; }
            // Lock-free, non-blocking: a CAS failure means another gate operation
            // succeeded (bounded per attempt); never waits for another task.
            bool enter() {
                uint32_t g = gate.load();
                do { if (!(g & ARMED) || (g & WRITERS) == WRITERS) { return false; } } while (!gate.compare_exchange_weak(g, g + 1));
                return true;
            }
            void leave() { gate.fetch_sub(1, std::memory_order_release); }
            // Worker only. No writer can be admitted while RESETTING/disarmed.
            void reset() {
                uint32_t g = gate.load(), d;
                do { d = ((g + EPOCH_ONE) & ~(EPOCH_ONE - 1)) | RESETTING | (g & WRITERS) | ((g & WRITERS) ? DIRTY : 0); }
                while (!gate.compare_exchange_weak(g, d));
                stage=0;loss_cause=0;loss_claim=0;loss_stage=0;sent_at=0;loss_at=0;loss_detail=0;disconnect_reason=-1;write_status=0;
                polls=0;denied=0;rx_notifications=0;frames=0;pair_replies=0;session_replies=0;
                gate.fetch_and(~RESETTING, std::memory_order_release);
            }
            void arm() { if (!(gate.load() & DIRTY)) { gate.fetch_or(ARMED); } } // worker only
            void freeze() { // worker only; terminal: no later write is admitted
                uint32_t g = gate.load();
                while (!gate.compare_exchange_weak(g, (g & ~ARMED) | ((g & WRITERS) ? DIRTY : 0))) {}
            }
            void note_disconnect(int32_t reason) { if (enter()) { TESLA_PAIR_TRACE_POINT(2); int32_t none = -1; disconnect_reason.compare_exchange_strong(none, reason); leave(); } }
            void note_write_status(int32_t status) { if (enter()) { write_status.store(status); leave(); } }
            void note_write_refused() { if (enter()) { int32_t none = 0; write_status.compare_exchange_strong(none, -3); leave(); } }
            void note_rx() { if (enter()) { rx_notifications.fetch_add(1); leave(); } }
            void count_poll() { if (enter()) { polls.fetch_add(1); leave(); } }
            void count_denied() { if (enter()) { denied.fetch_add(1); leave(); } }
            void mark_sent(uint32_t now) { if (enter()) { sent_at.store(now ? now : 1); leave(); } }
            // Any task. One bounded pass; false = overlapped a reset/freeze change: discard.
            bool snapshot(TraceSnapshot &s) const {
                const uint32_t g1 = gate.load(std::memory_order_acquire);
                if (g1 & RESETTING) { return false; }
                s = TraceSnapshot{};
                if (g1 & DIRTY) { s.cause = static_cast<uint32_t>(TraceCause::Unavailable); }
                else {
                    s.cause = loss_cause.load(std::memory_order_acquire); // published last by the single claimant
                    TESLA_PAIR_TRACE_POINT(1);
                    const uint32_t sent = sent_at.load(), lost = s.cause ? loss_at.load() : 0;
                    s.stage = stage.load(); s.loss_stage = s.cause ? loss_stage.load() : 0; s.detail = s.cause ? loss_detail.load() : 0;
                    s.elapsed = sent && lost ? lost - sent : 0; s.disconnect = disconnect_reason.load();
                    s.polls = polls.load(); s.denied = denied.load(); s.notifications = rx_notifications.load();
                    s.frames = frames.load(); s.pair_replies = pair_replies.load(); s.session_replies = session_replies.load();
                }
                std::atomic_thread_fence(std::memory_order_acquire);
                return ((g1 ^ gate.load(std::memory_order_relaxed)) & ~(WRITERS | ARMED)) == 0;
            }
        } trace;
        // Any task/callback. Only while an attempt is armed; first writer wins.
        void trace_cause(TraceCause cause, int32_t detail) {
            if (!trace.enter()) { return; }
            TESLA_PAIR_TRACE_POINT(2);
            uint32_t expected = 0;
            if (trace.loss_claim.compare_exchange_strong(expected, 1)) {
                TESLA_PAIR_TRACE_POINT(3);
                trace.loss_detail.store(detail); trace.loss_stage.store(trace.stage.load());
                const uint32_t now = millis(); trace.loss_at.store(now ? now : 1);
                trace.loss_cause.store(static_cast<uint32_t>(cause), std::memory_order_release);
            }
            trace.leave();
        }
        void trace_stage(TraceStage stage) {
            if (!trace.enter()) { return; }
            auto old = trace.stage.load();
            while (old < static_cast<uint32_t>(stage) && !trace.stage.compare_exchange_weak(old, static_cast<uint32_t>(stage))) {}
            trace.leave();
        }
        // Worker-only: counts are deltas of nonsecret SDK counters for this attempt.
        void trace_arm() {
            trace.reset();
            if (vehicle) {
                trace.frames_base = vehicle->frames_seen(); trace.pair_replies_base = vehicle->pair_replies_seen();
                trace.session_replies_base = vehicle->session_replies_seen(); trace.deferred_base = vehicle->deferred_overflows();
            }
            trace.arm(); trace_stage(TraceStage::Claimed);
            trace_logging = true;
        }
        void trace_sample() {
            if (!vehicle || !trace.enter()) { return; }
            trace.frames.store(vehicle->frames_seen() - trace.frames_base);
            trace.pair_replies.store(vehicle->pair_replies_seen() - trace.pair_replies_base);
            trace.session_replies.store(vehicle->session_replies_seen() - trace.session_replies_base);
            trace.leave();
        }
        // Worker-only snapshot buffer; formatted and flushed to the event log at
        // a shallow worker stack point, never inside SDK completions/GATT waits.
        struct TraceEntry {
            const char *outcome = nullptr; // static literal
            uint32_t command = 0, acked = 0, total = 0;
            TraceSnapshot trace;
        } trace_entries[3];
        uint8_t trace_entry_count = 0;
        uint32_t trace_entries_dropped = 0;
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
        std::atomic<uint32_t> config_revision{0}; // main writer; worker action-authority validation
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
