#include "tesla_ble_bench_smoke.h"
#include "tesla_ble_bench_admission.h"

#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS && (!defined(TESLA_BLE_R13_MASTER_BENCH) || !TESLA_BLE_R13_MASTER_BENCH)
#include <esp_netif.h>
#include <cstring>
#include "config.h"
#include "main_dependencies.h"

static ConfigRoot smoke_state;

void tesla_ble_bench_smoke_pre_setup()
{
    smoke_state = Config::Object({
        {"status", Config::Str("idle", 0, 20)},
        {"stage", Config::Str("idle", 0, 32)},
        {"failure_stage", Config::Str("none", 0, 32)},
        {"failure_code", Config::Int(0, INT32_MIN, INT32_MAX)},
        {"task_create_code", Config::Int(0, INT32_MIN, INT32_MAX)},
        {"teardown_stage", Config::Str("none", 0, 32)},
        {"teardown_code", Config::Int(0, INT32_MIN, INT32_MAX)},
        {"initialization_ok", Config::Bool(false)},
        {"teardown_complete", Config::Bool(false)},
        {"worker_active", Config::Bool(false)},
        {"restart_required", Config::Bool(false)},
    });
}

void tesla_ble_bench_smoke_loop()
{
    tesla_ble_bench_smoke.supervise();
    const auto s = tesla_ble_bench_smoke.snapshot();
    // Config is accessed only in the main task, never the Bluetooth callback
    // or smoke task. This is a separate BENCH schema, not Tesla/SoC state.
    smoke_state.get("status")->updateString(s.status);
    smoke_state.get("stage")->updateString(s.stage);
    smoke_state.get("failure_stage")->updateString(s.failure_stage);
    smoke_state.get("failure_code")->updateInt(s.failure_code);
    smoke_state.get("task_create_code")->updateInt(s.task_create_code);
    smoke_state.get("teardown_stage")->updateString(s.teardown_stage);
    smoke_state.get("teardown_code")->updateInt(s.teardown_code);
    smoke_state.get("initialization_ok")->updateBool(s.initialization_ok);
    smoke_state.get("teardown_complete")->updateBool(s.teardown_complete);
    smoke_state.get("worker_active")->updateBool(s.worker_active);
    smoke_state.get("restart_required")->updateBool(s.restart_required);
}

static bool local_ethernet_request(WebServerRequest &request)
{
    const auto peer = request.getPeerAddress();
    const auto local = request.getLocalAddress();
    if (peer.type() != IPv4 || local.type() != IPv4) { return false; }
    auto *netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    esp_netif_ip_info_t info{};
    if (!netif || !esp_netif_is_netif_up(netif) || esp_netif_get_ip_info(netif, &info) != ESP_OK) { return false; }
    const uint32_t address = static_cast<uint32_t>(peer);
    const uint32_t mask = info.netmask.addr;
    const uint32_t network = info.ip.addr & mask;
    return mask != 0 && info.ip.addr != 0 && static_cast<uint32_t>(local) == info.ip.addr &&
           address != info.ip.addr && address != network && address != (network | ~mask) &&
           (address & mask) == network;
}

void tesla_ble_bench_smoke_register_urls()
{
    api.addState("tesla_vehicle/bench_stack_smoke_state", &smoke_state);
    // Explicit HTTP PUT only, not an is_action API (which also accepts GET and
    // other backends). Existing web-server authentication still applies. Even
    // without configured authentication, require the directly attached IPv4
    // Ethernet subnet and the ETH local socket address; no forwarded headers.
    server.on("/tesla_vehicle/bench_stack_smoke", HTTP_PUT, [](WebServerRequest request) {
        if (!local_ethernet_request(request)) { return request.send_plain(403, "Local Ethernet bench request required"); }
        const size_t length = request.contentLength();
        if (length != 0) {
            char payload[2];
            if (length != sizeof(payload) || request.receive(payload, sizeof(payload)) != 2 ||
                payload[0] != '{' || payload[1] != '}') {
                return request.send_plain(400, "Expected empty body or {}");
            }
        }
        if (strcmp(tesla_ble_bench_smoke.snapshot().status, "idle") != 0 || tesla_bench::owner.load() != 0) {
            return request.send_plain(409, "One smoke attempt per boot; restart required");
        }
        const bool accepted = tesla_ble_bench_smoke.request();
        tesla_ble_bench_smoke_loop();
        return accepted ? request.send_plain(202, "Bench stack smoke accepted; poll bench_stack_smoke_state") :
                          request.send_plain(503, "Could not create bench smoke task; restart required");
    });
}
#endif
