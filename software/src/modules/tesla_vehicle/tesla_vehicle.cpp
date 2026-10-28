#include "tesla_vehicle.h"
#include "bluetooth_memory.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <cctype>
#include "tesla_ble_iram_diagnostics.h"
#if !TESLA_BLE_R13_MASTER_BENCH
#include "tesla_ble_bench_smoke.h"
#include "tesla_ble_bench_discovery.h"
#endif
#if TESLA_BLE_R13_MASTER_BENCH && !defined(TESLA_BLE_MODULE_HOST_TEST)
#include <esp_rom_sys.h>
#endif

#ifndef TESLA_BLE_MODULE_HOST_TEST
#if WARP_BLUETOOTH_COMPILED && !TESLA_BLE_USE_NIMBLE
#include <esp_bt.h>
#include <esp_bt_main.h>
#endif

#include "event_log_prefix.h"
#include "generated/module_dependencies.h"
#if WARP_TESLA_MEMORY_PHASES && WARP_BLUETOOTH_COMPILED
// This explicit diagnostic-only environment contains Web Server. Do not expose
// its headers to ordinary SDK-OFF or non-instrumented module compilations.
#include "modules/web_server/web_server.h"
extern WebServer server;
#endif
#include "tools.h"
#endif
#include "tesla_ble_storage.h"
#include "tesla_ble_transport.h"
#include "tesla_ble_validation.h"
#include "tesla_ble_config_policy.h"
#include "tesla_ble_result.h"
#include "tesla_ble_key_command.h"
#include "tesla_ble_presence.h"


static constexpr uint32_t RESPONSE_TIMEOUT_MS = 40000;

// Wall-clock receive time for SoC publication. 0 = clock not synchronized:
// never substitute uptime as a time of day.
static uint32_t tesla_wall_clock_seconds()
{
    struct timeval tv;
    if (!rtc.clock_synced(&tv) || tv.tv_sec <= 0 || static_cast<uint64_t>(tv.tv_sec) > UINT32_MAX) { return 0; }
    return static_cast<uint32_t>(tv.tv_sec);
}

static TeslaBleIdentity current_ev_identity()
{
#if TESLA_BLE_MANUAL_VEHICLE_UI
    const auto live = ev.get_live_identity();
    return {live.generation, live.index, live.mac};
#else
    return {ev.get_session_generation(), ev.get_active_ev_index(), ev.get_active_ev_mac()};
#endif
}

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

#include "tesla_profiles.inc"
#if TESLA_BLE_MANUAL_VEHICLE_UI
#include "tesla_auto_poll.inc"
#endif


void TeslaVehicle::pre_setup()
{
    bluetooth_memory::sample(bluetooth_memory::Stage::Boot);
    bluetooth_config = Config::Object({{"enable", Config::Bool(false)}});
    bluetooth_update = Config::Object({{"enable", Config::Bool(false)}});
    bluetooth_state = Config::Object({
        {"supported", Config::Bool(bluetooth_supported())},
        {"unsupported_reason", Config::Str(bluetooth_unsupported_reason(), 0, 32)},
        {"phase", Config::Str("disabled", 0, 8)},
        {"restart_required", Config::Bool(false)},
        {"restart_reason", Config::Str("", 0, 32)},
        {"persistence_error", Config::Bool(false)},
        {"legacy_tesla_enabled", Config::Bool(false)},
    });
    empty_command = Config::Object({});
#if TESLA_BLE_R13_MASTER_BENCH
    bench_state = Config::Object({{"running", Config::Bool(false)}, {"key_presence", Config::Str("unknown", 0, 16)}});
#endif
}

bool TeslaVehicle::prepare_tesla()
{
#if TESLA_BLE_R13_MASTER_BENCH
    return false;
#else
    if (tesla) { return true; }
    tesla.reset(new (std::nothrow) Tesla);
    if (!tesla) { return false; }
#if TESLA_BLE_MANUAL_VEHICLE_UI
    tesla->profile = make_profile();
    tesla->config = make_profile(); // Volatile startup snapshot, never an API or config file.
    tesla->profiles = ConfigRoot{Config::Object({
        {"entries", Config::Array({}, &tesla->profile, 0, EV_MAX_EVS)},
    }), [this](Config &update, ConfigSource source) { return validate_profiles(update, source); }};
    tesla->profile_command = Config::Object({{"ev_index", Config::Int(-1, -1, EV_MAX_EVS - 1)}});
    tesla->guided_command = Config::Object({
        {"phase", Config::Str("", 0, 12)}, {"request", Config::Str("", 0, 32)},
        {"generation", Config::Str("", 0, 20)}, {"revision", Config::Uint(0, 0, UINT32_MAX)}, {"profile", make_profile()},
    });
    tesla->guidance_profile = Config::Object({
        {"ev_index", Config::Int(-1, -1, EV_MAX_EVS - 1)},
        {"ev_mac", Config::Str("", 0, 17)}, {"namespace_clean", Config::Bool(false)},
        {"binding_fresh", Config::Bool(false)},
        {"paired", Config::Bool(false)}, {"key_presence", Config::Str("unknown", 0, TESLA_BLE_PRESENCE_MAX_LENGTH)},
    });
    tesla->guidance = Config::Object({
        {"generation", Config::Str("", 0, 20)}, {"revision", Config::Uint(0, 0, UINT32_MAX)}, {"request", Config::Str("", 0, 32)}, {"ready", Config::Bool(false)},
        {"profiles", Config::Array({}, &tesla->guidance_profile, 0, EV_MAX_EVS)},
    });
    tesla->legacy_blocked = TeslaBleStorageAdapter::legacy_material_present();
#else
    tesla->config = ConfigRoot{Config::Object({
        {"enable", Config::Bool(false)},
        {"vin", Config::Str("", 0, 17)},
        {"ble_address", Config::Str("", 0, 17)},
        {"ev_index", Config::Int(-1, -1, EV_MAX_EVS - 1)},
        {"poll_interval", Config::Uint(300, 30, 3600)},
        {"wake_once", Config::Bool(false)},
    }), [this](Config &update, ConfigSource source) -> String {
#if TESLA_BLE_MANUAL_VEHICLE_UI
        if(source!=ConfigSource::File && bluetooth.discovery_busy()) { return String("Search in progress"); }
#endif
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

        const bool changed = update.get("enable")->asBool() != tesla->config.get("enable")->asBool() ||
                             update.get("vin")->asString() != tesla->config.get("vin")->asString() ||
                             update.get("ble_address")->asString() != tesla->config.get("ble_address")->asString() ||
                             update.get("ev_index")->asInt() != tesla->config.get("ev_index")->asInt() ||
                             update.get("poll_interval")->asUint() != tesla->config.get("poll_interval")->asUint() ||
                             update.get("wake_once")->asBool() != tesla->config.get("wake_once")->asBool();
        const auto action = tesla_ble_config_update_action(source == ConfigSource::File,
                                                            tesla->runtime_owner.get() != nullptr,
                                                            changed);
        if (action == TeslaBleConfigUpdateAction::ApplyInLoop) {
            tesla->runtime_update_requested.store(true);
        }
        return "";
    }};

#endif
    tesla->state = Config::Object({
#if TESLA_BLE_MANUAL_VEHICLE_UI
        {"profile_ev_index", Config::Int(-1, -1, EV_MAX_EVS - 1)},
        {"active_ev_index", Config::Int(-1, -1, EV_MAX_EVS - 1)},
        {"legacy_blocked", Config::Bool(tesla->legacy_blocked)},
#endif
        {"running", Config::Bool(false)},
        {"stopping", Config::Bool(false)},
        {"connected", Config::Bool(false)},
        {"key_present", Config::Bool(false)},
        {"key_presence", Config::Str("unknown", 0, TESLA_BLE_PRESENCE_MAX_LENGTH)},
        {"paired", Config::Bool(false)},
        {"pairing_pending", Config::Bool(false)},
        // HTTP acceptance is not evidence of transmission or authentication.
        {"pair_request_sent", Config::Bool(false)},
        // Diagnostic evidence only; never authority for enrollment or charging.
        {"pair_tx_total_bytes", Config::Uint(0, 0, 1026)},
        {"pair_tx_attempted_bytes", Config::Uint(0, 0, 1026)},
        {"pair_tx_acked_bytes", Config::Uint(0, 0, 1026)},
        {"pair_tx_complete", Config::Bool(false)},
        {"pair_result", Config::Int(-1, -1, 8)},
        {"pair_outcome", Config::Str("none", 0, 24)},
        {"pair_response_seen", Config::Bool(false)},
        {"pair_response_whitelist", Config::Bool(false)},
        {"pair_response_code", Config::Uint(0, 0, UINT32_MAX)},
        {"pair_response_outer", Config::Uint(0, 0, UINT32_MAX)},
        {"pair_response_operation", Config::Uint(0, 0, UINT32_MAX)},
        // First-cause per-attempt pairing trace. Nonsecret numbers/enum names
        // only; diagnostics, never authority for enrollment or charging.
        {"pair_stage", Config::Str("none", 0, 12)},
        {"pair_loss_cause", Config::Str("none", 0, 24)},
        {"pair_loss_stage", Config::Str("none", 0, 12)},
        {"pair_loss_detail", Config::Int(0, INT32_MIN, INT32_MAX)},
        {"pair_loss_elapsed_ms", Config::Uint(0, 0, UINT32_MAX)},
        {"pair_disconnect_reason", Config::Int(-1, INT32_MIN, INT32_MAX)},
        {"pair_verify_polls", Config::Uint(0, 0, UINT32_MAX)},
        {"pair_verify_denied", Config::Uint(0, 0, UINT32_MAX)},
        {"pair_rx_notifications", Config::Uint(0, 0, UINT32_MAX)},
        {"pair_rx_frames", Config::Uint(0, 0, UINT32_MAX)},
        {"pair_rx_pair_replies", Config::Uint(0, 0, UINT32_MAX)},
        {"pair_rx_session_replies", Config::Uint(0, 0, UINT32_MAX)},
        {"timeout_cause", Config::Str("", 0, 24)},
        {"timeout_operation", Config::Uint(0, 0, 255)},
        {"timeout_elapsed_ms", Config::Uint(0, 0, UINT32_MAX)},
        {"connect_attempt", Config::Uint(0, 0, UINT32_MAX)},
        {"connect_flags", Config::Uint(0, 0, 511)},
        {"connect_submit_rc", Config::Int(-1, -1, INT32_MAX)},
        {"connect_cancel_rc", Config::Int(-1, -1, INT32_MAX)},
        {"connect_status", Config::Int(-1, -1, INT32_MAX)},
        {"restart_required", Config::Bool(false)},
        {"last_soc", Config::Float(NAN)},
        {"action_busy", Config::Bool(false)},
        {"last_update", Config::Uptime()},
        // Unix seconds of the last consumed authenticated SoC; 0 = WARP clock unsynchronized.
        {"last_update_unix", Config::Uint(0, 0, UINT32_MAX)},
        // Terminal outcome of the last SoC operation (see PollOutcome).
        {"poll_outcome", Config::Str("none", 0, 24)},
        {"poll_outcome_automatic", Config::Bool(false)},
        // Autonomous firmware scheduler: off|waiting|not_paired|searching|polling|not_found|failed.
        {"auto_poll", Config::Str("off", 0, 12)},
        {"last_error", Config::Str("", 0, 96)},
    });

#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS && !TESLA_BLE_R13_MASTER_BENCH && !TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
    tesla_ble_bench_smoke_pre_setup();
#endif
#if TESLA_BLE_MANUAL_VEHICLE_UI
    tesla->discovery_target=Config::Object({
        {"name",Config::Str("",0,18)}, {"ble_address",Config::Str("",0,17)}, {"rssi",Config::Int(0,-127,20)},
        {"ble_address_type",Config::Uint(0,0,1)}, {"matches_vin",Config::Bool(false)},
    });
    tesla->discovery_state=Config::Object({
        {"status",Config::Str("idle",0,9)}, {"busy",Config::Bool(false)},
        {"error",Config::Str("",0,16)}, {"truncated",Config::Bool(false)},
        // VIN beacon seen, but only non-connectable: vehicle reports its BLE connection limit.
        {"vin_not_connectable",Config::Bool(false)},
        {"revision",Config::Uint(0,0,UINT32_MAX)}, {"ev_index",Config::Int(-1,-1,EV_MAX_EVS-1)},
        {"result_age_ms",Config::Uint(TeslaBleDiscovery::RESULT_TTL_MS,0,UINT32_MAX)},
        {"result_ttl_ms",Config::Uint(TeslaBleDiscovery::RESULT_TTL_MS,0,UINT32_MAX)},
        {"searched_vin",Config::Str("",0,17)}, {"ev_mac",Config::Str("",0,17)}, {"generation",Config::Str("",0,20)},
        {"targets",Config::Array({},&tesla->discovery_target,0,TeslaBleDiscovery::LIMIT)},
    });
    tesla->scan_command=Config::Object({
        {"vin",Config::Str("",0,17)}, {"ev_index",Config::Int(-1,-1,EV_MAX_EVS-1)}, {"ev_mac",Config::Str("",0,17)},
    });
#endif
#if TESLA_BLE_MANUAL_VEHICLE_UI
    const bool saved_profiles = TeslaBleStorageAdapter::profiles_config_present();
    if (!api.restorePersistentConfig("tesla_vehicle/profiles", &tesla->profiles) && saved_profiles) {
        tesla->legacy_blocked = true; // An unreadable/new-schema mapping is not a blank identity store.
        tesla->state.get("legacy_blocked")->updateBool(true);
    }
    if (tesla->legacy_blocked) {
        publish_error("Legacy Tesla storage retained; verified offline migration required; actions blocked");
    }
#else
    if (!api.restorePersistentConfig("tesla_vehicle/config", &tesla->config)) {
        api.restorePersistentConfig("tesla_ble/config", &tesla->config);
    }
#endif
    return true;
#endif
}

