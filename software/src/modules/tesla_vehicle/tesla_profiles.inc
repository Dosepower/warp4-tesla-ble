// Included by tesla_vehicle.cpp; production implementation, also host-executed.
#if TESLA_BLE_MANUAL_VEHICLE_UI
#include "tesla_ble_identifier.h"
static_assert(EV_MAX_EVS == TeslaBleStorageAdapter::MAX_PROFILES, "Review profile namespace bound when EV_MAX_EVS changes");
static uint64_t profile_mac_value(const char *text)
{
    if (!tesla_ble_valid_address(text)) { return 0; }
    unsigned b[6]{};
    if (sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) { return 0; }
    uint64_t value = 0;
    for (auto byte : b) { value = (value << 8) | byte; }
    return value;
}

Config TeslaVehicle::make_profile()
{
    return Config::Object({
        {"ev_index", Config::Int(-1, -1, EV_MAX_EVS - 1)},
        {"ev_mac", Config::Str("", 0, 17)},
        {"enable", Config::Bool(false)},
        {"vin", Config::Str("", 0, 17)},
        {"ble_address", Config::Str("", 0, 17)},
        {"ble_address_type", Config::Uint(0, 0, 1)},
        {"discovery_revision", Config::Uint(0,0,UINT32_MAX)},
        {"poll_interval", Config::Uint(300, 30, 3600)},
        {"wake_once", Config::Bool(false)},
    });
}

Config *TeslaVehicle::find_profile(int index) const
{
    auto entries = tesla->profiles.get("entries");
    for (size_t i = 0; i < entries->count(); ++i) {
        auto p = entries->get(i);
        if (p->get("ev_index")->asInt() == index) { return static_cast<Config *>(p); }
    }
    return nullptr;
}

// A saved authorization alone does not authorize arbitrary edited transports.
// Missing legacy transport evidence requires one explicit VIN-matching scan.
static bool profile_material_authorized(TeslaBleStorageAdapter &store, const char *vin, uint64_t mac)
{
    if (!tesla_ble_valid_vin(vin) || !mac) { return false; }
    uint8_t v[18]{}, m[6]{}, a[1]{};size_t n=0;
    if (store.private_key_presence()!=TeslaBleStorageAdapter::KeyPresence::Present ||
        store.load_into("vehicle_vin",v,sizeof(v),n)!=tesla_clean::LoadResult::Loaded || n!=17 || memcmp(v,vin,17) ||
        store.load_into("warp_ev_mac",m,sizeof(m),n)!=tesla_clean::LoadResult::Loaded || n!=6) { return false; }
    uint64_t bound=0;for(auto b:m) { bound=(bound<<8)|b; }
    return bound==mac && store.load_into("authorized",a,sizeof(a),n)==tesla_clean::LoadResult::Loaded && n==1 && a[0]==1;
}

static bool profile_transport_record(const char *vin,const char *address,uint8_t type,uint8_t (&record)[26])
{
    char name[19]{};unsigned bytes[6]{};
    if (type>1 || !tesla_ble_vehicle_name(vin,name) || !tesla_ble_valid_address(address) ||
        sscanf(address,"%2x:%2x:%2x:%2x:%2x:%2x",&bytes[0],&bytes[1],&bytes[2],&bytes[3],&bytes[4],&bytes[5])!=6) { return false; }
    record[0]=1;memcpy(record+1,name,18);
    for(unsigned i=0;i<6;++i) { record[19+i]=bytes[i]; }record[25]=type;return true;
}

static bool profile_transport_matches(TeslaBleStorageAdapter &store,const char *vin,const char *address,uint8_t type)
{
    uint8_t expected[26]{},saved[26]{};size_t n=0;
    return profile_transport_record(vin,address,type,expected) &&
        store.load_into("ble_binding",saved,sizeof(saved),n)==tesla_clean::LoadResult::Loaded && n==sizeof(saved) &&
        !memcmp(expected,saved,sizeof(saved));
}

bool TeslaVehicle::fresh_binding(int index,uint64_t mac,const char *vin,const char *address,uint8_t type,uint32_t revision)
{
    TeslaBleDiscovery::Scope scope;scope.index=index;scope.mac=mac;
    scope.generation=ev.get_live_identity().generation;
    uint8_t record[26]{};
    return index >= 0 && index < EV_MAX_EVS && revision > tesla->reset_discovery_revision[index] &&
        profile_transport_record(vin,address,type,record) && tesla_ble_vehicle_name(vin,scope.name) &&
        ev.get_profile_mac(index)==mac && bluetooth.discovery_matches(scope,revision,record+19,type);
}

