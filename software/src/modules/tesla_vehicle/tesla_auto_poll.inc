// Autonomous firmware SoC scheduler (TESLA_BLE_MANUAL_VEHICLE_UI builds only).
// Included by tesla_vehicle.cpp after tesla_profiles.inc. Main task only.
//
// Primary operating mode requested by the user: periodic SoC updates without
// an open browser or any external agent, using the per-profile persisted
// poll_interval. One cycle reuses the existing production predicates unchanged:
//   1. read-only key/authorization/transport-binding probe (same as guidance),
//   2. an own VIN-scoped discovery for the CURRENT physical PEV generation,
//   3. fresh_binding(): exactly one connectable advertiser with the VIN name at
//      the profile's already persisted and authorized BLE address (an address
//      change still requires the user's explicit scan/adoption),
//   4. select_profile() with that scan revision (runtime-only, never persisted),
//   5. one bounded SoC operation through the normal worker request path.
// Never pairs, never writes key/binding material, never charges, never
// queues behind or concurrently with manual/guided/discovery/pairing work.
// The interval is the minimum gap after ANY finished SoC operation (manual or
// automatic) and after any unsuccessful cycle. Unplug, identity or profile
// changes abandon the cycle; the worker's own identity gate retires any
// in-flight operation.

static constexpr uint32_t AUTO_POLL_FIRST_DELAY_MS = 15000; // after plug-in/identity change
static constexpr uint32_t AUTO_POLL_RETRY_MS = 5000;        // non-radio deferral only
static constexpr uint32_t AUTO_POLL_SCAN_TIMEOUT_MS = (TeslaBleDiscovery::SCAN_SECONDS + 10) * 1000;
static constexpr uint32_t AUTO_POLL_SELECT_TIMEOUT_MS = 15000;

static bool auto_poll_same_identity(const TeslaBleIdentity &a, const TeslaBleIdentity &b)
{
    return a.generation == b.generation && a.index == b.index && a.mac == b.mac;
}

void TeslaVehicle::auto_poll_publish(const char *state)
{
    tesla->state.get("auto_poll")->updateString(state);
}