void TeslaVehicle::setup()
{
    bool desired = false;
    bluetooth_commit_ok = load_bluetooth(desired);
    bluetooth_config.get("enable")->updateBool(desired);
    bluetooth_boot_enabled = bluetooth_supported() && bluetooth_commit_ok && desired;
#if TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
    // Exercise ordinary Tesla/API registration, but never start a vehicle or
    // honor persisted activation. Only the separate no-peer UART lifecycle owns BT.
    bluetooth_boot_enabled = false;
    prepare_tesla();
#endif
#if !TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
    if (bluetooth_boot_enabled) {
#if TESLA_BLE_R13_MASTER_BENCH
#if TESLA_BLE_MANUAL_WEB_BENCH
        // Retry8 launched only after normal API/MQTT startup. Defer until the
        // ordinary module loop, after the same complete boot/allocation probe.
        service_start_requested = true;
#else
        tesla_ble_iram_snapshot("module.setup", "boot");
        master_bench.start();
#endif
#else
        prepare_tesla();
#if WARP_BLUETOOTH_COMPILED
        bluetooth.start(gatt_callback);
#endif
#endif
    }
#endif
    publish_bluetooth_state();
    initialized = true;
#if TESLA_BLE_R13_MASTER_BENCH
    if (bluetooth_boot_enabled) { bench_emit("boot", true); }
#endif
}