bool TeslaVehicle::runtime_binding_matches()
{
    auto *r=tesla->runtime_owner.get();
    if (!r || r->profile_pending.load() || !r->profile_ready.load() || !r->profile_actionable.load()) { return false; }
    const auto identity=r->session.identity();
    if (!identity || !active_identity_matches(*identity) || identity->index!=r->configured_ev_index ||
        identity->mac!=r->profile_mac || ev.get_profile_mac(identity->index)!=r->profile_mac ||
        !r->command_outcome.binding_identity_current(*identity)) { return false; }
    // Fresh discovery is required at admission. Only the accepted in-flight
    // command may outlive its TTL: at most 120s (30s worker setup + bounded
    // send + the full 60s approval window). Identity/config/profile/transport
    // remain immutable while it owns the runtime. Cancellation revokes this
    // authority immediately; no subsequent poll/retry inherits it.
    const auto authority = r->command_outcome.pair_binding_current(millis(), *identity,
            tesla->config_revision, r->binding_revision);
    if (authority) { return *authority && bluetooth.discovery_authority_current(r->binding_revision); }
    // Stored authorization reuses the key, never an expired radio discovery.
    TeslaBleDiscovery::Scope scope;scope.index=r->configured_ev_index;scope.mac=r->profile_mac;
    scope.generation=identity->generation;
    return tesla_ble_vehicle_name(r->vin.c_str(),scope.name) &&
        r->binding_revision > tesla->reset_discovery_revision[identity->index] &&
        bluetooth.discovery_matches(scope,r->binding_revision,r->remote_address,r->remote_address_type);
}

String TeslaVehicle::validate_profiles(Config &update, ConfigSource source)
{
    if (tesla->legacy_blocked) { return checked_error_string("Legacy Tesla data retained; verified offline migration required"); }
    if (source != ConfigSource::File && (bluetooth.discovery_busy() || service_start_requested ||
        (tesla->runtime_owner.get() && !discovery_allowed()))) {
        return checked_error_string("Tesla action busy; wait before editing profiles");
    }
    auto entries = update.get("entries");
    if (entries->count() > EV_MAX_EVS) { return checked_error_string("Too many WARP profiles"); }
    uint32_t seen = 0;
    for (size_t i = 0; i < entries->count(); ++i) {
        auto p = entries->get(i);
        const int index = p->get("ev_index")->asInt();
        const auto mac = profile_mac_value(p->get("ev_mac")->asString().c_str());
        if (index < 0 || index >= EV_MAX_EVS || (seen & (1U << index))) { return checked_error_string("Duplicate or invalid ev_index"); }
        seen |= 1U << index;
        if (!mac || ev.get_profile_mac(index) != mac) { return checked_error_string("Existing WARP profile with exact EV MAC required; no wildcard"); }
        const auto &vin = p->get("vin")->asString();
        const auto &address = p->get("ble_address")->asString();
        // Permission can be saved before discovery. It never starts a worker
        // or authorizes a transport; select_profile still requires a fresh binding.
        if ((vin.length() && !tesla_ble_valid_vin(vin.c_str())) ||
            (address.length() && !tesla_ble_valid_address(address.c_str()))) {
            return checked_error_string("Valid 17-character VIN and BLE address required when provided");
        }
        // Never silently replace a local identity when an index is reused or
        // the VIN changes. Even disabled mappings must retain their binding.
        auto *old = find_profile(index);
        if (source != ConfigSource::File && !old) {
            // A missing mapping is not evidence of empty storage. Orphaned
            // credentials/transactions must not be rebound to a new VIN.
            TeslaBleStorageAdapter store;
            if (!store.select_profile(index, mac) ||
                store.private_key_presence() != TeslaBleStorageAdapter::KeyPresence::Absent) {
                return checked_error_string("New profile namespace not proven clean; retained material untouched");
            }
        }
        if (source != ConfigSource::File && old && old->get("ble_address")->asString().length() && !address.length()) {
            TeslaBleStorageAdapter store; uint8_t auth[1]{}; size_t n=0;
            if (!store.select_profile(index, profile_mac_value(old->get("ev_mac")->asString().c_str())) ||
                store.private_key_presence() != TeslaBleStorageAdapter::KeyPresence::Absent ||
                store.load_into("authorized",auth,sizeof(auth),n) != tesla_clean::LoadResult::Absent) {
                return checked_error_string("First unpair this profile; retained key binding cannot be edited");
            }
        }
        if (source != ConfigSource::File && address.length()) {
            TeslaBleStorageAdapter store;
            const auto type=p->get("ble_address_type")->asUint();
            const bool saved=store.select_profile(index,mac) && profile_material_authorized(store,vin.c_str(),mac) &&
                profile_transport_matches(store,vin.c_str(),address.c_str(),type);
            // An unchanged mapping in another saved profile is retention, not
            // fresh radio authority. Selection/commit still require fresh_binding.
            auto *old = find_profile(index);
            const bool retained = old && index != ev.get_live_identity().index && old->get("ev_mac")->asString() == p->get("ev_mac")->asString() && old->get("vin")->asString() == vin &&
                old->get("ble_address")->asString() == address && old->get("ble_address_type")->asUint() == type &&
                old->get("discovery_revision")->asUint() == p->get("discovery_revision")->asUint();
            if (!saved && !retained && !fresh_binding(index,mac,vin.c_str(),address.c_str(),type,p->get("discovery_revision")->asUint())) {
                return checked_error_string("Fresh exact VIN search match required; manual address, expired or replaced search rejected");
            }
        }
        if (source != ConfigSource::File && old &&
            (old->get("ev_mac")->asString() != p->get("ev_mac")->asString() || old->get("vin")->asString() != vin)) {
            TeslaBleStorageAdapter store;
            if (!store.select_profile(index, profile_mac_value(old->get("ev_mac")->asString().c_str())) ||
                store.private_key_presence() != TeslaBleStorageAdapter::KeyPresence::Absent) {
                return checked_error_string("Delete only this profile's local key under Advanced before changing its binding");
            }
        }
        for (size_t j = 0; j < i; ++j) {
            auto other = entries->get(j);
            if (other->get("ev_mac")->asString() == p->get("ev_mac")->asString() ||
                (vin.length() && other->get("vin")->asString() == vin)) {
                return checked_error_string("EV MAC or VIN already assigned to another profile");
            }
        }
    }
    if (source != ConfigSource::File) {
        auto old_entries = tesla->profiles.get("entries");
        for (size_t i = 0; i < old_entries->count(); ++i) {
            auto old = old_entries->get(i);
            const int index = old->get("ev_index")->asInt();
            if (index >= 0 && !(seen & (1U << index))) {
                TeslaBleStorageAdapter store;
                if (!store.select_profile(index, profile_mac_value(old->get("ev_mac")->asString().c_str())) ||
                    store.private_key_presence() != TeslaBleStorageAdapter::KeyPresence::Absent) {
                    return checked_error_string("Delete this profile's key before removing its Tesla mapping");
                }
            }
        }
    }
    if (source != ConfigSource::File) {
        if (tesla->config_revision == UINT32_MAX) { return checked_error_string("Profile sequence exhausted; reboot required"); }
        ++tesla->config_revision; // Also cancels A→B→A edits, even if bytes return.
        clear_guided();
        tesla->guidance_refresh = true;
    }
    if (source != ConfigSource::File && tesla->runtime_owner.get()) {
        tesla->runtime_owner.get()->profile_ready.store(false); // Reload explicitly; never apply/save into a live protocol object.
    }
    return "";
}