void TeslaVehicle::auto_poll()
{
    auto &a = tesla->auto_poll;
    const uint32_t now = millis();
    auto *r = tesla->runtime_owner.get();
    // Publish the worker's terminal SoC outcome first, so a just-finished
    // operation always pushes the next automatic cycle out by one interval.
    if (r) {
        const auto sequence = r->poll_outcome_sequence.load();
        if (tesla->poll_outcome_epoch != r->epoch || tesla->poll_outcome_seen != sequence) {
            // Per runtime epoch; a new worker's sequence restarts at 0 (nothing to publish yet).
            tesla->poll_outcome_epoch = r->epoch; tesla->poll_outcome_seen = sequence;
            static const char *const names[] = {"none", "ok", "asleep_wake_disabled", "asleep_wake_limited", "woken_no_soc", "failed", "busy"};
            const auto value = r->poll_outcome.load();
            if (sequence != 0 && value < sizeof(names) / sizeof(names[0])) {
                tesla->state.get("poll_outcome")->updateString(names[value]);
                tesla->state.get("poll_outcome_automatic")->updateBool(r->poll_outcome_automatic.load());
                a.last_operation_ms = now; a.operation_seen = true;
            }
        }
    }
    const auto live = ev.get_live_identity();
    const TeslaBleIdentity id{live.generation, live.index, live.mac};
    Config *p = live.index >= 0 && live.mac ? find_profile(live.index) : nullptr;
    const bool configured = p && !tesla->legacy_blocked && p->get("enable")->asBool() &&
        p->get("vin")->asString().length() == 17 && p->get("ble_address")->asString().length() &&
        profile_mac_value(p->get("ev_mac")->asString().c_str()) == live.mac && ev.get_profile_mac(live.index) == live.mac &&
        ev.get_active_ev_index() == live.index && ev.get_active_ev_mac() == live.mac;
    if (!configured || !auto_poll_same_identity(a.identity, id)) {
        a = {};
        if (!configured) { auto_poll_publish("off"); return; }
        a.identity = id; a.next_ms = now + AUTO_POLL_FIRST_DELAY_MS;
        auto_poll_publish("waiting");
        return;
    }
    const uint32_t interval_ms = p->get("poll_interval")->asUint() * 1000U;
    auto end_cycle = [&](const char *state) {
        a.phase = Tesla::AutoPoll::Phase::Idle;
        a.next_ms = now + interval_ms;
        a.interval_base_ms = now; a.interval_applied_ms = interval_ms; a.interval_scheduled = true;
        auto_poll_publish(state);
    };
    auto retry_later = [&] { a.next_ms = now + AUTO_POLL_RETRY_MS; a.interval_scheduled = false; };
    char vin[18]{}, address[18]{};
    strlcpy(vin, p->get("vin")->asString().c_str(), sizeof(vin));
    strlcpy(address, p->get("ble_address")->asString().c_str(), sizeof(address));
    const uint8_t type = p->get("ble_address_type")->asUint();
    switch (a.phase) {
    case Tesla::AutoPoll::Phase::Idle: {
        if (a.interval_scheduled && a.interval_applied_ms != interval_ms) {
            // The user changed the interval: apply it to the pending wait now
            // (shorter or longer), measured from the end of the last cycle.
            // Wrap-safe: elapsed time is an unsigned difference.
            const uint32_t elapsed = now - a.interval_base_ms;
            a.next_ms = elapsed >= interval_ms ? now : now + (interval_ms - elapsed);
            a.interval_applied_ms = interval_ms;
        }
        if (static_cast<int32_t>(now - a.next_ms) < 0) { return; }
        if (a.operation_seen && now - a.last_operation_ms < interval_ms) { return; }
        // Never interleave with an explicit user flow or any busy worker state.
        TeslaBleDiscovery::Snapshot s{};
        const bool snapshot = bluetooth.discovery_snapshot(s);
        const bool foreign_result = snapshot && s.status == TeslaBleDiscovery::Status::Complete &&
            s.revision != a.own_revision && now - s.completed_at_ms < TeslaBleDiscovery::RESULT_TTL_MS;
        if (!snapshot || foreign_result || tesla->guided.request[0] || service_start_requested || !discovery_allowed()) {
            retry_later(); return;
        }
        // Read-only probe under the same idle gate as publish_guidance().
        TeslaBleStorageAdapter store;
        if (!store.select_profile(live.index, live.mac) ||
            store.private_key_presence() != TeslaBleStorageAdapter::KeyPresence::Present ||
            !profile_material_authorized(store, vin, live.mac) || !profile_transport_matches(store, vin, address, type)) {
            end_cycle("not_paired"); return;
        }
        TeslaBleDiscovery::Scope scope;
        scope.index = live.index; scope.mac = live.mac; scope.generation = live.generation;
        memcpy(scope.searched_vin, vin, 17);
        if (!tesla_ble_vehicle_name(vin, scope.name) || !bluetooth.request_discovery(scope)) {
            retry_later(); return;
        }
        a.scan_baseline = s.revision; a.config_revision = tesla->config_revision;
        a.phase = Tesla::AutoPoll::Phase::Scanning; a.phase_ms = now;
        auto_poll_publish("searching"); publish_discovery(); publish_bluetooth_state();
        return;
    }
    case Tesla::AutoPoll::Phase::Scanning: {
        TeslaBleDiscovery::Snapshot s{};
        if (!bluetooth.discovery_snapshot(s) || s.busy || s.revision == a.scan_baseline) {
            if (now - a.phase_ms > AUTO_POLL_SCAN_TIMEOUT_MS) { end_cycle("failed"); }
            return;
        }
        a.own_revision = s.revision;
        if (s.status != TeslaBleDiscovery::Status::Complete || tesla->config_revision != a.config_revision ||
            !fresh_binding(live.index, live.mac, vin, address, type, s.revision)) {
            end_cycle("not_found"); return; // Out of range, not connectable, ambiguous or address changed.
        }
        String error;
        if (!select_profile(live.index, false, error, s.revision)) { end_cycle("failed"); return; }
        r = tesla->runtime_owner.get();
        a.runtime_epoch = r ? r->epoch : 0;
        a.phase = Tesla::AutoPoll::Phase::Selecting; a.phase_ms = now;
        return;
    }
    case Tesla::AutoPoll::Phase::Selecting: {
        if (!r || r->epoch != a.runtime_epoch || tesla->config_revision != a.config_revision ||
            now - a.phase_ms > AUTO_POLL_SELECT_TIMEOUT_MS) { end_cycle("failed"); return; }
        if (r->profile_pending.load()) { return; }
        if (!r->profile_ready.load() || !live_profile(live.index) || !runtime_binding_matches()) { end_cycle("failed"); return; }
        if (!r->authorized.load() || !r->profile_key_present.load()) { end_cycle("not_paired"); return; }
        if (tesla->guided.request[0] || tesla->state.get("pairing_pending")->asBool() ||
            !r->command_outcome.forward_available() || r->manual_poll_pending.exchange(true)) {
            end_cycle("waiting"); return; // An explicit action owns the worker; its result stands.
        }
        r->auto_poll_requested.store(true);
        a.phase = Tesla::AutoPoll::Phase::Polling; a.phase_ms = now;
        auto_poll_publish("polling");
        return;
    }
    case Tesla::AutoPoll::Phase::Polling:
        if (r && r->epoch == a.runtime_epoch && (r->auto_poll_requested.load() || r->manual_poll_pending.load())) { return; }
        // Bounded by the worker's request deadline; the worker published the outcome.
        end_cycle("waiting");
        return;
    }
}