#if TESLA_BLE_R13_MASTER_BENCH
void TeslaVehicle::bench_emit(const char *request, bool ok)
{
    publish_bluetooth_state();
    const auto boolean = [](bool v) { return v ? "true" : "false"; };
#ifdef TESLA_BLE_MODULE_HOST_TEST
    std::printf(
#else
    esp_rom_printf(
#endif
        "R13_STATE {\"request\":\"%s\",\"ok\":%s,\"supported\":%s,\"unsupported_reason\":\"%s\","
        "\"desired\":%s,\"boot_enabled\":%s,\"phase\":\"%s\",\"restart_required\":%s,"
        "\"persistence_error\":%s,\"controller\":%d,\"host\":%d,\"gatt_ready\":%s,"
        "\"worker_stack_min\":%u,\"tesla_running\":false,\"key_presence\":\"unknown\"}\n",
        request, boolean(ok), boolean(bluetooth_supported()), bluetooth_unsupported_reason(),
        boolean(bluetooth_config.get("enable")->asBool()), boolean(bluetooth_boot_enabled),
        (strcmp(request, "teardown") == 0 && master_bench.stopped()) ? "stopped" : bluetooth_state.get("phase")->asString().c_str(), boolean(bluetooth_state.get("restart_required")->asBool()),
        boolean(bluetooth_state.get("persistence_error")->asBool()), master_bench.controller(), master_bench.host(),
        boolean(master_bench.gatt_ready()), static_cast<unsigned>(master_bench.stack_min()));
}
#if !TESLA_BLE_MANUAL_WEB_BENCH
void TeslaVehicle::bench_serial()
{
    for (unsigned i = 0; i < 32 && Serial.available(); ++i) {
        const char c = static_cast<char>(Serial.read());
        if (c != '\n' && (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126)) {
            bench_line_overflow = true; continue;
        }
        if (c != '\n') {
            if (bench_line_size + 1 < sizeof(bench_line)) { bench_line[bench_line_size++] = c; }
            else { bench_line_overflow = true; }
            continue;
        }
        bench_line[bench_line_size] = '\0';
        String error;
        bool ok = false;
        const char *name = "invalid";
        if (!bench_line_overflow) {
            if (strcmp(bench_line, "r13:state") == 0) { name = "state"; ok = true; }
            else if (strcmp(bench_line, "r13:on") == 0) { name = "on"; ok = save_bluetooth(true, error); }
            else if (strcmp(bench_line, "r13:off") == 0) { name = "off"; ok = save_bluetooth(false, error); }
            else if (strcmp(bench_line, "r13:restart") == 0) { name = "restart"; ok = restart_bluetooth(error); }
        }
        bench_emit(name, ok);
        bench_line_size = 0; bench_line_overflow = false;
    }
}
#endif
#endif

void TeslaVehicle::register_bluetooth_urls()
{
    // Local transaction: addPersistentConfig's void writer cannot confirm commit.
    api.addState("bluetooth/config", &bluetooth_config);
    api.addState("bluetooth/state", &bluetooth_state);
#if WARP_TESLA_MEMORY_PHASES && !defined(TESLA_BLE_MODULE_HOST_TEST)
    server.on_HTTPThread("/bluetooth/memory", HTTP_GET, [](WebServerRequest request) {
        request.beginChunkedResponse_json(200);
        request.sendChunk("{\"version\":1,\"stack_unit\":\"bytes\",\"samples\":[");
        char buffer[512];
        for(unsigned i=0;i<static_cast<unsigned>(bluetooth_memory::Stage::Count);++i) {
            if(!bluetooth_memory::json(static_cast<bluetooth_memory::Stage>(i),buffer,sizeof(buffer))) {
                return request.endChunkedResponse(); // Fixed maximum row, truncation never serialized.
            }
            if(i)request.sendChunk(",");
            request.sendChunk(buffer);
        }
        request.sendChunk("]}");
        return request.endChunkedResponse();
    });
#endif
    api.addCommand("bluetooth/config_update", &bluetooth_update, {}, [this](Language, String &errmsg) {
        save_bluetooth(bluetooth_update.get("enable")->asBool(), errmsg);
    }, false);
    api.addCommand("bluetooth/restart", &empty_command, {}, [this](Language, String &errmsg) {
        restart_bluetooth(errmsg);
    }, true);
}

bool TeslaVehicle::load_bluetooth(bool &desired)
{
    BluetoothConfigStore store(BLUETOOTH_CONFIG_PATH, bluetooth_storage_quarantined);
    const bool ok = store.load(desired);
    bluetooth_storage_quarantined = store.io_quarantined();
    return ok;
}

bool TeslaVehicle::persist_bluetooth(bool desired)
{
    BluetoothConfigStore store(BLUETOOTH_CONFIG_PATH, bluetooth_storage_quarantined);
    const bool ok = store.save(desired);
    bluetooth_storage_quarantined = store.io_quarantined();
    return ok;
}

bool TeslaVehicle::save_bluetooth(bool desired, String &errmsg)
{
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if(bluetooth.discovery_busy()) { errmsg=checked_error_string("Search in progress");return false; }
#endif
    if (bluetooth_restart_pending) { errmsg = checked_error_string("Restart pending"); return false; }
#if TESLA_BLE_R13_MASTER_BENCH
    if (bench_restart_pending) { errmsg = checked_error_string("Restart pending"); return false; }
#endif
    if (desired && !bluetooth_supported()) {
        errmsg = checked_error_string("Bluetooth unavailable: SDK or hardware does not support this service");
        return false;
    }
    bluetooth_commit_ok = persist_bluetooth(desired);
    if (!bluetooth_commit_ok) {
        errmsg = checked_error_string("Bluetooth configuration commit failed; not saved; no restart permitted");
    } else { bluetooth_config.get("enable")->updateBool(desired); }
    publish_bluetooth_state();
    return bluetooth_commit_ok;
}

bool TeslaVehicle::restart_bluetooth(String &errmsg)
{
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if(bluetooth.discovery_busy()) { errmsg=checked_error_string("Search in progress");return false; }
#endif
    bool saved = false;
    publish_bluetooth_state();
    if (!bluetooth_supported() || !bluetooth_commit_ok ||
        !bluetooth_state.get("restart_required")->asBool() ||
        !load_bluetooth(saved) || saved != bluetooth_config.get("enable")->asBool()) {
        bluetooth_commit_ok = false;
        publish_bluetooth_state();
        errmsg = checked_error_string("No verified Bluetooth commit available for restart");
        return false;
    }
#if TESLA_BLE_R13_MASTER_BENCH
#if TESLA_BLE_MANUAL_WEB_BENCH
    if (service_start_requested) { errmsg = checked_error_string("Bluetooth boot startup pending"); return false; }
#endif
    if (master_bench.failed()) { errmsg = checked_error_string("Bench lifecycle failed"); return false; }
    if (master_bench.active()) {
        bench_restart_pending = true; return true;
    }
    if (strcmp(master_bench.phase(), "disabled") != 0) {
        errmsg = checked_error_string("Bench lifecycle not quiescent"); return false;
    }
#endif
    if (bluetooth_boot_enabled && !bluetooth.stopped()) {
        if (bluetooth.failed()) { errmsg = checked_error_string("Bluetooth lifecycle failed; ownership retained"); return false; }
        bluetooth_restart_pending = true; service_start_requested = false; return true;
    }
    trigger_reboot("Bluetooth configuration");
    return true;
}

void TeslaVehicle::register_urls()
{
    register_bluetooth_urls();
#if TESLA_BLE_R13_MASTER_BENCH
    // Never expose Tesla commands/config or old active bench routes here.
#if !TESLA_BLE_MANUAL_WEB_BENCH
    api.addState("tesla_vehicle/state", &bench_state);
#endif
    return;
#endif
    if (!tesla) { return; } // Core registration is boot-only: reboot ON exposes service APIs.
    register_tesla_urls();
}

void TeslaVehicle::request_pair(int index, String &errmsg)
{
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if (!live_profile(index)) { errmsg = checked_error_string("Selected WARP profile is inactive or its SLAC binding changed"); return; }
#else
    (void)index;
#endif
    if (!accepts_commands()) { errmsg = checked_error_string("Tesla BLE is not enabled"); return; }
    Runtime *r = tesla->runtime_owner.get();
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if (r->authorized.load()) { errmsg = checked_error_string("Profile already authorized; no re-pairing needed"); return; }
    if (!runtime_binding_matches()) { errmsg = checked_error_string("Fresh VIN search match expired or changed; scan and select again"); return; }
#endif
    if (r->manual_poll_pending.load()) { errmsg = checked_error_string("Tesla poll is pending"); return; }
    const auto identity = r->session.identity();
    if (!identity || identity->index != r->configured_ev_index || identity->mac == 0) {
        errmsg = checked_error_string("Configured Tesla is not currently connected"); return;
    }
    if (!r->command_outcome.accept_pair(*identity, millis(),
#if TESLA_BLE_MANUAL_VEHICLE_UI
        tesla->config_revision, r->binding_revision
#else
        0, 0
#endif
        )) { errmsg = checked_error_string("Tesla pairing is pending or deletion is active"); return; }
    r->pair_requested.store(true);
    tesla->state.get("pair_request_sent")->updateBool(false);
    tesla->state.get("pairing_pending")->updateBool(true);
}

void TeslaVehicle::request_poll(int index, String &errmsg)
{
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if (!live_profile(index)) { errmsg = checked_error_string("Selected WARP profile is inactive or its SLAC binding changed"); return; }
#else
    (void)index;
#endif
    if (!accepts_commands() || !tesla->runtime_owner.get()->command_outcome.forward_available()) {
        errmsg = checked_error_string("Tesla BLE is not enabled"); return;
    }
    Runtime *r = tesla->runtime_owner.get();
#if TESLA_BLE_MANUAL_VEHICLE_UI
    const auto identity = r->session.identity();
    if (!r->profile_key_present.load()) { errmsg = checked_error_string("This profile has no known local key; first setup requires key-card pairing"); return; }
    if (!runtime_binding_matches()) { errmsg = checked_error_string("Verified VIN transport binding required"); return; }
    if (tesla->state.get("pairing_pending")->asBool() || !identity || identity->index != r->configured_ev_index || identity->mac == 0 ||
        r->manual_poll_pending.exchange(true)) {
        errmsg = checked_error_string("Tesla poll pending or configured vehicle not active"); return;
    }
#endif
    r->poll_requested.store(true);
}

void TeslaVehicle::register_tesla_urls()
{
#if TESLA_BLE_MANUAL_VEHICLE_UI
    api.addPersistentConfig("tesla_vehicle/profiles", &tesla->profiles, {}, {"entries"});
#else
    api.addPersistentConfig("tesla_vehicle/config", &tesla->config, {}, {"vin", "ble_address"});
#endif
    api.addState("tesla_vehicle/state", &tesla->state);
#if TESLA_BLE_MANUAL_VEHICLE_UI
    // Volatile state only: never addPersistentConfig, log, or write identities.
    api.addState("tesla_vehicle/discovery",&tesla->discovery_state,{}, {"targets"});
    api.addCommand("tesla_vehicle/scan",&tesla->scan_command,{"vin","ev_mac"},[this](Language,String &errmsg) {
        TeslaBleDiscovery::Scope scope;
        scope.index=tesla->scan_command.get("ev_index")->asInt();
        scope.generation=ev.get_live_identity().generation;
        scope.mac=profile_mac_value(tesla->scan_command.get("ev_mac")->asString().c_str());
        if(scope.index<0 || !scope.mac || ev.get_profile_mac(scope.index)!=scope.mac ||
            !tesla_ble_vehicle_name(tesla->scan_command.get("vin")->asString().c_str(),scope.name)) {
            errmsg=checked_error_string("Valid VIN and exact existing WARP profile required for search");return;
        }
        // Informational scope copied with the completed discovery snapshot.
        // Existing name/MAC/generation/revision checks remain adoption authority.
        memcpy(scope.searched_vin,tesla->scan_command.get("vin")->asString().c_str(),17);
        if(!discovery_allowed() || !bluetooth.request_discovery(scope)) {
            errmsg=checked_error_string("Tesla search unavailable: Bluetooth inactive, restart pending, or action busy");return;
        }
        publish_discovery();publish_bluetooth_state();
    },true);
#endif
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS && !TESLA_BLE_R13_MASTER_BENCH && !TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
    tesla_ble_bench_smoke_register_urls();
    tesla_ble_bench_discovery_register_urls();
#endif

#if TESLA_BLE_MANUAL_VEHICLE_UI
    api.addState("tesla_vehicle/guidance", &tesla->guidance, {}, {"profiles"});
    api.addCommand("tesla_vehicle/guided", &tesla->guided_command, {"profile"}, [this](Language, String &errmsg) {
        guided_command(errmsg);
    }, true);
    api.addCommand("tesla_vehicle/start", &tesla->profile_command, {}, [this](Language, String &errmsg) {
        select_profile(tesla->profile_command.get("ev_index")->asInt(), false, errmsg);
    }, true);
#else
    api.addCommand("tesla_vehicle/start", &empty_command, {}, [this](Language, String &errmsg) {
        if (!bluetooth.active() || bluetooth_restart_pending || !tesla->config.get("enable")->asBool() ||
            service_start_requested ||
#if TESLA_BLE_MANUAL_VEHICLE_UI
            bluetooth.discovery_busy() ||
            !bluetooth_commit_ok || bluetooth_config.get("enable")->asBool() != bluetooth_boot_enabled ||
#endif
            tesla->runtime_owner.restart_required() || tesla->runtime_owner.get()) {
            errmsg = checked_error_string("Tesla service unavailable, disabled, or already started"); return;
        }
        service_start_requested = true; // Consent is volatile; no pairing or vehicle command here.
    }, true);
#endif
    api.addCommand("tesla_vehicle/pair",
#if TESLA_BLE_MANUAL_VEHICLE_UI
        &tesla->profile_command,
#else
        &empty_command,
#endif
        {}, [this](Language, String &errmsg) {
        request_pair(
#if TESLA_BLE_MANUAL_VEHICLE_UI
            tesla->profile_command.get("ev_index")->asInt(),
#else
            -1,
#endif
            errmsg);
    }, true);

    api.addCommand("tesla_vehicle/poll",
#if TESLA_BLE_MANUAL_VEHICLE_UI
        &tesla->profile_command,
#else
        &empty_command,
#endif
        {}, [this](Language, String &errmsg) {
        request_poll(
#if TESLA_BLE_MANUAL_VEHICLE_UI
            tesla->profile_command.get("ev_index")->asInt(),
#else
            -1,
#endif
            errmsg);
    }, true);

#if TESLA_BLE_MANUAL_VEHICLE_UI
    api.addCommand("tesla_vehicle/forget_key", &tesla->profile_command, {}, [this](Language, String &errmsg) {
        select_profile(tesla->profile_command.get("ev_index")->asInt(), true, errmsg);
    }, true);
#else
    api.addCommand("tesla_vehicle/forget_key", &empty_command, {}, [this](Language, String &errmsg) {
#if TESLA_BLE_MANUAL_VEHICLE_UI
        if (bluetooth.discovery_busy() || service_start_requested || tesla->state.get("pairing_pending")->asBool() ||
            (tesla->runtime_owner.get() && tesla->runtime_owner.get()->manual_poll_pending.load())) {
            errmsg = checked_error_string("Tesla action is pending; wait before local deletion"); return;
        }
#endif
        if (tesla->runtime_owner.get() == nullptr) {
            if (!start_runtime(true)) { errmsg = checked_error_string("Could not start Tesla storage worker; reboot may be required"); }
            return;
        }
        if (!accepts_commands()) {
            // A general Bluetooth-only worker is intentionally alive even when
            // the Tesla service is OFF. Do not misreport that as teardown, or
            // loosen the existing deletion/worker ownership gates.
            errmsg = checked_error_string(!tesla->config.get("enable")->asBool() ?
                "Tesla service is disabled; disable Bluetooth and restart before deleting the local key, or enable the Tesla service" :
                "Tesla BLE is stopping; retry after teardown");
            return;
        }
        Runtime *r = tesla->runtime_owner.get();
        if (!r->command_outcome.accept_forget()) {
            errmsg = checked_error_string("Tesla key deletion is already active");
            return;
        }
        r->pair_requested.store(false);
        r->poll_requested.store(false);
        r->forget_requested.store(true);
    }, true);
#endif
}

#if TESLA_BLE_MANUAL_VEHICLE_UI
bool TeslaVehicle::discovery_allowed() const
{
    if(!tesla || !bluetooth.active() || !bluetooth_boot_enabled || !bluetooth_commit_ok ||
       !bluetooth_config.get("enable")->asBool() || bluetooth_restart_pending || bluetooth.discovery_busy() ||
       tesla->legacy_blocked || service_start_requested || tesla->runtime_update_requested.load() || tesla->runtime_owner.restart_required() ||
       tesla->runtime_owner.is_stopping() || tesla->state.get("pairing_pending")->asBool()) { return false; }
    auto *r=tesla->runtime_owner.get();
    return !r || (!r->profile_pending.load() && !r->storage_only && r->ble_ready.load() && !r->stop_requested.load() &&
        !r->manual_poll_pending.load() && !r->pair_requested.load() && !r->poll_requested.load() &&
        !r->forget_requested.load() && !r->pair_approval_waiting.load() &&
        r->command_outcome.forward_state()==TeslaBleCommandOutcome::Forward::Available);
}

void TeslaVehicle::publish_discovery()
{
    if(!tesla) { return; }
    TeslaBleDiscovery::Snapshot s;
    if(!bluetooth.discovery_snapshot(s)) { return; }
    // Refresh age even when the result revision is unchanged. The existing live
    // adoption check enforces this exact TTL independently of API publication.
    tesla->discovery_state.get("result_age_ms")->updateUint(s.status==TeslaBleDiscovery::Status::Complete ? millis()-s.completed_at_ms : TeslaBleDiscovery::RESULT_TTL_MS);
    if (s.revision == tesla->discovery_revision) {
        auto rows = tesla->discovery_state.get("targets");
        const bool scope_current = ev.get_live_identity().generation == s.scope.generation && ev.get_profile_mac(s.scope.index) == s.scope.mac;
        for (size_t i = 0; i < rows->count() && i < s.count; ++i) {
            rows->get(i)->get("matches_vin")->updateBool(scope_current && bluetooth.discovery_matches(s.scope, s.revision, s.targets[i].address, s.targets[i].address_type));
        }
        return;
    }
    auto &state=tesla->discovery_state;
    auto rows=state.get("targets");rows->removeAll();
    bool ok=true,vin_connectable=false,vin_not_connectable=false;
    if(s.status==TeslaBleDiscovery::Status::Complete) {
        ok=rows->setCount(s.count);
        for(unsigned i=0;ok && i<s.count;++i) {
            char address[18];const auto &t=s.targets[i];
            if(!strcmp(t.name,s.scope.name)) { (t.connectable ? vin_connectable : vin_not_connectable)=true; }
            snprintf(address,sizeof(address),"%02X:%02X:%02X:%02X:%02X:%02X",t.address[0],t.address[1],t.address[2],t.address[3],t.address[4],t.address[5]);
            auto row=rows->get(i);
            ok=row->get("name")->updateStringChecked(t.name) && row->get("ble_address")->updateStringChecked(address);
            row->get("rssi")->updateInt(t.rssi);
            row->get("ble_address_type")->updateUint(t.address_type);
            row->get("matches_vin")->updateBool(bluetooth.discovery_matches(s.scope,s.revision,t.address,t.address_type));
        }
    }
    if(!ok) { rows->removeAll(); }
    state.get("busy")->updateBool(s.busy);
    state.get("truncated")->updateBool(s.truncated);
    state.get("vin_not_connectable")->updateBool(ok && vin_not_connectable && !vin_connectable);
    state.get("revision")->updateUint(s.revision);
    state.get("ev_index")->updateInt(s.scope.index);
    char mac_text[18]{},generation[21]{};
    if (s.scope.mac) snprintf(mac_text,sizeof(mac_text),"%02X:%02X:%02X:%02X:%02X:%02X",
        unsigned((s.scope.mac>>40)&255),unsigned((s.scope.mac>>32)&255),unsigned((s.scope.mac>>24)&255),
        unsigned((s.scope.mac>>16)&255),unsigned((s.scope.mac>>8)&255),unsigned(s.scope.mac&255));
    snprintf(generation,sizeof(generation),"%llu",static_cast<unsigned long long>(s.scope.generation));
    state.get("searched_vin")->updateString(s.scope.searched_vin);
    state.get("ev_mac")->updateString(mac_text);
    state.get("generation")->updateString(generation);
    state.get("status")->updateString(!ok?"error":s.status==TeslaBleDiscovery::Status::Idle?"idle":
        s.status==TeslaBleDiscovery::Status::Searching?"searching":s.status==TeslaBleDiscovery::Status::Complete?"complete":"error");
    state.get("error")->updateString(ok?s.error:"memory");
    tesla->discovery_revision=s.revision;
}
#endif

bool TeslaVehicle::accepts_commands() const
{
    if (!tesla || bluetooth_restart_pending || !bluetooth.active()) { return false; }
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if (tesla->legacy_blocked || bluetooth.discovery_busy() || !bluetooth_commit_ok || bluetooth_config.get("enable")->asBool() != bluetooth_boot_enabled) { return false; }
    auto *active = tesla->runtime_owner.get();
    if (!active || active->profile_pending.load() || !active->profile_ready.load() || !active->profile_actionable.load()) { return false; }
#endif
    const Runtime *r = tesla->runtime_owner.get();
    return bluetooth_boot_enabled && !tesla->runtime_owner.restart_required() && !ev.session_exhausted() && tesla->config.get("enable")->asBool() && r != nullptr &&
           r->service_enabled &&
           !r->identity_exhausted.load() &&
           !tesla->runtime_update_requested.load() && !r->stop_requested.load();
}

bool TeslaVehicle::start_runtime(bool storage_only)
{
    if (!tesla || bluetooth_restart_pending || (!storage_only && (!bluetooth_boot_enabled || !bluetooth.active() || !bluetooth_supported()))) { return false; }
    if (ev.session_exhausted()) {
        tesla->runtime_owner.require_restart(); tesla->state.get("restart_required")->updateBool(true); return false;
    }
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
    (void)storage_only;
    return false; // Bench smoke must never create a Tesla runtime.
#else
    tesla->start_attempted = true;
    return tesla->runtime_owner.start(storage_only || bluetooth_boot_enabled, [this, storage_only]() -> std::unique_ptr<Runtime> {
        bluetooth_memory::sample(bluetooth_memory::Stage::RuntimeBefore);
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
                const size_t depth = rx ? RX_QUEUE_DEPTH : 8;
                rx = false;
                return xQueueCreate(depth, size);
            })) {
            publish_error("Failed to allocate BLE queues");
            return nullptr;
        }
        const auto &vin = tesla->config.get("vin")->asString();
        bluetooth_memory::sample(bluetooth_memory::Stage::QueuesReady);
        const auto &address = tesla->config.get("ble_address")->asString();
        const bool service =
#if TESLA_BLE_MANUAL_VEHICLE_UI
            true; // One reusable local worker; deletion never constructs a protocol vehicle.
#else
            !storage_only && tesla->config.get("enable")->asBool();
#endif
        if (service && !storage_only && (!tesla_ble_valid_vin(vin.c_str()) || !tesla_ble_valid_address(address.c_str()))) {
            publish_error("Invalid or unavailable Tesla BLE identity");
            return nullptr;
        }
        r->vin.assign(vin.c_str());
        if (service && !storage_only) {
            unsigned bytes[6]{};
            if (sscanf(address.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x", &bytes[0], &bytes[1], &bytes[2],
                       &bytes[3], &bytes[4], &bytes[5]) != 6) { return nullptr; }
            for (size_t i = 0; i < 6; ++i) { r->remote_address[i] = static_cast<uint8_t>(bytes[i]); }
        }
        r->configured_ev_index = tesla->config.get("ev_index")->asInt();
#if TESLA_BLE_MANUAL_VEHICLE_UI
        if (!r->storage_adapter.select_profile(r->configured_ev_index.load(),
                profile_mac_value(tesla->config.get("ev_mac")->asString().c_str()))) { return nullptr; }
#endif
        r->poll_interval_seconds = tesla->config.get("poll_interval")->asUint();
        r->wake_once = tesla->config.get("wake_once")->asBool();
        r->storage_only = storage_only;
#if TESLA_BLE_MANUAL_VEHICLE_UI
        r->storage_only = false; // Profile deletion is scoped in ProfileRequest, not global worker mode.
#endif
        r->service_enabled = service;
        return r;
    }, [this](Runtime &r) {
        r.epoch = tesla->runtime_owner.epoch();
        r.session.publish(current_ev_identity());
        tesla->callback_gate.open();
        if (xTaskCreatePinnedToCore(task_entry, "tesla_ble", 12288, this, 1, &r.task_handle, 0) != pdPASS) {
            tesla->callback_gate.close();
            r.task_handle = nullptr;
            publish_error("Failed to start Tesla BLE task");
            return false;
        }
        bluetooth_memory::sample(bluetooth_memory::Stage::TaskCreated);
        return true;
    });