bool TeslaVehicle::active_identity_matches(const TeslaBleIdentity &identity) const
{
    const auto live = ev.get_live_identity();
    return live.index >= 0 && live.mac != 0 && live.generation == identity.generation && live.index == identity.index && live.mac == identity.mac;
}

void TeslaVehicle::refresh_live_session()
{
    auto *r = tesla->runtime_owner.get();
    const auto live = ev.get_live_identity();
    r->session.publish({live.generation, live.index, live.mac});
}

bool TeslaVehicle::live_profile(int index) const
{
    if (!tesla || index < 0 || index != tesla->selected_index || !accepts_commands()) { return false; }
    auto *r = tesla->runtime_owner.get();
    auto *p = find_profile(index);
    if (!p || !p->get("enable")->asBool()) { return false; }
    const auto live = ev.get_live_identity();
    return live.index == index && live.mac == r->profile_mac && r->configured_ev_index.load() == index && ev.get_active_ev_index() == index &&
        ev.get_active_ev_mac() != 0 && ev.get_active_ev_mac() == r->profile_mac && ev.get_profile_mac(index) == r->profile_mac;
}

bool TeslaVehicle::select_profile(int index, bool deleting, String &errmsg)
{
    if (!discovery_allowed()) { errmsg = checked_error_string("Bluetooth inactive, restart pending, migration blocked or action busy"); return false; }
    auto *p = find_profile(index);
    if (!p) { errmsg = checked_error_string("No Tesla mapping for selected WARP profile"); return false; }
    const uint64_t mac = profile_mac_value(p->get("ev_mac")->asString().c_str());
    // Deletion may clean an orphan after an EV profile edit; its recorded
    // namespace, not the new EV MAC, is the exact deletion target.
    if (!deleting && (!p->get("enable")->asBool() || !mac || ev.get_profile_mac(index) != mac ||
        ev.get_active_ev_index() != index || ev.get_active_ev_mac() != mac)) {
        errmsg = checked_error_string("Selected WARP profile not active or exact EV MAC binding changed"); return false;
    }
    if (!mac) { errmsg = checked_error_string("Invalid saved EV MAC; no deletion attempted"); return false; }
    if (deleting) {
        // Probe before revoking config or allocating a worker. Unknown/absent
        // storage is not permission for a destructive namespace operation.
        TeslaBleStorageAdapter store;
        if (!store.select_profile(index, mac)) { errmsg = checked_error_string("Unknown key state; retained untouched"); return false; }
        const auto presence = store.private_key_presence();
        if (presence != TeslaBleStorageAdapter::KeyPresence::Present && presence != TeslaBleStorageAdapter::KeyPresence::CleanupRequired) {
            errmsg = checked_error_string("No proven local key material; retained untouched"); return false;
        }
        if (tesla->config_revision == UINT32_MAX) { errmsg = checked_error_string("Profile sequence exhausted; reboot required"); return false; }
        // Consume/revoke consent, not the user's saved vehicle assignment.
        // Worker retires the old protocol and erases only this namespace's
        // key/auth/session material before it can become actionable again.
        ++tesla->config_revision;
        clear_guided();
        tesla->guidance_refresh = true;
    }
    if (!deleting) {
        if (!fresh_binding(index,mac,p->get("vin")->asString().c_str(),p->get("ble_address")->asString().c_str(),
                p->get("ble_address_type")->asUint(),p->get("discovery_revision")->asUint())) {
            errmsg=checked_error_string("Search this VIN again and select its exact match before loading or pairing");return false;
        }
    }
    auto *r = tesla->runtime_owner.get();
    if (!r) {
        for (const char *key : {"vin", "ble_address", "ev_mac"}) {
            if (!tesla->config.get(key)->updateStringChecked(p->get(key)->asString().c_str())) { errmsg = "OOM"; return false; }
        }
        tesla->config.get("enable")->updateBool(true);
        tesla->config.get("ev_index")->updateInt(index);
        if (!start_runtime(deleting)) { errmsg = checked_error_string("Could not allocate local Tesla worker"); return false; }
        r = tesla->runtime_owner.get();
    }
    Runtime::ProfileRequest request;
    request.index = index; request.mac = mac; request.deleting = deleting;
    request.discovery_revision=p->get("discovery_revision")->asUint();request.address_type=p->get("ble_address_type")->asUint();
    strlcpy(request.vin, p->get("vin")->asString().c_str(), sizeof(request.vin));
    strlcpy(request.address, p->get("ble_address")->asString().c_str(), sizeof(request.address));
    r->profile_ready.store(false);
    r->profile_request = request; // Handoff: no writer until worker clears pending.
    tesla->selected_index = index;
    tesla->published_index = -1;
    r->profile_pending.store(true, std::memory_order_release);
    publish_bluetooth_state();
    return true;
}

