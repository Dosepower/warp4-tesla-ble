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
    return profile_transport_record(vin,address,type,record) && tesla_ble_vehicle_name(vin,scope.name) &&
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
    // Stored authorization reuses the key, never an expired radio discovery.
    TeslaBleDiscovery::Scope scope;scope.index=r->configured_ev_index;scope.mac=r->profile_mac;
    scope.generation=identity->generation;
    return tesla_ble_vehicle_name(r->vin.c_str(),scope.name) &&
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
        if (p->get("enable")->asBool() && (!tesla_ble_valid_vin(vin.c_str()) || !tesla_ble_valid_address(address.c_str()))) {
            return checked_error_string("Valid 17-character VIN and BLE address required");
        }
        // Never silently replace a local identity when an index is reused or
        // the VIN changes. Even disabled mappings must retain their binding.
        auto *old = find_profile(index);
        if (source != ConfigSource::File && address.length()) {
            TeslaBleStorageAdapter store;
            const auto type=p->get("ble_address_type")->asUint();
            const bool saved=store.select_profile(index,mac) && profile_material_authorized(store,vin.c_str(),mac) &&
                profile_transport_matches(store,vin.c_str(),address.c_str(),type);
            if (!saved && !fresh_binding(index,mac,vin.c_str(),address.c_str(),type,p->get("discovery_revision")->asUint())) {
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
    tesla->state.get("key_presence")->updateString(r->profile_key_present.load() ? "present" : "unknown");
    tesla->state.get("pairing_pending")->updateBool(false);
    tesla->state.get("pair_request_sent")->updateBool(false);
    tesla->state.get("connected")->updateBool(false);
    publish_error("");
}
#endif
