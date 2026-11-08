// Included once by the selected feature module, never a second host instance.
#include "bluetooth_core.h"
#include "../tesla_vehicle/bluetooth_memory.h"
#ifndef TESLA_BLE_MODULE_HOST_TEST
#include "modules/api/api.h"
#include "modules/ev/ev.h"
#include "tools.h"
#include <esp_random.h>
extern API api;
extern Ev ev;
#if WARP_TESLA_MEMORY_PHASES
#include "modules/web_server/web_server.h"
extern WebServer server;
#endif
#endif
#include <cmath>
#include <cstring>
static String checked_error_string(const char *message) {
    String text(message);
    if (!text) { return String("OOM"); }
    return text;
}
#if WARP_FEATURE_REMOTE
struct RemoteEvIdentity {
    uint64_t generation;
    int index;
    uint64_t mac;
    bool operator==(const RemoteEvIdentity &o) const { return generation==o.generation && index==o.index && mac==o.mac; }
    bool operator!=(const RemoteEvIdentity &o) const { return !(*this==o); }
};
static RemoteEvIdentity remote_current_ev_identity() {
    const auto live=ev.get_live_identity();return {live.generation,live.index,live.mac};
}
#include "../tesla_vehicle/remote_settings.inc"
#endif
void BluetoothCore::pre_setup() {
    bluetooth_memory::sample(bluetooth_memory::Stage::Boot);
    bluetooth_config=Config::Object({{"enable",Config::Bool(false)}});
    bluetooth_update=Config::Object({{"enable",Config::Bool(false)}});
    bluetooth_state=Config::Object({
        {"supported",Config::Bool(bluetooth_supported())},
        {"unsupported_reason",Config::Str(bluetooth_unsupported_reason(),0,32)},
        {"phase",Config::Str("disabled",0,8)},
        {"restart_required",Config::Bool(false)},
        {"restart_reason",Config::Str("",0,32)},
        {"persistence_error",Config::Bool(false)},
        {"legacy_tesla_enabled",Config::Bool(false)},
    });
    empty_command=Config::Object({});
#if WARP_FEATURE_REMOTE
    remote_pre_setup();
#endif
#if TESLA_BLE_R13_MASTER_BENCH
    bench_state=Config::Object({{"running",Config::Bool(false)},{"key_presence",Config::Str("unknown",0,16)}});
#endif
}
void BluetoothCore::setup() {
#if WARP_FEATURE_REMOTE
    remote_load();
#endif
    bool desired=false;
    bluetooth_commit_ok=load_bluetooth(desired);
    bluetooth_config.get("enable")->updateBool(desired);
    bluetooth_boot_enabled=bluetooth_supported() && bluetooth_commit_ok && desired;
    if(bluetooth_boot_enabled) bluetooth.start();
    publish_bluetooth_state();
    initialized=true;
}
void BluetoothCore::register_urls() { register_bluetooth_urls(); }
void BluetoothCore::register_bluetooth_urls() {
    api.addState("bluetooth/config",&bluetooth_config);
    api.addState("bluetooth/state",&bluetooth_state);
#if WARP_FEATURE_REMOTE
    remote_register_urls();
#endif
#if WARP_TESLA_MEMORY_PHASES && !defined(TESLA_BLE_MODULE_HOST_TEST)
    server.on_HTTPThread("/bluetooth/memory",HTTP_GET,[](WebServerRequest request) {
        request.beginChunkedResponse_json(200);
        request.sendChunk("{\"version\":1,\"stack_unit\":\"bytes\",\"samples\":[");
        char buffer[512];
        for(unsigned i=0;i<static_cast<unsigned>(bluetooth_memory::Stage::Count);++i) {
            if(!bluetooth_memory::json(static_cast<bluetooth_memory::Stage>(i),buffer,sizeof(buffer))) return request.endChunkedResponse();
            if(i) request.sendChunk(",");
            request.sendChunk(buffer);
        }
        request.sendChunk("]}");
        return request.endChunkedResponse();
    });
#endif
    api.addCommand("bluetooth/config_update",&bluetooth_update,{},[this](Language,String &errmsg) {
        save_bluetooth(bluetooth_update.get("enable")->asBool(),errmsg);
    },false);
    api.addCommand("bluetooth/restart",&empty_command,{},[this](Language,String &errmsg) { restart_bluetooth(errmsg); },true);
}
bool BluetoothCore::load_bluetooth(bool &desired) {
    BluetoothConfigStore store(BLUETOOTH_CONFIG_PATH,bluetooth_storage_quarantined);
    const bool ok=store.load(desired);
    bluetooth_storage_quarantined=store.io_quarantined();
    return ok;
}
bool BluetoothCore::persist_bluetooth(bool desired) {
    BluetoothConfigStore store(BLUETOOTH_CONFIG_PATH,bluetooth_storage_quarantined);
    const bool ok=store.save(desired);
    bluetooth_storage_quarantined=store.io_quarantined();
    return ok;
}
bool BluetoothCore::save_bluetooth(bool desired,String &errmsg) {
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if(bluetooth.discovery_busy()) { errmsg=checked_error_string("Search in progress");return false; }
#endif
    if(bluetooth_restart_pending) { errmsg=checked_error_string("Restart pending");return false; }
#if TESLA_BLE_R13_MASTER_BENCH
    if(bench_restart_pending) { errmsg=checked_error_string("Restart pending");return false; }
#endif
    if(desired && !bluetooth_supported()) {
        errmsg=checked_error_string("Bluetooth unavailable: SDK or hardware does not support this service");return false;
    }
    bluetooth_commit_ok=persist_bluetooth(desired);
    if(!bluetooth_commit_ok) errmsg=checked_error_string("Bluetooth configuration commit failed; not saved; no restart permitted");
    else bluetooth_config.get("enable")->updateBool(desired);
    publish_bluetooth_state();
    return bluetooth_commit_ok;
}
bool BluetoothCore::restart_bluetooth(String &errmsg) {
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if(bluetooth.discovery_busy()) { errmsg=checked_error_string("Search in progress");return false; }
#endif
    bool saved=false;
    publish_bluetooth_state();
    if(!bluetooth_supported() || !bluetooth_commit_ok || !bluetooth_state.get("restart_required")->asBool() ||
       !load_bluetooth(saved) || saved!=bluetooth_config.get("enable")->asBool()) {
        bluetooth_commit_ok=false;publish_bluetooth_state();
        errmsg=checked_error_string("No verified Bluetooth commit available for restart");return false;
    }
#if TESLA_BLE_R13_MASTER_BENCH
#if TESLA_BLE_MANUAL_WEB_BENCH
    if(service_start_requested) { errmsg=checked_error_string("Bluetooth boot startup pending");return false; }
#endif
    if(master_bench.failed()) { errmsg=checked_error_string("Bench lifecycle failed");return false; }
    if(master_bench.active()) { bench_restart_pending=true;return true; }
    if(strcmp(master_bench.phase(),"disabled")!=0) { errmsg=checked_error_string("Bench lifecycle not quiescent");return false; }
#endif
    if(bluetooth_boot_enabled && !bluetooth.stopped()) {
        if(bluetooth.failed()) { errmsg=checked_error_string("Bluetooth lifecycle failed; ownership retained");return false; }
        bluetooth_restart_pending=true;service_start_requested=false;return true;
    }
    trigger_reboot("Bluetooth configuration");return true;
}
void BluetoothCore::publish_bluetooth_state() {
    const bool latched=bluetooth.failed() || (bluetooth_boot_enabled && bluetooth.stopped());
    const bool restart=bluetooth_supported() && (latched || bluetooth_config.get("enable")->asBool()!=bluetooth_boot_enabled);
    bluetooth_state.get("phase")->updateString(!bluetooth_supported()?"disabled":latched?"error":bluetooth.phase());
    bluetooth_state.get("restart_required")->updateBool(restart);
    bluetooth_state.get("restart_reason")->updateString(!restart?"":latched?"lifecycle_latched":"configuration_changed");
    bluetooth_state.get("persistence_error")->updateBool(!bluetooth_commit_ok);
    bluetooth_state.get("legacy_tesla_enabled")->updateBool(false);
}
void BluetoothCore::loop() {
    bluetooth.supervise();
#if WARP_FEATURE_REMOTE
    if(bluetooth_restart_pending) remote_setup.cancel();
    remote_loop();
    if(bluetooth_restart_pending && remote_setup.busy()) return;
#endif
    publish_bluetooth_state();
    if(bluetooth_restart_pending) {
        bluetooth.request_stop();
        if(bluetooth.stopped()) {
            bluetooth_restart_pending=false;
            trigger_reboot("Bluetooth verified teardown");
        }
    }
}