void TeslaVehicle::apply_profile()
{
    auto *r = tesla->runtime_owner.get();
    if (!r->profile_pending.load(std::memory_order_acquire)) { return; }
    const auto request = r->profile_request;
    disconnect_internal(); // Retire old link/protocol before touching any namespace.
    if (r->stop_requested.load()) { return; }
    r->vehicle.reset(); r->keys.reset();
    r->profile_actionable.store(false);
    r->reset_pair_diagnostics();
    r->deleting_profile = request.deleting;
    xQueueReset(r->queues.events());
    r->manual_poll_pending.store(false); r->poll_requested.store(false); r->pair_requested.store(false);
    r->bound_vehicle_mac.store(0); r->session.bind(0); r->authorized.store(false);
    r->vin.assign(request.vin); r->configured_ev_index.store(request.index); r->profile_mac = request.mac;
    r->binding_revision=request.discovery_revision;r->remote_address_type=request.address_type;
    // The old adapter has no file/identity leases: only worker touches it.
    if (!r->storage_adapter.select_profile(request.index, request.mac)) {
        post_event(EventType::Error, "Could not select profile storage"); r->stop_requested.store(true); return;
    }
    bool ok = true;
    if (request.deleting) { ok = r->storage->forget_material(); }
    uint8_t vin[18]{}, bound[6]{}, authorization[1]{}; size_t n = 0;
    const auto v = r->storage->load_into("vehicle_vin", vin, sizeof(vin), n);
    ok = ok && (v == tesla_clean::LoadResult::Absent || (v == tesla_clean::LoadResult::Loaded && n == 17 && !memcmp(vin, request.vin, 17)));
    const auto b = r->storage->load_into("warp_ev_mac", bound, sizeof(bound), n);
    uint64_t mac = 0; for (auto byte : bound) { mac = (mac << 8) | byte; }
    ok = ok && (b == tesla_clean::LoadResult::Absent || (b == tesla_clean::LoadResult::Loaded && n == 6 && mac == request.mac));
    if (b == tesla_clean::LoadResult::Loaded && ok) { r->bound_vehicle_mac.store(mac); r->session.bind(mac); }
    const auto a = r->storage->load_into("authorized", authorization, sizeof(authorization), n);
    ok = ok && (a == tesla_clean::LoadResult::Absent || (a == tesla_clean::LoadResult::Loaded && n == 1 && authorization[0] == 1));
    const auto presence = r->storage->private_key_presence();
    r->profile_key_presence.store(presence);
    r->profile_key_present.store(presence == TeslaBleStorageAdapter::KeyPresence::Present);
    ok = ok && (presence != TeslaBleStorageAdapter::KeyPresence::Present || v == tesla_clean::LoadResult::Loaded);
    r->authorized.store(ok && a == tesla_clean::LoadResult::Loaded && mac == request.mac &&
        v == tesla_clean::LoadResult::Loaded && presence == TeslaBleStorageAdapter::KeyPresence::Present);
    unsigned bytes[6]{};
    if (!request.deleting && sscanf(request.address, "%2x:%2x:%2x:%2x:%2x:%2x", &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) != 6) { ok = false; }
    for (unsigned i = 0; i < 6; ++i) { r->remote_address[i] = bytes[i]; }
    if (!request.deleting) {
        const bool saved=r->authorized.load() && profile_transport_matches(*r->storage,request.vin,request.address,request.address_type);
        const auto live=ev.get_live_identity();
        const bool fresh=live.index==request.index && live.mac==request.mac &&
            fresh_binding(request.index,request.mac,request.vin,request.address,request.address_type,request.discovery_revision);
        if (!fresh) { ok=false; }
        if (ok && !saved) {
            uint8_t record[26]{};
            ok=profile_transport_record(request.vin,request.address,request.address_type,record) &&
                tesla_clean::KeyStorage::committed(r->storage->save_transaction("ble_binding",record,sizeof(record)));
        }
    }
    if (!ok || (!request.deleting && !create_vehicle())) {
        post_event(EventType::Error, "Profile storage inconsistent or VIN changed; retained untouched; delete only this profile to reset");
        r->stop_requested.store(true); return;
    }
    r->deleting_profile = false;
    bluetooth_memory::sample(bluetooth_memory::Stage::ProfileApplied,true);
    r->profile_actionable.store(!request.deleting);
    post_event(EventType::KeyPresence, nullptr, presence == TeslaBleStorageAdapter::KeyPresence::Present ? 1.0f :
        presence == TeslaBleStorageAdapter::KeyPresence::Absent ? 0.0f : presence == TeslaBleStorageAdapter::KeyPresence::CleanupRequired ? 2.0f : NAN);
    r->profile_ready.store(true, std::memory_order_release);
    r->profile_pending.store(false, std::memory_order_release);
}