#endif
}

bool TeslaVehicle::create_vehicle()
{
    Runtime *r = tesla->runtime_owner.get();
    r->vehicle.reset();
    r->keys.reset(new (std::nothrow) tesla_clean::IdentityStore);
    if (!r->keys || !r->storage->initialize_identity(*r->keys, {TeslaBleTransportAdapter::random, nullptr}, false) ||
        TeslaBleTransportAdapter::random(nullptr, r->routing_address.data(), r->routing_address.size()) != 0) { return false; }
    r->vehicle.reset(new (std::nothrow) tesla_clean::Vehicle(*r->keys,
        {reinterpret_cast<const uint8_t *>(r->vin.c_str()), 17},
        {r->routing_address.data(), r->routing_address.size()}, r->transport_adapter.ports()));
    bluetooth_memory::sample(bluetooth_memory::Stage::ProtocolReady,true);
    return r->vehicle != nullptr;
}

void TeslaVehicle::task_entry(void *context)
{
    static_cast<TeslaVehicle *>(context)->task_loop();
}

void TeslaVehicle::task_loop()
{
    bluetooth_memory::sample(bluetooth_memory::Stage::WorkerBegin,true);
    Runtime *r = tesla->runtime_owner.get();
    if (!r->stop_requested.load()) {
        r->worker_watch.begin(millis());
        const auto initial = r->session.identity();
        if (initial) { r->observed_identity = *initial; r->policy.on_vehicle_changed(initial->generation, initial->index); }
        if (!r->storage->set_mutation_guard([r]() {
            const auto identity = r->session.identity();
            return !r->stop_requested.load() && !r->operation.retired() &&
                (r->storage_only ||
#if TESLA_BLE_MANUAL_VEHICLE_UI
                 r->deleting_profile ||
#endif
                 (identity && *identity == r->observed_identity));
        })) {
            post_event(EventType::Error, "Failed to allocate Tesla storage guard");
            r->stop_requested.store(true);
        }
#if TESLA_BLE_MANUAL_VEHICLE_UI
        if (!r->stop_requested.load()) {
            r->controller_used = true;
            gatt_owner.store(this);
            r->ble_ready.store(bluetooth.active());
        }
#else
        if (!r->stop_requested.load() && r->storage_only) {
            if (!r->storage->forget_material()) { post_event(EventType::Error, "Could not remove Tesla key material"); }
            r->stop_requested.store(true);
        } else if (!r->stop_requested.load() && r->service_enabled) {
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
            }
        }
        if (!r->stop_requested.load() && !r->storage_only) {
#if WARP_BLUETOOTH_COMPILED
            r->controller_used = true;
            if (bluetooth.active()) {
                gatt_owner.store(this);
                r->ble_ready.store(true);
            } else {
                post_event(EventType::Error, "Failed to initialize Bluetooth");
                r->stop_requested.store(true);
            }
#endif
        }
#endif
        r->worker_watch.finish();
    }

    while (!r->stop_requested.load()) {
        if (!r->service_enabled) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        r->worker_watch.begin(millis());
        if (r->storage->restart_required()) {
            post_event(EventType::Error, "Tesla persistence close failed; storage quarantined until reboot");
            r->stop_requested.store(true);
            r->worker_watch.finish();
            break;
        }
        process_disconnect();
        update_active_vehicle();
#if TESLA_BLE_MANUAL_VEHICLE_UI
        apply_profile();
        flush_pair_trace(); // Shallow frame; also covers profile reload/teardown entries.
        if (!r->vehicle || !r->profile_ready.load()) {
            r->worker_watch.finish(); vTaskDelay(pdMS_TO_TICKS(20)); continue;
        }
#endif
        bluetooth_memory::sample(bluetooth_memory::Stage::WorkerActive,true,1000);
        process_rx();
        process_commands();
        process_pair_verification();
        if (r->stop_requested.load()) {
            flush_pair_trace();
            r->worker_watch.finish();
            break;
        }
        r->vehicle->tick();
        check_worker_exhaustion();
        flush_pair_trace();

        if (r->in_flight_started_ms != 0 && millis() - r->in_flight_started_ms > RESPONSE_TIMEOUT_MS) {
            r->in_flight_started_ms = 0;
            r->manual_poll_pending.store(false);
            post_event(EventType::Error, "Tesla SoC request timed out");
        }

#if !TESLA_BLE_MANUAL_VEHICLE_UI
        const auto identity = r->session.identity();
        const bool matching_vehicle = identity && identity->index == r->configured_ev_index;
        if (matching_vehicle && static_cast<int32_t>(millis() - r->next_poll_ms) >= 0 && r->in_flight_started_ms == 0) {
            request_soc(false);
        }
#endif

        r->worker_watch.finish();
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    r->worker_watch.begin(millis());
    r->trace_cause(Runtime::TraceCause::Stop, 0);
    disconnect_internal();
    flush_pair_trace();
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
    bluetooth_memory::sample(bluetooth_memory::Stage::WorkerStopped,true);
    r->worker_stopped.store(true, std::memory_order_release);
    for (;;) {
        vTaskSuspend(nullptr);
    }
}

bool TeslaVehicle::release_bluetooth(Runtime &r)
{
#if WARP_BLUETOOTH_COMPILED
    if (!r.controller_used) { return true; }
    // Controller/host APIs execute only on the separately owned lifecycle task.
    // This teardown task is supervised by main; never reclaim either context
    // after a timeout or SDK failure without the existing ACK/drain barrier.
    bluetooth.request_stop();
    const auto begin = millis();
    while (!bluetooth.stopped() && !bluetooth.failed() && millis() - begin < 15000) { vTaskDelay(pdMS_TO_TICKS(10)); }
    if (!bluetooth.stopped()) {
        strlcpy(r.teardown_error, "Bluetooth teardown failed; ownership retained; reboot required", sizeof(r.teardown_error)); return false;
    }
    gatt_owner.store(nullptr);
    return true;
#else
    (void)r;
    return true; // Unsupported builds cannot acquire the controller.
#endif
}

void TeslaVehicle::teardown_entry(void *context)
{
    auto *owner = static_cast<TeslaVehicle *>(context);
    Runtime *r = owner->tesla->runtime_owner.get();
    const bool ok = owner->release_bluetooth(*r);
    bluetooth_memory::sample(bluetooth_memory::Stage::TeardownStopped,true);
    r->teardown.complete(ok);
    // No runtime/BLE access after completion; only an acknowledged task is deleted.
    for (;;) { vTaskSuspend(nullptr); }
}

