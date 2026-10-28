// Autonomous firmware SoC scheduler (TESLA_BLE_MANUAL_VEHICLE_UI builds only).
// Included by tesla_vehicle.cpp after tesla_profiles.inc. Main task only.
//
// Periodic SoC updates without an open browser or any external agent. The
// schedule is charge-aware (user decision):
//   - once after plug-in (per physical plug-in session = EV identity generation),
//     with a bounded retry/backoff for transient failures, never endlessly;
//   - while the EVSE reports active charging, every per-profile persisted
//     poll_interval (measured from the end of the previous request);
//   - a real charge start/resume requests promptly (bounded delay), even
//     before the interval would be due; the demand survives an operation
//     claimed before the edge (whatever its outcome) and is served once, by
//     the first request handed over after the edge;
//   - charge pause (e.g. PV/charge-manager pause) or charge end: no further
//     automatic request, scan or wake. The last successful value keeps its
//     real receive time; nothing is refreshed or re-stamped.
// Wake coupling: an automatic request may wake an asleep vehicle exactly in the
// contexts above (plug-in request incl. its bounded retries, charge start/resume,
// periodic while charging), always subject to TeslaBleSocPolicy's per-operation
// single wake and MIN_WAKE_SPACING_MS. There is no user wake switch any more;
// the legacy persisted profile flag "wake_once" is not consulted (no config rewrite).
// Post-wake follow-up: if an operation (manual or automatic) exposed a wake but
// delivered no SoC, ONE later request without any wake follows after
// AUTO_POLL_POST_WAKE_DELAY_MS (a just woken infotainment can need longer than one
// operation's session budget). An automatic origin needs its context to persist
// (charging, or the plug-in request still open); a manual origin belongs to the
// user's explicit request. The follow-up never wakes, so it cannot re-arm itself.
// The EVSE state is only observed (read-only); this module never starts,
// stops, releases or authorizes charging.
// One cycle reuses the existing production predicates unchanged:
//   1. read-only key/authorization/transport-binding probe (same as guidance),
//   2. an own VIN-scoped discovery for the CURRENT physical PEV generation,
//   3. fresh_binding(): exactly one connectable advertiser with the VIN name at
//      the profile's already persisted and authorized BLE address (an address
//      change still requires the user's explicit scan/adoption),
//   4. select_profile() with that scan revision (runtime-only, never persisted),
//   5. one bounded SoC operation through the normal worker request path.
// Never pairs, never writes key/binding material, never charges, never
// queues behind or concurrently with manual/guided/discovery/pairing work.
// Unplug, identity or profile changes abandon the cycle; the worker's own
// identity gate retires any in-flight operation. An operation already handed
// to the worker is drained (bounded by its request deadline), never duplicated.

static constexpr uint32_t AUTO_POLL_FIRST_DELAY_MS = 15000; // after plug-in/identity change
static constexpr uint32_t AUTO_POLL_RETRY_MS = 5000;        // non-radio deferral only
static constexpr uint32_t AUTO_POLL_SCAN_TIMEOUT_MS = (TeslaBleDiscovery::SCAN_SECONDS + 10) * 1000;
static constexpr uint32_t AUTO_POLL_SELECT_TIMEOUT_MS = 15000;
// Real charge start/resume: request after a short settle delay, but never
// sooner than this gap after any finished SoC operation (manual or automatic).
static constexpr uint32_t AUTO_POLL_CHARGE_START_DELAY_MS = 5000;
static constexpr uint32_t AUTO_POLL_MIN_GAP_MS = 30000; // = smallest configurable interval
// Post-wake follow-up: measured from the end of the waking operation; equal to the
// minimum gap, so it never crowds any other request.
static constexpr uint32_t AUTO_POLL_POST_WAKE_DELAY_MS = AUTO_POLL_MIN_GAP_MS;
// Plug-in request: at most this many cycles per plug-in session; backoff
// after the first and second unsuccessful one.
static constexpr uint8_t AUTO_POLL_INITIAL_ATTEMPTS = 3;
static constexpr uint32_t AUTO_POLL_INITIAL_BACKOFF_MS[AUTO_POLL_INITIAL_ATTEMPTS - 1] = {30000, 120000};

static bool auto_poll_same_identity(const TeslaBleIdentity &a, const TeslaBleIdentity &b)
{
    return a.generation == b.generation && a.index == b.index && a.mac == b.mac;
}