void TeslaVehicle::publish_profile()
{
    auto *r = tesla->runtime_owner.get();
    tesla->state.get("active_ev_index")->updateInt(ev.get_active_ev_index());
    if (!r || r->profile_pending.load(std::memory_order_acquire) || !r->profile_ready.load(std::memory_order_acquire)) { return; }
    const int index = r->configured_ev_index.load();
    if (tesla->published_index == index) { return; }
    tesla->published_index = index;
    tesla->pairing_policy.on_key_removed();
    tesla->state.get("profile_ev_index")->updateInt(index);
    tesla->state.get("paired")->updateBool(r->authorized.load());
    tesla->state.get("key_present")->updateBool(r->profile_key_present.load());
    const auto presence = r->profile_key_presence.load();
    tesla_ble_publish_presence(tesla->state.get("key_presence"), presence == TeslaBleStorageAdapter::KeyPresence::Present,
        presence != TeslaBleStorageAdapter::KeyPresence::Unknown, presence == TeslaBleStorageAdapter::KeyPresence::CleanupRequired);
    tesla->state.get("pairing_pending")->updateBool(false);
    tesla->state.get("pair_request_sent")->updateBool(false);
    tesla->state.get("connected")->updateBool(false);
    publish_error("");
}

void TeslaVehicle::clear_guided()
{
    tesla->guided = {};
    tesla->guidance.get("ready")->updateBool(false);
    tesla->guidance.get("request")->updateString("");
}

bool TeslaVehicle::guided_current() const
{
    const auto &g = tesla->guided;
    auto *r = tesla->runtime_owner.get();
    return g.request[0] && r && r->epoch == g.runtime_epoch &&
        tesla->config_revision == g.config_revision && active_identity_matches(g.identity) &&
        uint32_t(millis() - g.started_ms) < 15000 && bluetooth.active() && !bluetooth_restart_pending &&
        !tesla->runtime_owner.restart_required() && !tesla->runtime_owner.is_stopping() && !r->stop_requested.load();
}