void TeslaVehicle::stop_runtime()
{
    tesla->runtime_owner.begin_stop([this](Runtime &r) {
        tesla->callback_gate.close();
        r.stop_requested.store(true);
        r.deferred_soc.reset(); r.deferred_attempts = 0;
        r.teardown.begin(millis());
        tesla->state.get("stopping")->updateBool(true);
        tesla->state.get("connected")->updateBool(false);
        tesla->state.get("pairing_pending")->updateBool(false);
        tesla->state.get("pair_request_sent")->updateBool(false);
    });
    Runtime *r = tesla->runtime_owner.get();
    if (r == nullptr) {
        return;
    }
    const bool stopped = r->worker_stopped.load(std::memory_order_acquire);
    // Cancellation can hold outcome while waiting for session; never wait here.
    // Unknown classification stays pending, but teardown supervision continues.
    const auto stop_state = r->command_outcome.stop_state();
    if (r->storage->restart_required() || (stop_state && (stop_state->deletion_incomplete || stop_state->exhausted))) {
        tesla->runtime_owner.require_restart();
        tesla->state.get("restart_required")->updateBool(true);
    }
    if (stopped) {
        // Completed erase is an authoritative negative pairing fact even when
        // MAX-order KeyRemoved was not consumed and the final probe is unknown.
        if (stop_state && stop_state->deletion_complete) {
            tesla->state.get("paired")->updateBool(false);
        }
        const auto *outcome = r->shutdown_result.read();
        if (outcome && !r->shutdown_presence_applied &&
            (outcome->final_presence || tesla_ble_publication_newer(outcome->presence_order, r->pairing_order))) {
            r->shutdown_presence_applied = true;
            if (outcome->presence_order > r->pairing_order) { r->pairing_order = outcome->presence_order; }
            tesla->state.get("key_present")->updateBool(outcome->key_present);
            tesla_ble_publish_presence(tesla->state.get("key_presence"), outcome->key_present, outcome->key_known, outcome->cleanup_required);
            // Final negative material authority invalidates possession now,
            // even if erase/reconstruction or retained teardown later fails.
            // Present/Unknown cannot revoke an authenticated pairing fact.
            if (outcome->cleanup_required || (outcome->key_known && !outcome->key_present)) {
                tesla->pairing_policy.on_key_loaded(false);
                tesla->state.get("paired")->updateBool(false);
            }
        }
        if (outcome && !r->supervision_failed && tesla_ble_publication_newer(outcome->error_order, r->error_order)) {
            r->error_order = outcome->error_order; publish_error(outcome->error);
        }
        if (outcome && outcome->publication_exhausted) {
            tesla->runtime_owner.require_restart(); tesla->state.get("restart_required")->updateBool(true);
            if (!r->supervision_failed) {
                r->error_order = UINT64_MAX; // Terminal fence, not an emitted identity.
                publish_error("Tesla identity/publication sequence exhausted; reboot required");
            }
        }
    }
    const auto completion = r->teardown.poll(millis(), stopped, tesla->callback_gate.drained(), [this, r]() {
        vTaskDelete(r->task_handle);
        r->task_handle = nullptr;
        return xTaskCreatePinnedToCore(teardown_entry, "tesla_teardown", 4096, this, 1,
                                       &r->teardown_handle, 0) == pdPASS;
    });
    if (completion == TeslaBleTeardown::Result::Failed) {
        tesla->runtime_owner.require_restart();
        tesla->state.get("restart_required")->updateBool(true);
        if (!r->supervision_failed && r->teardown.completed_result() == TeslaBleTeardown::Result::Failed && r->teardown_error[0] != '\0') {
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
    if (tesla->runtime_owner.finish_stop(stopped, tesla->callback_gate.drained(), r->controller_used || r->supervision_failed,
                                  [](Runtime &runtime) { return runtime.teardown.succeeded(); })) {
        // Destroying Runtime releases protocol, adapter references, strings,
        // flags and both queues. Keys/configuration remain on disk untouched.
        tesla->pairing_policy.on_key_loaded(tesla->state.get("key_present")->asBool());
        tesla->state.get("paired")->updateBool(false);
        tesla->state.get("running")->updateBool(false);
        tesla->state.get("stopping")->updateBool(false);
        tesla->state.get("restart_required")->updateBool(tesla->runtime_owner.restart_required());
    }
}

void TeslaVehicle::update_active_vehicle()
{
    Runtime *r = tesla->runtime_owner.get();
    const auto identity = r->session.identity();
    if (!identity) { return; } // Contention is not unplugging; retry next iteration.
    if (identity == r->observed_identity) {
        return;
    }

    r->observed_identity = *identity;
    r->policy.on_vehicle_changed(identity->generation, identity->index);
#if TESLA_BLE_MANUAL_VEHICLE_UI
    r->manual_poll_pending.store(false);
    r->poll_requested.store(false);
    r->auto_poll_requested.store(false);
#endif
    r->in_flight_started_ms = 0;
    r->next_poll_ms = millis();

    // Clear pending protocol work on every identity change, same profile too.
    r->trace_cause(Runtime::TraceCause::IdentityChange, 0);
    disconnect_internal();
}

void TeslaVehicle::process_commands()
{
#if TESLA_BLE_MANUAL_VEHICLE_UI
    refresh_live_session();
    update_active_vehicle(); // Recheck even when main has not yet run since SLAC changed.
#endif
    if (check_worker_exhaustion()) { return; }
    Runtime *r = tesla->runtime_owner.get();
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
        r->pair_terminal_fault = false;
        r->reset_pair_diagnostics();
        r->trace_arm();
#if TESLA_BLE_MANUAL_VEHICLE_UI
        if (!active_identity_matches(*captured) || *captured != accepted->vehicle || captured->index != r->configured_ev_index ||
            captured->mac != r->profile_mac || !captured->mac) {
            cancel_pairing("SLAC identity changed before profile/key mutation", r->pair_command); return;
        }
#endif
        const uint64_t current_mac = captured->mac;
        tesla_clean::IdentityLease current_key;
        const auto encoded_mac = encode_vehicle_mac(current_mac);
#if TESLA_BLE_MANUAL_VEHICLE_UI
        if (r->authorized.load()) {
            cancel_pairing("This profile is already authorized; use manual SoC query, not re-pairing", r->pair_command); return;
        }
        if (!runtime_binding_matches(true)) {
            cancel_pairing("Fresh VIN search match changed before key generation",r->pair_command);return;
        }
        if (!tesla_clean::KeyStorage::committed(r->storage->save_transaction("vehicle_vin",
                reinterpret_cast<const uint8_t *>(r->vin.c_str()), 17))) {
            cancel_pairing("Could not persist profile VIN", r->pair_command); return;
        }
#endif
        if (*captured != accepted->vehicle || captured->index != r->configured_ev_index || current_mac == 0) {
            cancel_pairing("Configured Tesla is not currently connected", r->pair_command);
        }
#if TESLA_BLE_MANUAL_VEHICLE_UI
        else if (!runtime_binding_matches(true)) {
            cancel_pairing("VIN search or active profile changed before key generation",r->pair_command);
        }
#endif
        else if (!r->keys || (!r->keys->acquire(current_key) &&
                   !r->storage->initialize_identity(*r->keys, {TeslaBleTransportAdapter::random, nullptr}))) {
            cancel_pairing("Could not durably initialize Tesla pairing key", r->pair_command);
        } else if (!ensure_connected()) {
            cancel_pairing("Could not connect to Tesla for pairing", r->pair_command,
                r->operation.retired() || r->retirement_operation.retired() ? TeslaBleCommandOutcome::PairOutcome::InternalFailure : TeslaBleCommandOutcome::PairOutcome::ConnectionFailed);
        } else if (r->stop_requested.load() || r->session.identity() != captured) {
            cancel_pairing("Vehicle changed during pairing connection", r->pair_command);
        } else if (!tesla_clean::KeyStorage::committed(r->storage->save_transaction("warp_ev_mac", encoded_mac.data(), encoded_mac.size()))) {
            cancel_pairing("Could not persist WARP vehicle MAC binding", r->pair_command);
        } else {
            r->trace_stage(Runtime::TraceStage::Connected);
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
                r->pair_result.store(static_cast<int>(result.code));
                if (result.code == tesla_clean::VehicleResultCode::PairRequestSent) {
                    r->trace.mark_sent(millis());
                    r->trace_stage(Runtime::TraceStage::Sent);
                }
                const int32_t mismatch = (r->stop_requested.load() ? 1 : 0) | (result.context != request.request ? 2 : 0) |
                    (result.generation != r->connection_generation.load() ? 4 : 0) | (!r->session.matches(request) ? 8 : 0);
                if (mismatch) {
                    r->trace_cause(Runtime::TraceCause::SendCompletion, mismatch);
                    cancel_pairing("Pairing link/session lost; transmission may have occurred; approval not authenticated", command, TeslaBleCommandOutcome::PairOutcome::LinkLost); return;
                }
                r->pair_sending = false;
                if (result.code == tesla_clean::VehicleResultCode::PairRequestSent) {
                    r->command_outcome.pair_sent(command);
                    post_event(EventType::PairStarted);
                    r->pair_verifying = true;
                    r->pair_started_ms = millis(); r->pair_next_ms = millis() + 1000;
                    r->trace_stage(Runtime::TraceStage::Verifying);
                    queue_pair_trace(command, "sent");
                } else { cancel_pairing("Tesla pairing transmission failed; approval not authenticated", command); }
            });
            r->pair_sending = ready &&
#if TESLA_BLE_MANUAL_VEHICLE_UI
                runtime_binding_matches(true) &&
#endif
                // ATT Write-With-Response fragments share one bounded send
                // budget. The core's 500ms default is below a valid multi-write
                // exchange; use the module's supervised 10s operation budget.
                r->vehicle->pair(true, request.request, std::move(completion), {15000, 1000, 10000});
            if (!r->pair_sending) {
                r->session.disarm(request);
                cancel_pairing("Tesla pairing command refused; no pairing is pending", command);
            }
        }
    }

    if (r->poll_requested.exchange(false)) {
        request_soc(true);
    }
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if (r->auto_poll_requested.exchange(false)) {
        request_soc(false);
    }
#endif
    check_worker_exhaustion();
}

void TeslaVehicle::set_poll_outcome(Runtime *r, PollOutcome outcome, bool automatic)
{
    r->poll_outcome.store(static_cast<uint8_t>(outcome));
    r->poll_outcome_automatic.store(automatic);
    r->poll_outcome_sequence.fetch_add(1);
}