// Read-only EVSE observation on the main task, through the public API state
// "evse/state" (the EVSE backend updates it on the main task as well; no
// worker access). "Actively charging" is the EVSE's high-level charger_state
// CHARGER_STATE_CHARGING (3): IEC 61851 state C with the contactor switched on,
// the same value charge_manager uses for CASState::Charging. Plugged in but
// waiting for release (1, e.g. PV or charge-manager pause), ready to charge
// (2, vehicle not requesting or finished) and error (4) are NOT charging;
// neither is an allowed current > 0. Builds without an EVSE state: never
// charging, i.e. only the plug-in request.
static constexpr uint32_t AUTO_POLL_CHARGER_STATE_CHARGING = 3;
#ifdef CHARGER_STATE_CHARGING
static_assert(CHARGER_STATE_CHARGING == AUTO_POLL_CHARGER_STATE_CHARGING, "evse_common charger_state enum changed");
#endif

static bool auto_poll_actively_charging()
{
    static const Config *evse_state = nullptr;
    static bool looked_up = false;
    if (!looked_up) { // States are registered before the main loop runs; the pointer is stable.
        evse_state = api.getState("evse/state", false);
        looked_up = true;
    }
    return evse_state && evse_state->get("charger_state")->asUint() == AUTO_POLL_CHARGER_STATE_CHARGING;
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
            static const char *const names[] = {"none", "ok", "asleep_wake_disabled", "asleep_wake_limited", "woken_no_soc", "failed", "busy", "wake_sent_no_soc"};
            const auto value = r->poll_outcome.load();
            if (sequence != 0 && value < sizeof(names) / sizeof(names[0])) {
                tesla->state.get("poll_outcome")->updateString(names[value]);
                tesla->state.get("poll_outcome_automatic")->updateBool(r->poll_outcome_automatic.load());
                a.last_operation_ms = now; a.operation_seen = true;
                a.last_operation_manual = !r->poll_outcome_automatic.load();
                const auto outcome = static_cast<PollOutcome>(value);
                if (outcome == PollOutcome::Ok) {
                    a.post_wake_armed = a.post_wake = false; // SoC delivered: no follow-up needed
                    // A fresh SoC (e.g. the user's manual request) supersedes an older
                    // automatic failure; never leave it shown as the current state.
                    const auto &shown = tesla->state.get("auto_poll")->asString();
                    if (a.phase == Tesla::AutoPoll::Phase::Idle && (shown == "not_found" || shown == "failed" || shown == "after_wake")) {
                        auto_poll_publish(a.initial_done && !auto_poll_actively_charging() ? "not_charging" : "waiting");
                    }
                } else if (outcome == PollOutcome::WokenNoSoc || outcome == PollOutcome::WakeSentNoSoc) {
                    a.post_wake_armed = true; a.post_wake_manual = a.last_operation_manual;
                }
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
        a.identity = id; a.next_ms = now + AUTO_POLL_FIRST_DELAY_MS; // the one plug-in request
        a.soc_mark = tesla->state.get("last_update")->asUptime();
        auto_poll_publish("initial");
        return;
    }
    const uint32_t interval_ms = p->get("poll_interval")->asUint() * 1000U;
    const bool charging = auto_poll_actively_charging();
    // Charge start/resume request: short settle delay, but never sooner than
    // the minimum gap after any finished SoC operation.
    auto charge_start_due = [&] {
        uint32_t due = now + AUTO_POLL_CHARGE_START_DELAY_MS;
        const uint32_t gap_end = a.last_operation_ms + AUTO_POLL_MIN_GAP_MS;
        if (a.operation_seen && static_cast<int32_t>(gap_end - due) > 0) { due = gap_end; }
        return due;
    };
    if (charging != a.charging) {
        a.charging = charging;
        if (!charging) {
            a.charge_start = false; // pause/end: an unserved demand lapses; the next resume brings its own
        } else if (a.initial_done || a.phase != Tesla::AutoPoll::Phase::Idle || a.initial_attempts) {
            // Real charge start or resume (a plug-in request that has not
            // started yet already covers it). Also during a running cycle:
            // the demand stays pending across its end (see end_cycle).
            a.charge_start = true; ++a.charge_edges;
            if (a.phase == Tesla::AutoPoll::Phase::Idle) { a.next_ms = charge_start_due(); a.interval_scheduled = false; }
        }
    }
    // retryable: an unsuccessful plug-in request may be retried (bounded).
    auto end_cycle = [&](const char *state, bool retryable = true) {
        a.phase = Tesla::AutoPoll::Phase::Idle;
        a.cycle_post_wake = a.cycle_post_wake_manual = false;
        // The demand is served (once, even by a failure) only if no edge arrived
        // after this cycle's request hand-over (or start, without hand-over).
        if (a.cycle_edges == a.charge_edges) { a.charge_start = false; }
        if (a.cycle_initial && !a.initial_done) {
            ++a.initial_attempts;
            if (retryable && a.initial_attempts < AUTO_POLL_INITIAL_ATTEMPTS) {
                a.cycle_initial = false;
                a.next_ms = now + AUTO_POLL_INITIAL_BACKOFF_MS[a.initial_attempts - 1];
                if (a.charge_start && static_cast<int32_t>(charge_start_due() - a.next_ms) < 0) { a.next_ms = charge_start_due(); }
                a.interval_scheduled = false;
                auto_poll_publish(state);
                return;
            }
            a.initial_done = true;
        }
        a.cycle_initial = false;
        if (a.charge_start) {
            // Charge started/resumed while this cycle's request was already
            // handed over: a new request after the minimum gap, not one interval.
            a.next_ms = charge_start_due(); a.interval_scheduled = false;
        } else {
            a.next_ms = now + interval_ms;
            a.interval_base_ms = now; a.interval_applied_ms = interval_ms; a.interval_scheduled = true;
        }
        auto_poll_publish(state);
    };
    auto retry_later = [&] { a.next_ms = now + AUTO_POLL_RETRY_MS; a.interval_scheduled = false; };
    // A charge cycle whose charging ended before vehicle contact: no request
    // (a follow-up of the user's explicit request is not a charge cycle).
    const bool charge_cycle_obsolete = !a.cycle_initial && !a.cycle_post_wake_manual && !charging;
    char vin[18]{}, address[18]{};
    strlcpy(vin, p->get("vin")->asString().c_str(), sizeof(vin));
    strlcpy(address, p->get("ble_address")->asString().c_str(), sizeof(address));
    const uint8_t type = p->get("ble_address_type")->asUint();
    switch (a.phase) {
    case Tesla::AutoPoll::Phase::Idle: {
        if (!a.initial_done && tesla->state.get("last_update")->asUptime() != a.soc_mark) {
            // A SoC was already consumed for this identity (main publishes only
            // results of the current generation): no extra plug-in request.
            a.initial_done = true;
        }
        if (a.post_wake_armed) {
            // Schedule the single post-wake follow-up (no wake) after the waking operation.
            a.post_wake_armed = false; a.post_wake = true;
            a.next_ms = a.last_operation_ms + AUTO_POLL_POST_WAKE_DELAY_MS; a.interval_scheduled = false;
            auto_poll_publish("after_wake");
        }
        if (a.post_wake && !a.post_wake_manual && a.initial_done && !charging) {
            // Automatic origin whose context ended (pause/end): no new request.
            a.post_wake = false;
        }
        if (a.initial_done && !charging && !a.post_wake) {
            // Charge pause/end (or never charging after the plug-in request):
            // no automatic request, scan or wake until charging really starts.
            const auto &shown = tesla->state.get("auto_poll")->asString();
            if (shown == "waiting" || shown == "initial" || shown == "after_wake") { auto_poll_publish("not_charging"); }
            return;
        }
        if (a.interval_scheduled && a.interval_applied_ms != interval_ms) {
            // The user changed the interval: apply it to the pending wait now
            // (shorter or longer), measured from the end of the last cycle.
            // Wrap-safe: elapsed time is an unsigned difference.
            const uint32_t elapsed = now - a.interval_base_ms;
            a.next_ms = elapsed >= interval_ms ? now : now + (interval_ms - elapsed);
            a.interval_applied_ms = interval_ms;
        }
        if (static_cast<int32_t>(now - a.next_ms) < 0) { return; }
        // Minimum gap after any finished SoC operation: a charge start/resume
        // and a retry of the own plug-in request wait AUTO_POLL_MIN_GAP_MS; after
        // an explicit (manual) request the plug-in request and periodic requests
        // wait one interval, so they never crowd the user's own requests.
        const bool short_gap = a.charge_start || a.post_wake || (!a.initial_done && !a.last_operation_manual);
        if (a.operation_seen && now - a.last_operation_ms < (short_gap ? AUTO_POLL_MIN_GAP_MS : interval_ms)) { return; }
        // Never interleave with an explicit user flow or any busy worker state.
        TeslaBleDiscovery::Snapshot s{};
        const bool snapshot = bluetooth.discovery_snapshot(s);
        const bool foreign_result = snapshot && s.status == TeslaBleDiscovery::Status::Complete &&
            s.revision != a.own_revision && now - s.completed_at_ms < TeslaBleDiscovery::RESULT_TTL_MS;
        if (!snapshot || foreign_result || tesla->guided.request[0] || service_start_requested || !discovery_allowed()) {
            retry_later(); return;
        }
        a.cycle_initial = !a.initial_done; a.cycle_edges = a.charge_edges;
        // The follow-up is consumed by starting its cycle: at most one attempt.
        a.cycle_post_wake = a.post_wake; a.cycle_post_wake_manual = a.post_wake && a.post_wake_manual; a.post_wake = false;
        // Read-only probe under the same idle gate as publish_guidance().
        TeslaBleStorageAdapter store;
        if (!store.select_profile(live.index, live.mac) ||
            store.private_key_presence() != TeslaBleStorageAdapter::KeyPresence::Present ||
            !profile_material_authorized(store, vin, live.mac) || !profile_transport_matches(store, vin, address, type)) {
            end_cycle("not_paired", false); return;
        }
        TeslaBleDiscovery::Scope scope;
        scope.index = live.index; scope.mac = live.mac; scope.generation = live.generation;
        memcpy(scope.searched_vin, vin, 17);
        if (!tesla_ble_vehicle_name(vin, scope.name) || !bluetooth.request_discovery(scope)) {
            // Not started: no radio contact, the follow-up (if any) stays pending.
            a.cycle_initial = false; a.post_wake = a.cycle_post_wake; a.cycle_post_wake = a.cycle_post_wake_manual = false;
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
        if (charge_cycle_obsolete) { end_cycle("not_charging"); return; } // passive scan only, no contact
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
        if (!r->authorized.load() || !r->profile_key_present.load()) { end_cycle("not_paired", false); return; }
        if (charge_cycle_obsolete) { end_cycle("not_charging"); return; }
        if (tesla->guided.request[0] || tesla->state.get("pairing_pending")->asBool() ||
            !r->command_outcome.forward_available() || r->manual_poll_pending.exchange(true)) {
            end_cycle("waiting"); return; // An explicit action owns the worker; its result stands.
        }
        a.outcome_sequence0 = r->poll_outcome_sequence.load();
        a.cycle_edges = a.charge_edges; // this request is issued after every edge seen so far
        // Wake permission of this hand-over: every scheduler context except the
        // post-wake follow-up (stored before the request flag the worker claims).
        r->auto_poll_wake.store(!a.cycle_post_wake);
        r->auto_poll_requested.store(true);
        a.phase = Tesla::AutoPoll::Phase::Polling; a.phase_ms = now;
        auto_poll_publish("polling");
        return;
    }
    case Tesla::AutoPoll::Phase::Polling: {
        const bool own_runtime = r && r->epoch == a.runtime_epoch;
        if (own_runtime && charge_cycle_obsolete && r->auto_poll_requested.exchange(false)) {
            // Charging ended before the worker claimed the request: retract it
            // (no vehicle contact). Only this task sets manual_poll_pending for
            // an automatic request, and the worker never claimed it.
            r->manual_poll_pending.store(false);
            end_cycle("not_charging");
            return;
        }
        if (own_runtime && (r->auto_poll_requested.load() || r->manual_poll_pending.load())) { return; }
        // Bounded by the worker's request deadline; the worker published the outcome.
        // Plug-in request: success or "asleep, wake not permitted" is final for
        // this plug-in session; other outcomes may be retried (bounded).
        bool retryable = true;
        if (own_runtime && r->poll_outcome_sequence.load() != a.outcome_sequence0 && r->poll_outcome_automatic.load()) {
            const auto value = static_cast<PollOutcome>(r->poll_outcome.load());
            // "Asleep, wake not permitted" is final for the plug-in request only as a
            // configuration decision; a no-wake follow-up finding the car asleep is not.
            retryable = value != PollOutcome::Ok && !(value == PollOutcome::AsleepWakeDisabled && !a.cycle_post_wake);
        }
        end_cycle("waiting", retryable);
        return;
    }
    }
}