void TeslaVehicle::guided_command(String &errmsg)
{
    auto &command = tesla->guided_command;
    const auto &phase = command.get("phase")->asString();
    const auto &request = command.get("request")->asString();
    if (request.length() != 32) { errmsg = checked_error_string("Invalid explicit action request"); return; }
    for (auto c : request) { if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
        errmsg = checked_error_string("Invalid explicit action request"); return;
    } }
    // Strict JSON presence is enforced by ConfigRoot before this callback.
    // Equality alone cannot detect A->B->A: all phases carry the monotonic
    // revision captured with consent, not just the destructive reset phase.
    const auto command_live = ev.get_live_identity();
    char command_generation[21];
    snprintf(command_generation, sizeof(command_generation), "%llu", static_cast<unsigned long long>(command_live.generation));
    if (command.get("revision")->asUint() != tesla->config_revision ||
        command.get("generation")->asString() != command_generation) {
        errmsg = checked_error_string("Configuration or connected identity changed; confirm again"); return;
    }
    if (phase == "commit" || phase == "cancel") {
        auto p = command.get("profile");
        auto *saved = find_profile(p->get("ev_index")->asInt());
        if (!saved || p->get("ev_index")->asInt() != tesla->guided.identity.index) {
            errmsg = checked_error_string("Prepared profile changed; confirm again"); return;
        }
        for (const char *key : {"ev_mac", "vin", "ble_address"}) {
            if (p->get(key)->asString() != saved->get(key)->asString()) { errmsg = checked_error_string("Saved profile changed; confirm again"); return; }
        }
        for (const char *key : {"ble_address_type", "discovery_revision", "poll_interval"}) {
            if (p->get(key)->asUint() != saved->get(key)->asUint()) { errmsg = checked_error_string("Saved profile changed; confirm again"); return; }
        }
        if (p->get("enable")->asBool() != saved->get("enable")->asBool() || p->get("wake_once")->asBool() != saved->get("wake_once")->asBool()) {
            errmsg = checked_error_string("Saved permission changed; confirm again"); return;
        }
    }
    if (phase == "unpair" || phase == "cleanup") {
        // Explicit local destruction has distinct intents. Both use the saved
        // namespace (including orphan EV mappings), never a caller-selected MAC.
        auto p = command.get("profile");
        const int index = p->get("ev_index")->asInt();
        auto *saved = find_profile(index);
        if (!saved || !discovery_allowed() || service_start_requested) {
            errmsg = checked_error_string("Saved profile unavailable or action busy; confirm again"); return;
        }
        for (const char *key : {"ev_mac", "vin", "ble_address"}) {
            if (p->get(key)->asString() != saved->get(key)->asString()) { errmsg = checked_error_string("Saved profile changed; confirm again"); return; }
        }
        for (const char *key : {"ble_address_type", "discovery_revision", "poll_interval"}) {
            if (p->get(key)->asUint() != saved->get(key)->asUint()) { errmsg = checked_error_string("Saved profile changed; confirm again"); return; }
        }
        if (p->get("enable")->asBool() != saved->get("enable")->asBool() || p->get("wake_once")->asBool() != saved->get("wake_once")->asBool()) {
            errmsg = checked_error_string("Saved permission changed; confirm again"); return;
        }
        const auto mac = profile_mac_value(saved->get("ev_mac")->asString().c_str());
        TeslaBleStorageAdapter store;
        if (!store.select_profile(index, mac)) { errmsg = checked_error_string("Unknown key state; retained untouched"); return; }
        const auto presence = store.private_key_presence();
        uint8_t auth[1]{}; size_t size = 0;
        const auto authorization = store.load_into("authorized", auth, sizeof(auth), size);
        if ((presence != TeslaBleStorageAdapter::KeyPresence::Present && presence != TeslaBleStorageAdapter::KeyPresence::CleanupRequired) ||
            (authorization != tesla_clean::LoadResult::Absent && !(authorization == tesla_clean::LoadResult::Loaded && size == 1 && auth[0] == 1))) {
            errmsg = checked_error_string("Key or authorization state uncertain; retained untouched"); return;
        }
        const bool paired = profile_material_authorized(store, saved->get("vin")->asString().c_str(), mac);
        if (authorization == tesla_clean::LoadResult::Loaded && !paired) {
            errmsg = checked_error_string("Stored authorization binding uncertain; retained untouched"); return;
        }
        if ((phase == "unpair") != paired) {
            errmsg = checked_error_string("Pairing state changed; confirm the appropriate local action again"); return;
        }
        select_profile(index, true, errmsg); // Serialized worker retires old ownership before local deletion.
        return; // Never sends vehicle-side key removal, pairing, poll or charging.
    }
    if (phase == "reset") {
        // Local-only confirmed reset. Exact profile + monotonic config revision
        // + live identity generation reject stale/ABA/replayed consent.
        auto p = command.get("profile");
        const int index = p->get("ev_index")->asInt();
        auto *saved = find_profile(index);
        const auto live = ev.get_live_identity();
        char generation[21]; snprintf(generation, sizeof(generation), "%llu", static_cast<unsigned long long>(live.generation));
        if (!saved || command.get("revision")->asUint() != tesla->config_revision ||
            command.get("generation")->asString() != generation || !discovery_allowed() || service_start_requested ||
            tesla->config_revision == UINT32_MAX || ev.get_profile_mac(index) != profile_mac_value(p->get("ev_mac")->asString().c_str())) {
            errmsg = checked_error_string("Assignment changed or action busy; confirm again"); return;
        }
        for (const char *key : {"ev_mac", "vin", "ble_address"}) {
            if (p->get(key)->asString() != saved->get(key)->asString()) { errmsg = checked_error_string("Saved profile changed; confirm again"); return; }
        }
        for (const char *key : {"ble_address_type", "discovery_revision", "poll_interval"}) {
            if (p->get(key)->asUint() != saved->get(key)->asUint()) { errmsg = checked_error_string("Saved profile changed; confirm again"); return; }
        }
        if (p->get("enable")->asBool() != saved->get("enable")->asBool() || p->get("wake_once")->asBool() != saved->get("wake_once")->asBool()) {
            errmsg = checked_error_string("Saved permission changed; confirm again"); return;
        }
        TeslaBleStorageAdapter store;
        const auto mac = profile_mac_value(saved->get("ev_mac")->asString().c_str());
        if (!store.select_profile(index, mac)) { errmsg = checked_error_string("Unknown key state; retained untouched"); return; }
        const auto presence = store.private_key_presence();
        uint8_t auth[1]{}, vin[18]{}; size_t n=0;
        const auto authorization = store.load_into("authorized", auth, sizeof(auth), n);
        // Any authorization artifact or uncertain/orphan binding fails closed.
        if (authorization != tesla_clean::LoadResult::Absent ||
            (presence != TeslaBleStorageAdapter::KeyPresence::Absent && presence != TeslaBleStorageAdapter::KeyPresence::Present) ||
            (presence == TeslaBleStorageAdapter::KeyPresence::Present &&
             (saved->get("vin")->asString().length() != 17 || store.load_into("vehicle_vin", vin, sizeof(vin), n) != tesla_clean::LoadResult::Loaded || n != 17 || memcmp(vin, saved->get("vin")->asString().c_str(), 17)))) {
            errmsg = checked_error_string("First unpair this profile; key or authorization retained"); return;
        }
        if (!store.remove_key("ble_binding")) { errmsg = checked_error_string("Could not clear transport assignment; key retained"); return; }
        TeslaBleDiscovery::Snapshot snapshot;
        if (bluetooth.discovery_snapshot(snapshot)) tesla->reset_discovery_revision[index] = snapshot.revision;
        saved->get("enable")->updateBool(false);
        saved->get("ble_address")->updateString(""); saved->get("ble_address_type")->updateUint(0); saved->get("discovery_revision")->updateUint(0);
        ++tesla->config_revision; clear_guided(); tesla->guidance_refresh = true;
        if (auto *r = tesla->runtime_owner.get()) r->profile_ready.store(false);
        api.writeConfig("tesla_vehicle/profiles", &tesla->profiles);
        publish_guidance();
        return; // No worker start, key deletion, scan, pair, poll or charging.
    }
    if (phase == "cancel") {
        if (request == tesla->guided.request) { clear_guided(); }
        return;
    }
    const bool pair = phase == "prepare_pair";
    if (pair || phase == "prepare_poll") {
        if (tesla->guided.request[0] && guided_current()) { errmsg = checked_error_string("Explicit action preparation pending"); return; }
        clear_guided();
        auto p = command.get("profile");
        const int index = p->get("ev_index")->asInt();
        auto *saved = find_profile(index);
        const auto live = ev.get_live_identity();
        char generation[21]; snprintf(generation, sizeof(generation), "%llu", static_cast<unsigned long long>(live.generation));
        if (!saved || command.get("generation")->asString() != generation || live.index != index ||
            live.mac != profile_mac_value(p->get("ev_mac")->asString().c_str())) {
            errmsg = checked_error_string("Connected profile changed; confirm again"); return;
        }
        // Explicit config equality before allocating/loading anything. Do not
        // trust frontend cached state or an index alone as action consent.
        for (const char *key : {"ev_mac", "vin", "ble_address"}) {
            if (p->get(key)->asString() != saved->get(key)->asString()) { errmsg = checked_error_string("Saved profile changed; confirm again"); return; }
        }
        for (const char *key : {"ble_address_type", "discovery_revision", "poll_interval"}) {
            if (p->get(key)->asUint() != saved->get(key)->asUint()) { errmsg = checked_error_string("Saved profile changed; confirm again"); return; }
        }
        if (p->get("enable")->asBool() != saved->get("enable")->asBool() ||
            p->get("wake_once")->asBool() != saved->get("wake_once")->asBool()) {
            errmsg = checked_error_string("Saved permission changed; confirm again"); return;
        }
        if (!select_profile(index, false, errmsg)) { return; }
        auto &g = tesla->guided;
        strlcpy(g.request, request.c_str(), sizeof(g.request));
        g.identity = {live.generation, live.index, live.mac};
        g.config_revision = tesla->config_revision; g.runtime_epoch = tesla->runtime_owner.get()->epoch;
        g.started_ms = millis(); g.pair = pair;
        publish_guidance(); // Local preparation only. Never chain a radio action here.
        return;
    }
    if (phase != "commit") { errmsg = checked_error_string("Invalid explicit action phase"); return; }
    auto *r = tesla->runtime_owner.get();
    if (request != tesla->guided.request || !guided_current() || !r ||
        r->profile_pending.load() || !r->profile_ready.load() ||
        r->configured_ev_index.load() != tesla->guided.identity.index || !runtime_binding_matches()) {
        // A stale caller may not cancel somebody else's valid preparation.
        if (request == tesla->guided.request) { clear_guided(); }
        errmsg = checked_error_string("Preparation expired, failed or profile changed; confirm again"); return;
    }
    const int index = tesla->guided.identity.index;
    const bool pairing = tesla->guided.pair;
    clear_guided(); // Single use, including rejected key/prerequisite checks.
    if (pairing) { request_pair(index, errmsg); } else { request_poll(index, errmsg); }
}