void TeslaVehicle::request_soc(bool manual)
{
    if (check_worker_exhaustion()) { return; }
    Runtime *r = tesla->runtime_owner.get();
#if TESLA_BLE_MANUAL_VEHICLE_UI
    // An automatic operation that cannot start now ends without radio contact;
    // the main-task scheduler tries again at its next interval. Never queued.
    const bool automatic = !manual;
    const auto defer_soc_request = [r, manual]() {
        if (manual) { r->poll_requested.store(true); return; }
        set_poll_outcome(r, PollOutcome::Busy, true); r->manual_poll_pending.store(false);
    };
    const auto outcome = [r](PollOutcome value, bool is_automatic) { set_poll_outcome(r, value, is_automatic); };
#else
    const bool automatic = false;
    const auto defer_soc_request = [r, manual]() { if (manual) { r->poll_requested.store(true); } };
    const auto outcome = [](PollOutcome, bool) {};
#endif
    if (!r->command_outcome.forward_available()) { defer_soc_request(); return; }
    if (r->in_flight_started_ms != 0 || r->pair_sending || r->pair_verifying || r->pair_status_active || r->pair_approval_waiting.load()) {
        defer_soc_request();
        return;
    }
    const auto captured = r->session.identity();
    if (!captured) { defer_soc_request(); return; }
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if (!active_identity_matches(*captured)) {
        r->manual_poll_pending.store(false);
        if (manual) { post_event(EventType::Error, "SLAC identity changed before SoC action"); }
        outcome(PollOutcome::Failed, automatic); return;
    }
#endif
    if (captured->mac == 0 || captured->mac != r->bound_vehicle_mac.load() ||
        captured->index != r->configured_ev_index) {
        if (manual) {
            post_event(EventType::Error, "Connected vehicle MAC is not bound to this Tesla key");
        }
        outcome(PollOutcome::Failed, automatic);
        r->next_poll_ms = millis() + r->poll_interval_seconds * 1000U;
        r->manual_poll_pending.store(false);
        return;
    }
    r->in_flight = tesla_ble_prepare_request(r->session, r->epoch, *captured, r->configured_ev_index,
        [this, r]() { return ensure_connected() && !r->stop_requested.load(); });
    if (r->in_flight.request == 0) {
        if (check_worker_exhaustion()) { return; }
        post_event(EventType::Error, "Could not connect to current Tesla over BLE");
        outcome(PollOutcome::Failed, automatic);
        r->next_poll_ms = millis() + r->poll_interval_seconds * 1000U;
        r->manual_poll_pending.store(false);
        return;
    }
    // Manual requests may wake when needed; automatic ones only with the
    // profile's explicit wake permission. The policy spaces wakes over time.
    TeslaBleSocRequestDecision decision = r->policy.begin_request(captured->generation, r->configured_ev_index,
        manual || r->wake_once, millis());
    if (!decision.allowed || r->in_flight.request == 0) {
        r->policy.request_refused(decision, millis()); // Nothing enqueued: release an unused wake reservation.
        r->session.disarm(r->in_flight);
        r->in_flight = {};
        r->in_flight_started_ms = 0;
        if (manual) {
            post_event(EventType::Error, "Configured EV profile is not currently connected");
        }
        outcome(PollOutcome::Failed, automatic);
        r->next_poll_ms = millis() + r->poll_interval_seconds * 1000U;
        r->manual_poll_pending.store(false);
        return;
    }


    r->in_flight_started_ms = millis() ? millis() : 1;
    const auto request = r->in_flight;
    tesla_clean::VehicleCompletion completion;
    const bool ready = completion.assign([this, r, request, decision, manual, automatic, outcome](const tesla_clean::VehicleResult &result) {
        if (result.context != request.request) { return; }
        // Settle the wake reservation from the controller's own wire fact before
        // any publication filter: a retired link/session does not undo a
        // possibly exposed wake, and an unused reservation is released.
        r->policy.settle_wake(decision, result.wake_exposed, millis());
        if (result.generation != r->connection_generation.load() ||
            r->stop_requested.load() || !r->session.matches(request)) {
            // Link/session/identity retired: never published as a result, but the
            // operation's terminal status is still visible (no silent outcome).
            outcome(result.wake_exposed ? PollOutcome::WokenNoSoc : PollOutcome::Failed, automatic);
            return;
        }
        const bool soc = result.code == tesla_clean::VehicleResultCode::Soc && result.authenticated;
        if (soc && r->policy.accept_result(request.vehicle.generation, request.vehicle.index, result.battery)) {
            post_event(EventType::Soc, nullptr, result.battery, request, true, true);
            outcome(PollOutcome::Ok, automatic);
        } else {
            r->session.disarm(request);
            r->policy.on_transport_disconnected();
            // Explicit outcomes; the last published SoC stays unchanged and is never refreshed here.
            const bool asleep = result.code == tesla_clean::VehicleResultCode::SkippedAsleep;
            const bool woken = !asleep && !soc && result.wake_exposed;
            outcome(asleep ? (decision.wake_limited ? PollOutcome::AsleepWakeLimited : PollOutcome::AsleepWakeDisabled) :
                    woken ? PollOutcome::WokenNoSoc : PollOutcome::Failed, automatic);
            // A sleeping vehicle without wake permission is a normal automatic
            // outcome (poll_outcome), not an error banner.
            const char *failure = asleep ?
                (manual ? "Tesla asleep; wake skipped, last wake less than 2 min ago; SoC not updated" : nullptr) :
                woken ? "Tesla wake sent but no SoC received in time; SoC not updated" :
                "Tesla authenticated SoC request failed";
            if (failure) { post_event(EventType::Error, failure); }
        }
        r->in_flight_started_ms = 0;
        r->manual_poll_pending.store(false);
    });
    const bool accepted = ready && r->vehicle->charge_state_poll(decision.allow_wake ? tesla_clean::WakePolicy::WakeIfNeeded
                                                   : tesla_clean::WakePolicy::NoWakeSkip, r->in_flight.request, std::move(completion),
                                                   {RESPONSE_TIMEOUT_MS, 5000, 10000});
    if (!accepted) {
        r->policy.request_refused(decision, millis());
        check_worker_exhaustion();
        r->session.disarm(r->in_flight);
        r->in_flight = {};
        r->in_flight_started_ms = 0;
        post_event(EventType::Error, "Tesla SoC command refused; no request is in flight");
        outcome(PollOutcome::Failed, automatic);
        r->manual_poll_pending.store(false);
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
    Runtime *r = tesla->runtime_owner.get();
    if (!r->pair_verifying) { return; }
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if (!active_identity_matches(r->pair_request.vehicle)) {
        cancel_pairing("SLAC identity changed before pairing verification", r->pair_command); return;
    }
#endif
    const bool session_lost = r->stop_requested.load() || !r->session.matches(r->pair_request);
    tesla_clean::VcsecStatus response;
    if (!session_lost && millis() - r->pair_started_ms < 60000 && r->command_outcome.pair_current(r->pair_command) &&
        r->vehicle->take_pairing_status(r->pair_request.request, r->connection_generation.load(), response)) {
        r->trace_stage(Runtime::TraceStage::Response);
        r->command_outcome.pair_response(r->pair_command, response.information, response.outer_operation,
            response.whitelist_operation, response.whitelist, response.operation == tesla_clean::VcsecOperation::Waiting);
        if (response.operation == tesla_clean::VcsecOperation::Error && response.information != 13) {
            char message[96];
            snprintf(message, sizeof(message), "VCSEC whitelist information=%lu; unauthenticated current-link response", static_cast<unsigned long>(response.information));
            // Freeze the actual first response before CLOSE callbacks/errors.
            // Admission stays closed through the complete physical drainage.
            r->command_outcome.begin_worker_cleanup();
            cancel_pairing(message, r->pair_command, TeslaBleCommandOutcome::PairOutcome::VehicleRejected);
            disconnect_internal(false);
            r->command_outcome.end_cleanup();
            return;
        }
        // NONE/already-listed are not authenticated success. Continue the
        // existing verification without re-sending PRESENT_KEY.
    }
    if (session_lost || millis() - r->pair_started_ms >= 60000) {
        // Classify before disconnect_internal retires the session itself. A real
        // session/link failure must never be presented as ordinary card expiry.
        if (session_lost) { r->trace_cause(Runtime::TraceCause::VerifySession, r->stop_requested.load() ? 1 : 2); }
        r->command_outcome.retire_pair();
        disconnect_internal(false);
        cancel_pairing(session_lost ? "Pairing verification link/session lost; approval not authenticated" :
            "Approval deadline expired; approval not authenticated", r->pair_command,
            session_lost ? TeslaBleCommandOutcome::PairOutcome::LinkLost : TeslaBleCommandOutcome::PairOutcome::LocalExpiry);
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
            r->trace_cause(Runtime::TraceCause::VerifySession, 4);
            cancel_pairing("Pairing verification link/session lost; approval not authenticated", command, TeslaBleCommandOutcome::PairOutcome::LinkLost); return;
        }
        r->pair_status_active = false;
        if (result.code == tesla_clean::VehicleResultCode::KeyDenied) { r->trace.count_denied(); }
        if (result.code == tesla_clean::VehicleResultCode::Status && result.authenticated) {
#if TESLA_BLE_MANUAL_VEHICLE_UI
            const uint8_t authorized = 1;
            if (!tesla_clean::KeyStorage::committed(r->storage->save_transaction("authorized", &authorized, 1))) {
                cancel_pairing("Authenticated approval could not be persisted; key retained", command, TeslaBleCommandOutcome::PairOutcome::StorageFailure); return;
            }
            r->authorized.store(true);
#endif
            r->pair_verifying = false;
            r->vehicle->end_pairing_status(request.request);
            r->pair_approval_waiting.store(true);
            r->trace_stage(Runtime::TraceStage::Approved);
            queue_pair_trace(command, "approved");
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
    if (r->pair_status_active) { r->trace.count_poll(); }
    if (!r->pair_status_active) { r->pair_next_ms = millis() + 1000; }
}

void TeslaVehicle::cancel_pairing(const char *message, uint64_t command, TeslaBleCommandOutcome::PairOutcome reason)
{
    Runtime *r = tesla->runtime_owner.get();
    const auto presence = r->storage->private_key_presence();
#if TESLA_BLE_MANUAL_VEHICLE_UI
    r->profile_key_present.store(presence == TeslaBleStorageAdapter::KeyPresence::Present);
#endif
    const auto order = r->command_outcome.cancel_pair(command, message,
        presence == TeslaBleStorageAdapter::KeyPresence::Present,
        presence != TeslaBleStorageAdapter::KeyPresence::Unknown,
        presence == TeslaBleStorageAdapter::KeyPresence::CleanupRequired, [r, message]() {
        r->session.disarm(r->pair_request);
        r->pair_requested.store(false);
        r->pair_sending = r->pair_verifying = r->pair_status_active = false;
        r->pair_approval_waiting.store(false);
        strlcpy(r->terminal_error, message, sizeof(r->terminal_error));
        r->pair_terminal_fault = true;
    }, reason);
    if (order) { r->terminal_error_order = order; }
    if (order && r->vehicle) { r->vehicle->end_pairing_status(r->pair_request.request); }
    if (order) { queue_pair_trace(command, TeslaBleCommandOutcome::outcome_name(reason)); }
}

static const char *tesla_pair_stage_name(uint32_t stage)
{
    static const char *const names[] = {"none", "claimed", "connected", "sent", "verifying", "response", "approved"};
    return stage < sizeof(names) / sizeof(names[0]) ? names[stage] : "unknown";
}

static const char *tesla_pair_cause_name(uint32_t cause)
{
    static const char *const names[] = {"none", "remote_disconnect", "security", "rx_overflow", "frame_refused", "send_completion",
        "verify_session", "identity_change", "profile_reload", "stop", "operation_timeout", "operation_cancelled", "unavailable"};
    return cause < sizeof(names) / sizeof(names[0]) ? names[cause] : "unknown";
}

#ifdef TESLA_BLE_MODULE_HOST_TEST
#define TESLA_BLE_PAIR_TRACE 1
inline std::vector<std::string> host_pair_trace_log;
#endif

void TeslaVehicle::queue_pair_trace(uint64_t command, const char *outcome)
{
    // Worker-only, after the terminal (or "sent") decision. Copies numbers and a
    // static outcome literal only: never MAC/VIN/BLE address/key/payload bytes.
    // Formatting is deferred to flush_pair_trace() at a shallow worker frame.
    Runtime *r = tesla->runtime_owner.get();
    // Log lifecycle, not the write gate: a DIRTY successor attempt is active but
    // unarmed and must still log once (unavailable); inactive/terminal never logs.
    if (!r || !r->trace_logging) { return; }
    r->trace_sample(); // gate-admitted: no-op unless ARMED
    const bool sent = !strcmp(outcome, "sent");
    // Freeze this attempt's first-cause evidence: no later write is admitted;
    // a write still in flight here makes the record Unavailable (never mixed).
    if (!sent) { r->trace.freeze(); r->trace_logging = false; }
    if (r->trace_entry_count >= sizeof(r->trace_entries) / sizeof(r->trace_entries[0])) {
        if (r->trace_entries_dropped != UINT32_MAX) { ++r->trace_entries_dropped; }
        return;
    }
    auto &e = r->trace_entries[r->trace_entry_count++];
    e.outcome = outcome; e.command = uint32_t(command); e.acked = r->pair_tx_acked.load(); e.total = r->pair_tx_total.load();
    // The worker is the only resetter/freezer, so its own snapshot cannot overlap
    // one; the fallback still never logs a mixed record.
    if (!r->trace.snapshot(e.trace)) { e.trace = Runtime::TraceSnapshot{}; e.trace.cause = uint32_t(Runtime::TraceCause::Unavailable); }
}

void TeslaVehicle::flush_pair_trace()
{
    Runtime *r = tesla->runtime_owner.get();
    if (!r) { return; }
    for (uint8_t i = 0; i < r->trace_entry_count; ++i) {
        const auto &e = r->trace_entries[i];
        const auto &t = e.trace;
        char line[176];
        if (!strcmp(e.outcome, "sent")) {
            snprintf(line, sizeof(line), "Tesla pairing #%u: request sent, %u/%u bytes ATT-acknowledged; awaiting authenticated approval",
                unsigned(e.command), unsigned(e.acked), unsigned(e.total));
        } else {
            snprintf(line, sizeof(line),
                "Tesla pairing #%u: %s stage=%s loss=%s/%s/%d t=%u disc=%d polls=%u denied=%u rx=%u/%u/%u/%u tx=%u/%u",
                unsigned(e.command), e.outcome, tesla_pair_stage_name(t.stage), tesla_pair_cause_name(t.cause),
                tesla_pair_stage_name(t.loss_stage), int(t.detail), unsigned(t.elapsed), int(t.disconnect),
                unsigned(t.polls), unsigned(t.denied), unsigned(t.notifications), unsigned(t.frames),
                unsigned(t.pair_replies), unsigned(t.session_replies), unsigned(e.acked), unsigned(e.total));
        }
#ifdef TESLA_BLE_MODULE_HOST_TEST
        host_pair_trace_log.emplace_back(line);
#else
        logger.printfln("%s", line);
#endif
    }
    r->trace_entry_count = 0;
    if (r->trace_entries_dropped) {
#ifdef TESLA_BLE_MODULE_HOST_TEST
        host_pair_trace_log.emplace_back("Tesla pairing trace lines dropped");
#else
        logger.printfln("Tesla pairing trace: %u lines dropped", unsigned(r->trace_entries_dropped));
#endif
        r->trace_entries_dropped = 0;
    }
}

void TeslaVehicle::publish_pairing_state()
{
    Runtime *r = tesla->runtime_owner.get();
    const auto pair = r->command_outcome.take_pair_state([r](const TeslaBleRequestIdentity &approval) {
        return r->session.consume(approval,
            current_ev_identity(),
            r->configured_ev_index, true, true, 50, [](float) {});
    });
    if (!pair) { return; } // Contention never means idle.
    if (pair->command) {
        tesla->state.get("pair_outcome")->updateString(TeslaBleCommandOutcome::outcome_name(pair->outcome));
        tesla->state.get("pair_response_seen")->updateBool(pair->response_seen);
        tesla->state.get("pair_response_whitelist")->updateBool(pair->response_whitelist);
        tesla->state.get("pair_response_code")->updateUint(pair->response_code);
        tesla->state.get("pair_response_outer")->updateUint(pair->response_outer);
        tesla->state.get("pair_response_operation")->updateUint(pair->response_operation);
    }
    using Phase = TeslaBleCommandOutcome::PairPhase;
    const bool terminal = pair->completed;
    if (terminal) {
        const bool approved = pair->phase == Phase::Approval;
        // ACK ownership regardless of display order; an older terminal must not
        // undo a newer authenticated SoC already consumed from the event queue.
        if (tesla_ble_publication_newer(pair->order, r->pairing_order)) {
            r->pairing_order = pair->order;
            tesla->pairing_policy.on_key_loaded(pair->key_present);
            if (approved) { tesla->pairing_policy.on_authenticated_response(); }
            else { tesla->pairing_policy.on_pairing_error(); }
            tesla->state.get("key_present")->updateBool(pair->key_present);
            tesla_ble_publish_presence(tesla->state.get("key_presence"), pair->key_present, pair->key_known, pair->cleanup_required);
            tesla->state.get("paired")->updateBool(approved);
        }
        if (tesla_ble_publication_newer(pair->order, r->error_order)) {
            r->error_order = pair->order;
            publish_error(approved ? "" : pair->error[0] ? pair->error :
                "Pairing approval cancelled by session change; request may have been sent; not authenticated");
        }
        r->pair_approval_waiting.store(false);
    }
    const bool pending = pair->command && !terminal;
    tesla->state.get("pairing_pending")->updateBool(pending);
    tesla->state.get("pair_request_sent")->updateBool(pending && pair->sent);
    if (r->command_outcome.take_deleted()) {
        tesla->pairing_policy.on_key_removed();
        tesla->state.get("key_present")->updateBool(false);
        tesla->state.get("key_presence")->updateString("absent");
        tesla->state.get("paired")->updateBool(false);
        tesla->state.get("pairing_pending")->updateBool(false);
        tesla->state.get("pair_request_sent")->updateBool(false);
    }
}

bool TeslaVehicle::enqueue_rx(QueueHandle_t queue, uint64_t generation, const uint8_t *data, size_t size, uint32_t *peak)
{
    // Compile-time proof of the RX intake contract in tesla_vehicle.h: for every
    // admissible full notification size n (20..512) the slot cost satisfies
    // 23 * ceil(n / 22) <= 2 * n, so a guaranteed burst needs at most
    // floor(2 * RX_GUARANTEE_BYTES / 23) full-notification slots plus one slot per
    // short notification. A bare depth literal is not accepted as the proof.
    constexpr auto slot_cost_lemma = [] {
        for (size_t n = RX_FULL_PAYLOAD_MIN; n <= RX_NOTIFICATION_MAX; ++n) {
            if (23 * ((n + RX_CHUNK_BYTES - 1) / RX_CHUNK_BYTES) > 2 * n) { return false; }
        }
        return true;
    };
    static_assert(slot_cost_lemma(), "per-notification chunk waste bound must hold for every full notification size");
    static_assert(RX_QUEUE_DEPTH >= 2 * RX_GUARANTEE_BYTES / 23 + RX_GUARANTEE_SHORT, "RX queue must hold the guaranteed burst at worst-case fragmentation");
    static_assert(RX_GUARANTEE_BYTES >= 1024 + 2, "guaranteed burst contains one maximum framed message");
    static_assert(RX_FULL_PAYLOAD_MIN - 1 <= RX_CHUNK_BYTES, "a short notification costs exactly one slot");
    // Single producer (BLE host callback). Free space can only grow until these
    // sends finish, so the up-front check makes one notification all-or-nothing.
    // A refusal still invalidates the session at the caller: dropping any byte
    // corrupts the framed stream. Never blocks the host task.
    const size_t chunks = (size + RX_CHUNK_BYTES - 1) / RX_CHUNK_BYTES;
    if (!queue || !data || !size || size > RX_NOTIFICATION_MAX || uxQueueSpacesAvailable(queue) < chunks) { return false; }
    for (size_t offset = 0; offset < size; offset += RX_CHUNK_BYTES) {
        RxFragment fragment;
        fragment.connection_generation = generation;
        fragment.length = static_cast<uint16_t>(std::min(RX_CHUNK_BYTES, size - offset));
        memcpy(fragment.data, data + offset, fragment.length);
        if (xQueueSend(queue, &fragment, 0) != pdTRUE) { return false; }
    }
    if (peak) {
        const uint32_t used = static_cast<uint32_t>(RX_QUEUE_DEPTH - uxQueueSpacesAvailable(queue));
        if (used > *peak) { *peak = used; }
    }
    return true;
}

size_t TeslaVehicle::drain_rx()
{
    // Worker-only. Same filters for the ordinary loop and WRITE-wait service.
    Runtime *r = tesla->runtime_owner.get();
    if (r->rx_dispatching) { return 0; } // never nest inside a dispatching drain
    r->rx_dispatching = true;
    RxFragment fragment;
    size_t consumed = 0;
    for (size_t batch = 0; batch < RX_QUEUE_DEPTH && xQueueReceive(r->queues.rx(), &fragment, 0) == pdTRUE; ++batch) {
        ++consumed;
        if (r->stop_requested.load() || fragment.connection_generation != r->connection_generation.load()) {
            continue;
        }
        r->vehicle->receive({fragment.data, fragment.length}, fragment.connection_generation);
    }
    r->rx_dispatching = false;
    return consumed;
}

void TeslaVehicle::service_rx_during_write(uint64_t generation, const TeslaBleIdentity &identity)
{
    // Worker task only, inside gatt_operation's WRITE wait. Admitted ONLY while
    // the protocol Vehicle owns an accepted frame transmission: every complete
    // received frame is then deferred (Vehicle::emitted) and dispatched after
    // the TX by drain(tx). No protocol decision, completion, GATT submission or
    // operation-slot mutation can run here, and no nested drain (rx_dispatching).
    // Same stop/generation/identity policy as submission and process_rx.
    Runtime *r = tesla->runtime_owner.get();
    if (!r->vehicle || !r->vehicle->transmitting() || r->rx_dispatching || r->stop_requested.load() ||
        !r->connected.load() || r->connection_generation.load() != generation) { return; }
    const auto latest = r->session.identity();
    if (!latest || *latest != identity) { return; }
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if (!active_identity_matches(identity) || !runtime_binding_matches(true)) { return; }
#endif
    const size_t consumed = drain_rx();
    r->rx_write_serviced = consumed > UINT32_MAX - r->rx_write_serviced ? UINT32_MAX : r->rx_write_serviced + uint32_t(consumed);
}

void TeslaVehicle::process_rx()
{
    drain_rx();
    tesla->runtime_owner.get()->trace_sample();
    check_worker_exhaustion();
}

bool TeslaVehicle::check_worker_exhaustion()
{
    Runtime *r = tesla->runtime_owner.get();
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
    Runtime *r = tesla->runtime_owner.get();
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
#if TESLA_BLE_MANUAL_VEHICLE_UI
        r->profile_key_present.store(type == EventType::PairStarted);
#endif
        r->terminal_error[0] = '\0';
        r->command_outcome.publish(type == EventType::KeyRemoved ? OutcomeKind::KeyRemoved : OutcomeKind::PairStarted,
                                   type == EventType::PairStarted);
        return;
    }
    if (type == EventType::Error) {
        // Preserve the initiating failure through CLOSE/teardown, not forever:
        // a later independent action must still be able to report its own fault.
        if (r->pair_terminal_fault && (r->closing_link.load() || r->stop_requested.load())) { return; }
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
    if (type == EventType::Soc) {
        // Only caller: the worker's accepted authenticated SoC result. Receive time
        // is fixed here; Rtc::clock_synced() is gettimeofday() plus a constant
        // bound check (no Rtc member state), so it is safe from the worker task.
        event.received_uptime = now_us();
        event.received_unix = tesla_wall_clock_seconds();
    }
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
    strlcpy(tesla->pending_error, message ? message : "", sizeof(tesla->pending_error));
    tesla->error_pending = true;
    flush_error();
}

void TeslaVehicle::flush_error()
{
    if (tesla->error_pending && tesla->state.get("last_error")->updateStringChecked(tesla->pending_error)) { tesla->error_pending = false; }
}

void TeslaVehicle::loop()
{
#if TESLA_BLE_R13_MASTER_BENCH
#if TESLA_BLE_MANUAL_WEB_BENCH
    if (service_start_requested) {
        service_start_requested = false;
        tesla_ble_iram_snapshot("module.loop", "boot");
        master_bench.start(nullptr); // Exactly one shared lifecycle; no peer handler.
    }
#endif
    master_bench.supervise();
    publish_bluetooth_state();
#if !TESLA_BLE_MANUAL_WEB_BENCH
    bench_serial();
#endif
    // Dispatch stop only AFTER the USB/API handler returns and its ACK is
    // printed; no half-torn-down controller state can race into the restart ACK.
    if (bench_restart_pending && master_bench.active()) { master_bench.request_stop(); }
    if (bench_restart_pending && master_bench.stopped()) {
        bench_restart_pending = false;
        bench_emit("teardown", true);
        trigger_reboot("Bluetooth bench verified teardown");
    }
    return; // No Tesla runtime, EV/session, key/storage or old bench operations.
#endif
    bluetooth.supervise();
#if TESLA_BLE_MANUAL_VEHICLE_UI
    publish_discovery();
#endif
    publish_bluetooth_state();
#if TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
    return; // Registration is ordinary; no Tesla runtime/callback/peer call closure.
#endif
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS && !TESLA_BLE_R13_MASTER_BENCH && !TESLA_BLE_PSRAM_POLICY_DIAGNOSTIC
    tesla_ble_bench_smoke_loop();
    tesla_ble_bench_discovery.supervise();
#endif
    if (bluetooth_restart_pending) {
        if (tesla && tesla->runtime_owner.get()) { stop_runtime(); return; }
        bluetooth.request_stop();
        if (bluetooth.stopped()) {
            bluetooth_restart_pending = false;
            trigger_reboot("Bluetooth verified teardown");
        }
        return; // Failure/timeout retains ownership, never acknowledges reboot.
    }
    if (!tesla) { return; }
    flush_error();
#if TESLA_BLE_MANUAL_VEHICLE_UI
    publish_profile();
    publish_guidance();
    auto_poll();
#endif
    const bool changed = tesla->runtime_update_requested.exchange(false);
    if (changed) {
        tesla->start_attempted = false;
    }
    Runtime *r = tesla->runtime_owner.get();
    if (ev.session_exhausted()) { tesla->runtime_owner.require_restart(); }
    if (r && !r->storage_only && !bluetooth.active()) {
        tesla->runtime_owner.require_restart(); r->stop_requested.store(true);
    }
    if (tesla->runtime_owner.restart_required()) { tesla->state.get("restart_required")->updateBool(true); }
    if (r && (r->command_outcome.publication_exhausted() || r->session.exhausted() ||
              r->identity_exhausted.load() || ev.session_exhausted())) {
        tesla->runtime_owner.require_restart();
        r->stop_requested.store(true);
        tesla->state.get("restart_required")->updateBool(true);
        if (!r->supervision_failed) {
            publish_error("Tesla identity/publication sequence exhausted; reboot required");
        }
    }
    // Sample before retirement; later teardown must not erase this attempt's
    // first fault. No identifiers, key bytes or radio operations in diagnostics.
#if defined(TESLA_BLE_USE_NIMBLE) && TESLA_BLE_USE_NIMBLE
    if (r && !r->supervision_failed) {
        if (auto *central = bluetooth.central()) {
            const auto d = central->connect_diagnostic();
            tesla->state.get("connect_attempt")->updateUint(d.attempt);
            tesla->state.get("connect_flags")->updateUint(d.flags);
            tesla->state.get("connect_submit_rc")->updateInt(d.submit_rc);
            tesla->state.get("connect_cancel_rc")->updateInt(d.cancel_rc);
            tesla->state.get("connect_status")->updateInt(d.status);
        }
    }
#endif
    const auto supervision_now = millis();
    const bool operation_fault = r && r->operation.supervise(supervision_now);
    const bool retirement_fault = r && r->retirement_operation.supervise(supervision_now);
    if (r != nullptr && (operation_fault || retirement_fault || r->worker_watch.expired(supervision_now, 30000))) {
        // Snapshot only the first cause; later worker/teardown unwinding must
        // not overwrite the initiating deadline with a secondary symptom.
        if (!r->supervision_failed) {
            const bool operation = operation_fault, retirement = retirement_fault;
            const auto &slot = operation ? r->operation : r->retirement_operation;
            const bool cancelled = (operation || retirement) && slot.cause() == TeslaBleOperation::Cause::Cancelled;
            tesla->state.get("timeout_cause")->updateString(cancelled ? "undrained_cancel" : operation ? "gatt_operation" : retirement ? "link_retirement" : "worker_iteration");
            tesla->state.get("timeout_operation")->updateUint(operation || retirement ? slot.kind() : 0);
            tesla->state.get("timeout_elapsed_ms")->updateUint(operation || retirement ? slot.elapsed_ms(millis()) : r->worker_watch.elapsed_ms(millis()));
        }
        // Detect/quarantine only: this cannot interrupt an SDK queue/future wait.
        tesla->runtime_owner.require_restart();
        r->supervision_failed = true;
        r->stop_requested.store(true);
        tesla->state.get("restart_required")->updateBool(true);
        bool open_undrained = false;
#if defined(TESLA_BLE_USE_NIMBLE) && TESLA_BLE_USE_NIMBLE
        // OPEN whose SDK submission returned: the watchdog saw a missing
        // terminal/drain barrier, not a blocked submission or worker.
        if (operation_fault && r->operation.kind() == uint32_t(Open)) {
            if (auto *central = bluetooth.central()) { open_undrained = (central->connect_diagnostic().flags & 2) != 0; }
        }
#endif
        publish_error((operation_fault && r->operation.cause() == TeslaBleOperation::Cause::Cancelled) ||
                      (retirement_fault && r->retirement_operation.cause() == TeslaBleOperation::Cause::Cancelled) ?
            "Tesla BLE cancellation undrained; ownership retained; reboot required" :
            open_undrained ? "Tesla BLE connection attempt undrained; ownership retained; reboot required" :
            "Tesla BLE worker/submission timed out; ownership retained; reboot required");
    }
    if (r != nullptr && (changed || (!tesla->config.get("enable")->asBool() && r->service_enabled) ||
                         r->stop_requested.load() || tesla->runtime_owner.is_stopping())) {

        stop_runtime();
        return;
    }
    if (r == nullptr && service_start_requested && bluetooth.active() && !tesla->start_attempted && !tesla->runtime_owner.restart_required()) {
        service_start_requested = false;
        start_runtime();
        r = tesla->runtime_owner.get();
    }
    if (r == nullptr) {
        return;
    }
    r->session.publish(current_ev_identity());
    process_module_events();
}

void TeslaVehicle::publish_bluetooth_state()
{
#if TESLA_BLE_R13_MASTER_BENCH
    const bool bench_restart = bluetooth_supported() && (master_bench.failed() ||
        bluetooth_config.get("enable")->asBool() != bluetooth_boot_enabled);
    bluetooth_state.get("phase")->updateString(master_bench.phase());
    bluetooth_state.get("restart_required")->updateBool(bench_restart);
    bluetooth_state.get("restart_reason")->updateString(!bench_restart ? "" : master_bench.failed() ? "lifecycle_latched" : "configuration_changed");
    bluetooth_state.get("persistence_error")->updateBool(!bluetooth_commit_ok);
    bluetooth_state.get("legacy_tesla_enabled")->updateBool(false);
    bench_state.get("running")->updateBool(false);
    return;
#endif
    Runtime *r = tesla ? tesla->runtime_owner.get() : nullptr;
    const bool latched = bluetooth.failed() || (bluetooth_boot_enabled && bluetooth.stopped()) ||
        (tesla && tesla->runtime_owner.restart_required());
    const bool restart = bluetooth_supported() && (latched ||
        bluetooth_config.get("enable")->asBool() != bluetooth_boot_enabled);
    const char *phase = !bluetooth_supported() ? "disabled" : latched ? "error" :
        r && (tesla->runtime_owner.is_stopping() || r->stop_requested.load()) ? "stopping" : bluetooth.phase();
    bluetooth_state.get("phase")->updateString(phase);
    bluetooth_state.get("restart_required")->updateBool(restart);
    bluetooth_state.get("restart_reason")->updateString(!restart ? "" : latched ?
        "lifecycle_latched" : "configuration_changed");
    bluetooth_state.get("persistence_error")->updateBool(!bluetooth_commit_ok);
    bluetooth_state.get("legacy_tesla_enabled")->updateBool(tesla && tesla->config.get("enable")->asBool() && !bluetooth_boot_enabled);
    if (tesla) {
        // Event delivery is lossy and locally retiring a framed stream need not
        // produce a GAP-disconnected UI event. Reconcile the display with the
        // current logical link; never let historical Connected imply readiness.
        tesla->state.get("connected")->updateBool(r && !tesla->runtime_owner.is_stopping() &&
            !r->stop_requested.load() && r->connected.load());
        if(r) {
            const auto total=r->pair_tx_total.load(),acked=r->pair_tx_acked.load();
            tesla->state.get("pair_tx_total_bytes")->updateUint(total);
            tesla->state.get("pair_tx_attempted_bytes")->updateUint(r->pair_tx_attempted.load());
            tesla->state.get("pair_tx_acked_bytes")->updateUint(acked);
            tesla->state.get("pair_tx_complete")->updateBool(total>0 && acked==total);
            tesla->state.get("pair_result")->updateInt(r->pair_result.load());
            // First-cause trace: one bounded coherent snapshot of the gated
            // atomics. If it overlapped a worker reset/freeze change it is
            // discarded (previous coherent values stay); loop() republishes.
            Runtime::TraceSnapshot t;
            if (r->trace.snapshot(t)) {
                tesla->state.get("pair_stage")->updateString(tesla_pair_stage_name(t.stage));
                tesla->state.get("pair_loss_cause")->updateString(tesla_pair_cause_name(t.cause));
                tesla->state.get("pair_loss_stage")->updateString(tesla_pair_stage_name(t.loss_stage));
                tesla->state.get("pair_loss_detail")->updateInt(t.detail);
                tesla->state.get("pair_loss_elapsed_ms")->updateUint(t.elapsed);
                tesla->state.get("pair_disconnect_reason")->updateInt(t.disconnect);
                tesla->state.get("pair_verify_polls")->updateUint(t.polls);
                tesla->state.get("pair_verify_denied")->updateUint(t.denied);
                tesla->state.get("pair_rx_notifications")->updateUint(t.notifications);
                tesla->state.get("pair_rx_frames")->updateUint(t.frames);
                tesla->state.get("pair_rx_pair_replies")->updateUint(t.pair_replies);
                tesla->state.get("pair_rx_session_replies")->updateUint(t.session_replies);
            }
        }
        tesla->state.get("running")->updateBool(r && r->service_enabled && r->ble_ready.load() && !tesla->runtime_owner.is_stopping()
#if TESLA_BLE_MANUAL_VEHICLE_UI
            && r->profile_ready.load() && !r->profile_pending.load() && r->profile_actionable.load()
#endif
        );
        tesla->state.get("action_busy")->updateBool(
#if TESLA_BLE_MANUAL_VEHICLE_UI
            bluetooth.discovery_busy() ||
#endif
            service_start_requested || (r &&
            (
#if TESLA_BLE_MANUAL_VEHICLE_UI
             r->profile_pending.load() ||
#endif
             (!r->ble_ready.load() && !r->storage_only) || r->manual_poll_pending.load() || r->forget_requested.load() ||
             r->command_outcome.forward_state() != TeslaBleCommandOutcome::Forward::Available)));
    }
}

void TeslaVehicle::process_module_events()
{
    Runtime *r = tesla->runtime_owner.get();
    ModuleEvent event;
    for (size_t batch = 0; r != nullptr && batch < 8; ++batch) {
        if (r->deferred_soc) { event = *r->deferred_soc; r->deferred_soc.reset(); }
        else { r->deferred_attempts = 0; if (xQueueReceive(r->queues.events(), &event, 0) != pdTRUE) { break; } }
        if (!tesla->runtime_owner.accepts_event(event.runtime_epoch) || !accepts_commands()) {
            continue;
        }
        switch (event.type) {
        case EventType::PairApproved:
            // Authenticated status proves possession/approval, not SoC. Keep the
            // same final SLAC/runtime/request gate without invoking ev.set_soc.
            if (r->session.consume(event.request,
                    current_ev_identity(),
                    r->configured_ev_index, event.authenticated, event.correlated, 50,
                    [](float) {}) && tesla_ble_publication_newer(event.order, r->pairing_order)) {
                r->pairing_order = event.order;
                // Correlated authenticated possession may precede main's first
                // coalesced key-presence notification. Do not lose that proof.
                if (!tesla->pairing_policy.key_present()) { tesla->pairing_policy.on_key_loaded(true); }
                tesla->pairing_policy.on_authenticated_response();
                tesla->state.get("key_present")->updateBool(tesla->pairing_policy.key_present());
                tesla->state.get("key_presence")->updateString("present");
                tesla->state.get("paired")->updateBool(tesla->pairing_policy.paired());
                tesla->state.get("pairing_pending")->updateBool(tesla->pairing_policy.pairing_pending());
            }
            break;
        case EventType::KeyPresence:
            tesla->pairing_policy.on_key_loaded(event.soc == 1.0f);
            tesla->state.get("key_present")->updateBool(tesla->pairing_policy.key_present());
            break;
        case EventType::Connected:
        case EventType::Disconnected:
            if (tesla_ble_publication_newer(event.order, r->connection_order)) {
                r->connection_order = event.order;
                tesla->state.get("connected")->updateBool(r->connected.load());
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
                    current_ev_identity(),
                    r->configured_ev_index, event.authenticated, event.correlated, event.soc,
                    [](float soc) { ev.set_soc(soc); }, &retry)) {
                ev.session_updated(EVDataSource::Tesla);
                if (tesla_ble_publication_newer(event.order, r->pairing_order)) {
                    r->pairing_order = event.order;
                    if (!tesla->pairing_policy.key_present()) { tesla->pairing_policy.on_key_loaded(true); }
                    tesla->pairing_policy.on_authenticated_response();
                    tesla->state.get("key_present")->updateBool(tesla->pairing_policy.key_present());
                    tesla->state.get("key_presence")->updateString("present");
                    tesla->state.get("paired")->updateBool(tesla->pairing_policy.paired());
                    tesla->state.get("pairing_pending")->updateBool(tesla->pairing_policy.pairing_pending());
                }
                tesla->state.get("last_soc")->updateFloat(event.soc);
                // Receive time captured by the worker at acceptance; never re-read here,
                // so queue delay or a later NTP sync/step cannot alter it (0 stays 0).
                tesla->state.get("last_update")->updateUptime(event.received_uptime);
                tesla->state.get("last_update_unix")->updateUint(event.received_unix);
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
                    tesla->runtime_owner.require_restart(); r->stop_requested.store(true);
                    tesla->state.get("restart_required")->updateBool(true);
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
            tesla->pairing_policy.on_pairing_error();
            publish_error(event.message);
            tesla->state.get("pairing_pending")->updateBool(tesla->pairing_policy.pairing_pending());
            break;
        case EventType::PairStarted:
            tesla->pairing_policy.on_pair_request_sent();
            tesla->state.get("key_present")->updateBool(tesla->pairing_policy.key_present());
            tesla->state.get("paired")->updateBool(tesla->pairing_policy.paired());
            tesla->state.get("pairing_pending")->updateBool(tesla->pairing_policy.pairing_pending());
            publish_error("");
            break;
        case EventType::KeyRemoved:
            tesla->pairing_policy.on_key_removed();
            tesla->state.get("key_present")->updateBool(tesla->pairing_policy.key_present());
            tesla->state.get("paired")->updateBool(tesla->pairing_policy.paired());
            tesla->state.get("connected")->updateBool(false);
            tesla->state.get("pairing_pending")->updateBool(tesla->pairing_policy.pairing_pending());
            break;
        }
    }
    if (r == nullptr || !tesla->runtime_owner.accepts_event(r->epoch) || !accepts_commands()) { return; }
    if (const auto outcome = r->command_outcome.take()) {
        using Kind = TeslaBleCommandOutcome::Kind;
        if (outcome->kind == Kind::PairApproved) {
            if (r->session.consume(outcome->request,
                    current_ev_identity(),
                    r->configured_ev_index, true, true, 50, [](float) {}) &&
                tesla_ble_publication_newer(outcome->order, r->pairing_order)) {
                r->pairing_order = outcome->order;
                if (!tesla->pairing_policy.key_present()) { tesla->pairing_policy.on_key_loaded(true); }
                tesla->pairing_policy.on_authenticated_response();
                tesla->state.get("key_present")->updateBool(tesla->pairing_policy.key_present());
                tesla->state.get("key_presence")->updateString("present");
                tesla->state.get("paired")->updateBool(tesla->pairing_policy.paired());
                tesla->state.get("pairing_pending")->updateBool(tesla->pairing_policy.pairing_pending());
            }
            r->pair_approval_waiting.store(false);
            publish_pairing_state();
            return;
        }
        if (outcome->kind == Kind::KeyRemoved && tesla_ble_publication_newer(outcome->order, r->connection_order)) {
            r->connection_order = outcome->order;
            tesla->state.get("connected")->updateBool(false);
        }
        if (tesla_ble_publication_newer(outcome->order, r->pairing_order)) {
            r->pairing_order = outcome->order;
            if (outcome->kind == Kind::KeyRemoved) {
                tesla->pairing_policy.on_key_removed();
            } else if (outcome->kind == Kind::PairStarted) {
                tesla->pairing_policy.on_pair_request_sent();
            } else {
                tesla->pairing_policy.on_key_loaded(outcome->key_present);
            }
            tesla->state.get("key_present")->updateBool(tesla->pairing_policy.key_present());
            tesla->state.get("paired")->updateBool(tesla->pairing_policy.paired());
            tesla->state.get("pairing_pending")->updateBool(tesla->pairing_policy.pairing_pending());
            tesla_ble_publish_presence(tesla->state.get("key_presence"), outcome->key_present, outcome->key_known, outcome->cleanup_required);
        }
        if (tesla_ble_publication_newer(outcome->order, r->error_order)) {
            r->error_order = outcome->order;
            if (outcome->kind == Kind::Error || outcome->kind == Kind::KeyRemoved) {
                tesla->state.get("pair_outcome")->updateString("none");
                tesla->state.get("pair_response_seen")->updateBool(false);
            }
            publish_error(outcome->error);
        }
    }
    publish_pairing_state();
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if (r->profile_ready.load() && !r->profile_pending.load()) {
        tesla->state.get("paired")->updateBool(r->authorized.load());
    }
#endif
}
