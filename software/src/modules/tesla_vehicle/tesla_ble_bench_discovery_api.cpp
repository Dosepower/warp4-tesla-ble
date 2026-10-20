#include "tesla_ble_bench_discovery.h"
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS && (!defined(TESLA_BLE_R13_MASTER_BENCH) || !TESLA_BLE_R13_MASTER_BENCH)
#include <esp_netif.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include "main_dependencies.h"

static bool local_ethernet_request(WebServerRequest &request)
{
    const auto peer = request.getPeerAddress();
    const auto local = request.getLocalAddress();
    if (peer.type() != IPv4 || local.type() != IPv4) { return false; }
    auto *netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    esp_netif_ip_info_t info{};
    if (!netif || !esp_netif_is_netif_up(netif) || esp_netif_get_ip_info(netif, &info) != ESP_OK) { return false; }
    const uint32_t address = static_cast<uint32_t>(peer), mask = info.netmask.addr, network = info.ip.addr & mask;
    return mask != 0 && info.ip.addr != 0 && static_cast<uint32_t>(local) == info.ip.addr &&
           address != info.ip.addr && address != network && address != (network | ~mask) && (address & mask) == network;
}

void tesla_ble_bench_discovery_register_urls()
{
    server.on("/tesla_vehicle/bench_pair_rehearsal", HTTP_PUT, [](WebServerRequest request) {
        if (!request.is_strictly_authenticated()) { return request.send_plain(403,"Effective authentication with valid credentials required"); }
        if (!local_ethernet_request(request)) { return request.send_plain(403,"Local Ethernet bench request required"); }
        char payload[2];
        if (request.contentLength()!=2 || request.receive(payload,2)!=2 || memcmp(payload,"{}",2)) { return request.send_plain(400,"Expected {}"); }
        const int rc=tesla_ble_bench_discovery.request_pair_rehearsal();
        return request.send_plain(rc,rc==202?"Non-enrolling host/resource rehearsal accepted; no scan, GATT or Tesla payload; restart afterwards":"Rehearsal requires prepared unexposed local key; see state");
    });
    server.on("/tesla_vehicle/bench_pair_prepare", HTTP_PUT, [](WebServerRequest request) {
        if (!request.is_strictly_authenticated()) { return request.send_plain(403,"Effective authentication with valid credentials required"); }
        if (!local_ethernet_request(request)) { return request.send_plain(403,"Local Ethernet bench request required"); }
        char payload[38],vin[18]{};
        if (request.contentLength()!=sizeof(payload) || request.receive(payload,sizeof(payload))!=static_cast<int>(sizeof(payload)) ||
            memcmp(payload,"{\"target\":",10) || payload[10]<'0' || payload[10]>'7' || memcmp(payload+11,",\"vin\":\"",8) || memcmp(payload+36,"\"}",2)) {
            return request.send_plain(400,"Expected exactly {\"target\":N,\"vin\":\"<17 uppercase VIN bytes>\"}");
        }
        memcpy(vin,payload+19,17);
        const int rc=tesla_ble_bench_discovery.request_pair_prepare(payload[10]-'0',vin);
        return request.send_plain(rc,rc==202?"Key preparation accepted; no BLE request sent":"Preparation refused; require exact fresh VIN-bound connectable selection");
    });
    server.on("/tesla_vehicle/bench_pair_start", HTTP_PUT, [](WebServerRequest request) {
        if (!request.is_strictly_authenticated()) { return request.send_plain(403,"Effective authentication with valid credentials required"); }
        if (!local_ethernet_request(request)) { return request.send_plain(403,"Local Ethernet bench request required"); }
        constexpr char expected[]="{\"confirm_card_and_display\":true}"; char payload[sizeof(expected)-1];
        if (request.contentLength()!=sizeof(payload) || request.receive(payload,sizeof(payload))!=static_cast<int>(sizeof(payload)) || memcmp(payload,expected,sizeof(payload))) { return request.send_plain(400,"Explicit card/display confirmation acknowledgement required"); }
        const int rc=tesla_ble_bench_discovery.request_pair_start();
        return request.send_plain(rc,rc==202?"One pairing attempt accepted; physical card tap AND vehicle display approval required; sending is not success":"Pairing unavailable; see state; no automatic retries");
    });
    server.on("/tesla_vehicle/bench_pair_verify", HTTP_PUT, [](WebServerRequest request) {
        if (!request.is_strictly_authenticated()) { return request.send_plain(403,"Effective authentication with valid credentials required"); }
        if (!local_ethernet_request(request)) { return request.send_plain(403,"Local Ethernet bench request required"); }
        char payload[2];
        if (request.contentLength()!=2 || request.receive(payload,2)!=2 || memcmp(payload,"{}",2)) { return request.send_plain(400,"Expected {}"); }
        const int rc=tesla_ble_bench_discovery.request_pair_verify();
        return request.send_plain(rc,rc==202?"One fresh VCSEC SessionInfo HMAC verification accepted; no vehicle command":"Verification unavailable; see state");
    });
    server.on("/tesla_vehicle/bench_pair_revoke_confirm", HTTP_PUT, [](WebServerRequest request) {
        if (!request.is_strictly_authenticated()) { return request.send_plain(403,"Effective authentication with valid credentials required"); }
        if (!local_ethernet_request(request)) { return request.send_plain(403,"Local Ethernet bench request required"); }
        constexpr char expected[]="{\"tesla_removed_by_owner\":true}"; char payload[sizeof(expected)-1];
        if (request.contentLength()!=sizeof(payload) || request.receive(payload,sizeof(payload))!=static_cast<int>(sizeof(payload)) || memcmp(payload,expected,sizeof(payload))) { return request.send_plain(400,"Owner must remove this public key in Tesla first; confirmation is user-mediated"); }
        const int rc=tesla_ble_bench_discovery.confirm_pair_revoke();
        return request.send_plain(rc,"Owner removal assertion recorded only if 200; NOT cryptographic revocation proof; local key not deleted");
    });
    server.on("/tesla_vehicle/bench_pair_delete", HTTP_PUT, [](WebServerRequest request) {
        if (!request.is_strictly_authenticated()) { return request.send_plain(403,"Effective authentication with valid credentials required"); }
        if (!local_ethernet_request(request)) { return request.send_plain(403,"Local Ethernet bench request required"); }
        constexpr char expected[]="{\"ack_unresolved\":true}"; char payload[sizeof(expected)-1];
        if (request.contentLength()!=sizeof(payload) || request.receive(payload,sizeof(payload))!=static_cast<int>(sizeof(payload)) || memcmp(payload,expected,sizeof(payload))) { return request.send_plain(400,"Acknowledge local deletion does not revoke Tesla authorization"); }
        const int rc=tesla_ble_bench_discovery.request_pair_delete();
        return request.send_plain(rc,rc==202?"Local bench-key deletion accepted only; poll state; Tesla-side revoke remains separate":"Deletion unavailable; see state");
    });
    // HTTP only. Existing server authentication still applies. No generic API
    // action, GET action, MQTT, automation, boot or production runtime path.
    server.on("/tesla_vehicle/bench_scan", HTTP_PUT, [](WebServerRequest request) {
        if (!request.is_strictly_authenticated()) { return request.send_plain(403,"Effective authentication with valid credentials required"); }
        if (!local_ethernet_request(request)) { return request.send_plain(403, "Local Ethernet bench request required"); }
        const size_t length = request.contentLength();
        if (length) {
            char payload[2];
            if (length != sizeof(payload) || request.receive(payload, sizeof(payload)) != 2 || memcmp(payload, "{}", 2)) {
                return request.send_plain(400, "Expected empty body or {}");
            }
        }
        const int rc = tesla_ble_bench_discovery.request_scan();
        return request.send_plain(rc, rc == 202 ? "Active scan accepted (standard BLE scan requests only); poll bench_discovery_state" : "Scan unavailable; one attempt per boot; see state");
    });
    server.on("/tesla_vehicle/bench_probe", HTTP_PUT, [](WebServerRequest request) {
        if (!request.is_strictly_authenticated()) { return request.send_plain(403,"Effective authentication with valid credentials required"); }
        if (!local_ethernet_request(request)) { return request.send_plain(403, "Local Ethernet bench request required"); }
        // Intentionally strict bounded grammar; no VIN, MAC fallback, pairing,
        // command, arbitrary address or ambiguous identity fields are accepted.
        constexpr char prefix[] = "{\"target\":";
        char payload[sizeof(prefix) + 1];
        if (request.contentLength() != sizeof(payload) || request.receive(payload, sizeof(payload)) != static_cast<int>(sizeof(payload)) ||
            memcmp(payload, prefix, sizeof(prefix) - 1) || payload[sizeof(prefix)] != '}' ||
            payload[sizeof(prefix) - 1] < '0' || payload[sizeof(prefix) - 1] > '7') {
            return request.send_plain(400, "Expected exactly {\"target\":N}, N=0..7 from this boot's scan");
        }
        const int rc = tesla_ble_bench_discovery.request_probe(payload[sizeof(prefix) - 1] - '0');
        return request.send_plain(rc, rc == 202 ? "Metadata-only probe accepted; poll bench_discovery_state" : "Probe unavailable; need successful fresh scan and explicit connectable target; see state");
    });
    server.on("/tesla_vehicle/bench_discovery_state", HTTP_GET, [](WebServerRequest request) {
        if (!request.is_strictly_authenticated()) { return request.send_plain(403,"Effective authentication with valid credentials required"); }
        if (!local_ethernet_request(request)) { return request.send_plain(403, "Local Ethernet bench request required"); }
        tesla_ble_bench_discovery.supervise();
        const auto s = tesla_ble_bench_discovery.snapshot();
        // Ordinary allocation follows the proven PSRAM preference. No 4K BSS or
        // HTTP-task stack reservation in scarce internal byte memory.
        constexpr size_t capacity=12288; // HTTP/PSRAM only; bounded per-stage samples.
        std::unique_ptr<char[]> buffer(new (std::nothrow) char[capacity]);
        if (!buffer) { return request.send_plain(503, "Could not allocate state response"); }
        const int n = snprintf(buffer.get(), capacity,
            "{\"action\":\"%s\",\"status\":\"%s\",\"stage\":\"%s\",\"failure_stage\":\"%s\",\"failure_code\":%d,"
            "\"teardown_stage\":\"%s\",\"teardown_code\":%d,\"task_create_code\":%d,\"worker_active\":%s,\"teardown_complete\":%s,"
            "\"restart_required\":%s,\"transport_ready\":%s,\"scan_attempted\":%s,\"probe_attempted\":%s,\"selected_target\":%d,"
            "\"target_count\":%u,\"dropped_target_reports\":%u,\"target_ttl_ms\":120000,\"scan_seconds\":30,\"scan_type\":\"active\","
            "\"write_handle\":%u,\"notify_handle\":%u,\"cccd_handle\":%u,\"worker_stack_min_bytes\":%u,\"measurements_valid\":%s,"
            "\"exec_free\":%u,\"exec_largest\":%u,\"exec_min\":%u,\"internal8_free\":%u,\"internal8_largest\":%u,\"internal8_min\":%u,"
            "\"psram_free\":%u,\"psram_largest\":%u,\"psram_min\":%u,\"pairing_available\":true,\"key_effect\":\"%s\","
            "\"pair_state\":\"%s\",\"vin_bound\":%s,\"local_key_presence\":\"%s\",\"request_may_have_reached_tesla\":%s,\"pair_wait_expired\":%s,\"pair_information\":%u,"
            "\"tesla_side_revoke\":\"%s\",\"public_key_hex\":\"%s\",\"authenticated_domain\":\"%s\",\"infotainment_sync_verified\":false,"
            "\"tesla_authorization\":\"%s\",\"security_request_seen\":%s,\"security_reply_code\":%d,\"ble_bond_count\":%d,\"targets\":[",
            s.action, s.status, s.stage, s.failure_stage, s.failure_code, s.teardown_stage, s.teardown_code, s.task_create_code,
            s.worker_active ? "true" : "false", s.teardown_complete ? "true" : "false", s.restart_required ? "true" : "false",
            s.transport_ready ? "true" : "false", s.scan_attempted ? "true" : "false", s.probe_attempted ? "true" : "false", s.selected_target,
            s.target_count, s.dropped_targets, s.write_handle, s.notify_handle, s.cccd_handle, s.stack_min_bytes,
            s.measurements_valid ? "true" : "false",
            s.exec_free, s.exec_largest, s.exec_min, s.internal8_free, s.internal8_largest, s.internal8_min, s.psram_free, s.psram_largest, s.psram_min,
            !s.vin_bound?"untouched":(!strcmp(s.action,"pair_delete")?(s.worker_active?"local_delete_pending":(s.failure_code?"local_delete_uncertain":"local_deleted")):"bench_namespace_only"),
            s.pair_state,s.vin_bound?"true":"false",
            !s.vin_bound?"not_inspected":(s.worker_active?"pending":(s.failure_code?"unknown":(s.local_key_present?"present":(!strcmp(s.action,"pair_delete")?"absent":"unknown")))),
            s.request_may_have_reached_tesla?"true":"false",s.pair_wait_expired?"true":"false",static_cast<unsigned>(s.pair_information),s.tesla_removal_user_claim?"owner_claim_unverified":"unresolved",s.public_key_hex,
            !strcmp(s.pair_state,"authenticated")?"vcsec":"none",!strcmp(s.pair_state,"authenticated")?"vcsec_session_hmac_verified":"not_checked",
            s.security_request_seen ? "true" : "false", s.security_reply_code, s.ble_bond_count);
        if (n < 0 || static_cast<size_t>(n) >= capacity) { return request.send_plain(503, "State response overflow"); }
        size_t used = n;
        for (unsigned i = 0; i < s.target_count; ++i) {
            const auto &t = s.targets[i];
            const int added = snprintf(buffer.get() + used, capacity - used,
                "%s{\"target\":%u,\"address\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"address_type\":%u,\"rssi\":%d,\"local_name\":\"%s\",\"connectable\":%s,\"evidence\":\"tesla_local_name_shape\"}",
                i ? "," : "", i, t.address[0], t.address[1], t.address[2], t.address[3], t.address[4], t.address[5], t.address_type, t.rssi,
                t.local_name, t.connectable ? "true" : "false");
            if (added < 0 || static_cast<size_t>(added) >= capacity - used) { return request.send_plain(503, "State response overflow"); }
            used += added;
        }
        const auto &f=s.resource_failure;
        const int added=snprintf(buffer.get()+used,capacity-used,
            "],\"resource_gate_failed\":%s,\"resource_gate_mask\":%u,\"worker_stack_bytes\":%u,"
            "\"resource_thresholds\":{\"internal8_free\":%u,\"internal8_largest\":%u,\"internal8_min\":%u,\"worker_watermark\":%u},"
            "\"resource_failure\":{\"stage\":\"%s\",\"phase\":\"%s\",\"internal8_free\":%u,\"internal8_largest\":%u,\"internal8_min\":%u,\"worker_stack_min_bytes\":%u},"
            "\"resource_samples_dropped\":%u,\"rehearsal_attempted\":%s,\"resource_samples\":[",
            s.resource_gate_failed?"true":"false",s.resource_gate_mask,s.worker_stack_bytes,
            TeslaBleBenchDiscovery::MIN_INTERNAL8_FREE,TeslaBleBenchDiscovery::MIN_INTERNAL8_LARGEST,TeslaBleBenchDiscovery::MIN_INTERNAL8_MIN,TeslaBleBenchDiscovery::MIN_WORKER_WATERMARK,
            f.stage,f.phase,f.internal8_free,f.internal8_largest,f.internal8_min,f.stack_min_bytes,
            s.resource_samples_dropped,s.pair_rehearsal_attempted?"true":"false");
        if (added<0 || static_cast<size_t>(added)>=capacity-used) { return request.send_plain(503,"State response overflow"); }
        used+=added;
        TeslaBleBenchDiscovery::ResourceSample sample;
        for (unsigned index=0;index<32 && tesla_ble_bench_discovery.resource_sample(index,sample);++index) {
            const int count=snprintf(buffer.get()+used,capacity-used,"%s{\"stage\":\"%s\",\"phase\":\"%s\",\"code\":%d,\"internal8_free\":%u,\"internal8_largest\":%u,\"internal8_min\":%u,\"worker_stack_min_bytes\":%u}",index?",":"",sample.stage,sample.phase,sample.code,sample.internal8_free,sample.internal8_largest,sample.internal8_min,sample.stack_min_bytes);
            if (count<0 || static_cast<size_t>(count)>=capacity-used) { return request.send_plain(503,"State response overflow"); }
            used+=count;
        }
        if (used + 3 > capacity) { return request.send_plain(503, "State response overflow"); }
        memcpy(buffer.get() + used, "]}", 3);
        return request.send_json(200, buffer.get());
    });
}
#endif