void TeslaVehicle::publish_guidance()
{
    tesla->guidance.get("revision")->updateUint(tesla->config_revision);
    const auto live = ev.get_live_identity();
    char generation[21]; snprintf(generation, sizeof(generation), "%llu", static_cast<unsigned long long>(live.generation));
    tesla->guidance.get("generation")->updateStringChecked(generation);
    auto *r = tesla->runtime_owner.get();
    if (tesla->guided.request[0] && !guided_current()) { clear_guided(); }
    const bool ready = guided_current() && r && !r->profile_pending.load() && r->profile_ready.load() &&
        live_profile(tesla->guided.identity.index) && runtime_binding_matches();
    tesla->guidance.get("ready")->updateBool(ready);
    tesla->guidance.get("request")->updateStringChecked(tesla->guided.request);
    auto freshness_rows = tesla->guidance.get("profiles");
    // WARP additions/edits do not necessarily change Tesla configuration.
    // Bind bootstrap evidence to the exact namespace, not the selected index.
    for (int index = 0; index < EV_MAX_EVS; ++index) {
        const auto mac = ev.get_profile_mac(index);
        if (tesla->guidance_macs[index] != mac) {
            tesla->guidance_macs[index] = mac; tesla->guidance_refresh = true;
        }
    }
    for (size_t i = 0; i < freshness_rows->count(); ++i) {
        auto row = freshness_rows->get(i);
        auto *p = find_profile(row->get("ev_index")->asInt());
        row->get("binding_fresh")->updateBool(p && fresh_binding(p->get("ev_index")->asInt(),
            profile_mac_value(p->get("ev_mac")->asString().c_str()), p->get("vin")->asString().c_str(),
            p->get("ble_address")->asString().c_str(), p->get("ble_address_type")->asUint(), p->get("discovery_revision")->asUint()));
    }
    const int signature = tesla->state.get("profile_ev_index")->asInt() * 16 +
        (tesla->state.get("paired")->asBool() ? 8 : 0) + (tesla->state.get("key_present")->asBool() ? 4 : 0) +
        (tesla->state.get("key_presence")->asString() == "cleanup" ? 2 : 0);
    if (signature != tesla->guidance_key_signature) { tesla->guidance_refresh = true; tesla->guidance_key_signature = signature; }
    // Read-only boot/config/key-change summary, never starts a worker or scans.
    // Don't read a namespace concurrently with its worker transaction.
    if (tesla->legacy_blocked || !discovery_allowed() || tesla->guided.request[0]) {
        for (size_t i = 0; i < freshness_rows->count(); ++i) {
            freshness_rows->get(i)->get("namespace_clean")->updateBool(false);
        }
        tesla->guidance_refresh = true; return;
    }
    if (!tesla->guidance_refresh) { return; }
    auto rows = tesla->guidance.get("profiles");
    size_t count = 0;
    for (int index = 0; index < EV_MAX_EVS; ++index) {
        if (find_profile(index) || tesla->guidance_macs[index]) { ++count; }
    }
    if (!rows->setCount(count)) { return; }
    size_t at = 0;
    for (int index = 0; index < EV_MAX_EVS; ++index) {
        auto *p = find_profile(index);
        const auto current_mac = tesla->guidance_macs[index];
        if (!p && !current_mac) { continue; }
        auto row = rows->get(at++);
        const auto mac = p ? profile_mac_value(p->get("ev_mac")->asString().c_str()) : current_mac;
        TeslaBleStorageAdapter store;
        auto presence = TeslaBleStorageAdapter::KeyPresence::Unknown;
        bool paired = false;
        if (store.select_profile(index, mac)) {
            presence = store.private_key_presence();
            paired = p && presence == TeslaBleStorageAdapter::KeyPresence::Present &&
                profile_material_authorized(store, p->get("vin")->asString().c_str(), mac);
        }
        char mac_text[18];
        snprintf(mac_text, sizeof(mac_text), "%02X:%02X:%02X:%02X:%02X:%02X",
            unsigned((mac >> 40) & 255), unsigned((mac >> 32) & 255), unsigned((mac >> 24) & 255),
            unsigned((mac >> 16) & 255), unsigned((mac >> 8) & 255), unsigned(mac & 255));
        if (!row->get("ev_mac")->updateStringChecked(mac_text)) { return; }
        row->get("namespace_clean")->updateBool(!p && current_mac == mac && ev.get_profile_mac(index) == mac &&
            presence == TeslaBleStorageAdapter::KeyPresence::Absent);
        row->get("ev_index")->updateInt(index); row->get("paired")->updateBool(paired);
        row->get("binding_fresh")->updateBool(p && fresh_binding(index, mac, p->get("vin")->asString().c_str(),
            p->get("ble_address")->asString().c_str(), p->get("ble_address_type")->asUint(), p->get("discovery_revision")->asUint()));
        const char *key_presence = presence == TeslaBleStorageAdapter::KeyPresence::Present ? "present" :
            presence == TeslaBleStorageAdapter::KeyPresence::Absent ? "absent" :
            presence == TeslaBleStorageAdapter::KeyPresence::CleanupRequired ? "cleanup" : "unknown";
        if (!row->get("key_presence")->updateStringChecked(key_presence)) { return; }
        // State is the selected profile's UI authority, even after its runtime
        // was retired or a legitimate mapping save invalidated profile_ready.
        // Reconcile only inside this serialized idle storage probe, never from
        // a guessed missing row or concurrently with worker key/auth writes.
        if (p && tesla->state.get("profile_ev_index")->asInt() == index) {
            tesla->state.get("key_presence")->updateString(key_presence);
            tesla->state.get("key_present")->updateBool(presence == TeslaBleStorageAdapter::KeyPresence::Present);
            tesla->state.get("paired")->updateBool(paired);
        }
    }
    tesla->guidance_refresh = false;
}
#endif
