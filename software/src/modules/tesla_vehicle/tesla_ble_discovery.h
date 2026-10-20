#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#else
#include <mutex>
#endif

// Production-only discovery policy: fixed storage, no protocol/storage/GATT
// dependencies. The existing lifecycle worker submits the returned GAP actions.
// No timer can enter Queued: request() is the sole explicit admission point.
class TeslaBleDiscovery {
public:
    static constexpr unsigned LIMIT = 8, SCAN_SECONDS = 10;
    static constexpr uint32_t RESULT_TTL_MS = 120000, WATCHDOG_MS = 20000;
    enum class Status { Idle, Searching, Complete, Error };
    enum class Action { None, Params, Start, Stop, Retire };
    enum class Event { Params, Start, Done, Stop };
    struct Scope { int index=-1; uint64_t mac=0; char name[19]{}; uint64_t generation=0; };
    struct Target { uint8_t address[6]{}; uint8_t address_type=0; int rssi=0; char name[19]{}; bool ambiguous=false; };
    struct Snapshot {
        Status status=Status::Idle;
        const char *error="";
        bool busy=false, truncated=false;
        unsigned count=0;
        uint32_t revision=0;
        Scope scope;
        Target targets[LIMIT]{};
    };
    bool request(uint32_t now) { return request(now, Scope{}); }
    bool request(uint32_t now, const Scope &scope) {
        Guard guard(*this);
        if (running.load() || sealed || phase!=Phase::Idle || revision>=UINT32_MAX-2) { return false; }
        view={};view.scope=scope;view.status=Status::Searching;view.revision=++revision;
        began.store(now);since=now;phase=Phase::Queued;running.store(true);return true;
    }
    bool busy() const { return running.load(); }
    bool overdue(uint32_t now) const {
        const auto elapsed=now-began.load();
        return running.load() && elapsed<(uint32_t{1}<<31) && elapsed>WATCHDOG_MS;
    }
    bool snapshot(Snapshot &out) {
        // Copy a bounded value snapshot under the same lock on host and ESP.
        // A try-lock made main-task publication nondeterministically disappear
        // under worker contention (even after observing scan completion).
        Guard guard(*this);
        out=view;out.busy=running.load();return true;
    }
    // Live check, not a copied snapshot. Re-scan invalidates immediately; TTL
    // does not depend on a later timer/publication pass.
    bool matches(uint32_t now, const Scope &scope, uint32_t wanted_revision,
                 const uint8_t *address, uint8_t type) {
        Guard guard(*this);
        if (!address || type>1 || !scope.name[0] || !scope.mac || scope.index<0 ||
            running.load() || view.truncated || view.status!=Status::Complete || now-since>=RESULT_TTL_MS ||
            !wanted_revision || view.revision!=wanted_revision || view.scope.index!=scope.index ||
            view.scope.mac!=scope.mac || view.scope.generation!=scope.generation ||
            std::strcmp(view.scope.name,scope.name)) { return false; }
        unsigned names=0;bool exact=false;
        for (unsigned i=0;i<view.count;++i) {
            const auto &t=view.targets[i];
            if (!std::strcmp(t.name,scope.name)) {
                ++names;
                exact=!t.ambiguous && t.address_type==type && !std::memcmp(t.address,address,6);
            }
        }
        return names==1 && exact;
    }
    void cancel(const char *reason="cancelled") { Guard guard(*this);if(running.load()) { fail(reason); } }
    void quiesced() {
        // Called only after the host's deinit barrier; failed deinit retains
        // busy+context forever. Late GAP callbacks can never target freed state.
        Guard guard(*this);running.store(false);view.count=0;view.truncated=false;
        for(auto &target:view.targets) { target={}; }
        if(view.status!=Status::Error) { view.status=Status::Idle; }
        phase=Phase::Idle;view.revision=++revision;
    }
    Action step(uint32_t now) {
        Guard guard(*this);
        if(!running.load()) {
            if(view.status==Status::Complete && now-since>=RESULT_TTL_MS) {
                view={};view.revision=++revision;
            }
            return Action::None;
        }
        if((phase==Phase::Params || phase==Phase::Starting) && now-since>2000) { fail("timeout"); }
        if(phase==Phase::Scanning && now-since>(SCAN_SECONDS*1000+2000)) { fail("timeout"); }
        if(phase==Phase::Queued) { phase=Phase::Params;since=now;return Action::Params; }
        if(phase==Phase::StartQueued) { phase=Phase::Starting;since=now;scan_may_run=true;return Action::Start; }
        if(phase==Phase::Finished) {
            phase=Phase::Idle;since=now;scan_may_run=false;running.store(false);
            view.status=Status::Complete;view.revision=++revision;return Action::None;
        }
        if(phase==Phase::Failed) {
            if(scan_may_run && !stop_sent) { stop_sent=true;since=now;phase=Phase::Stopping;return Action::Stop; }
            phase=Phase::Retired;return Action::Retire;
        }
        if(phase==Phase::Stopping && (stop_done || now-since>2000)) { phase=Phase::Retired;return Action::Retire; }
        if(phase==Phase::Retired) { return Action::Retire; }
        return Action::None;
    }
    void submitted(Action action,bool ok) {
        Guard guard(*this);
        if(!ok) {
            if(action==Action::Stop) { stop_done=true; }
            else { fail(action==Action::Params?"parameters":"start"); }
        }
    }
    void event(Event event,bool ok) {
        Guard guard(*this);
        if(event==Event::Stop && phase==Phase::Stopping) { stop_done=true;return; }
        if(sealed || !running.load()) { return; }
        if(event==Event::Params && phase==Phase::Params) { if(ok) { phase=Phase::StartQueued; } else { fail("parameters"); } }
        else if(event==Event::Start && phase==Phase::Starting) { if(ok) { phase=Phase::Scanning; } else { fail("start"); } }
        else if(event==Event::Done && phase==Phase::Scanning) { if(ok) { phase=Phase::Finished; } else { fail("scan"); } }
    }
    void observe(const uint8_t *data,size_t adv,size_t rsp,const uint8_t *address,uint8_t type,int rssi,unsigned event) {
        // Bluedroid supplies ADV || SCAN_RSP with the original ADV event type.
        // Only public/random connectable ADV and exact Tesla VCSEC name shape.
        if(!data || !address || adv>31 || rsp>31 || type>1 || event>1 || rssi>20 || rssi< -127) { return; }
        bool nonzero=false, nonff=false;for(unsigned i=0;i<6;++i) { nonzero|=address[i]!=0;nonff|=address[i]!=255; }
        if(!nonzero || !nonff) { return; }
        char name[19]{};
        const auto parse=[&](const uint8_t *p,size_t size) {
            for(size_t at=0;at<size;) {
                const size_t n=p[at];
                if(!n) { for(;at<size;++at) { if(p[at]) { return false; } }break; }
                if(n>size-at-1) { return false; }
                if(p[at+1]==8 || p[at+1]==9) {
                    if(p[at+1]==8 && n!=19) { at+=n+1;continue; }
                    const uint8_t *v=p+at+2;
                    if(n!=19 || v[0]!='S' || v[17]!='C') { return false; }
                    for(unsigned i=1;i<17;++i) { if(!((v[i]>='0' && v[i]<='9') || (v[i]>='a' && v[i]<='f'))) { return false; } }
                    if(name[0] && std::memcmp(name,v,18)) { return false; }
                    std::memcpy(name,v,18);
                }
                at+=n+1;
            }
            return true;
        };
        if(!parse(data,adv) || !parse(data+adv,rsp) || !name[0]) { return; }
        Guard guard(*this);
        if(sealed || (phase!=Phase::Starting && phase!=Phase::Scanning)) { return; }
        Target *target=nullptr;
        for(unsigned i=0;i<view.count;++i) {
            if(!std::memcmp(view.targets[i].address,address,6)) {
                // Address alone is the editable transport config. Ambiguous
                // advertising identity/type at the same address is never offered.
                if(view.targets[i].address_type!=type || std::strcmp(view.targets[i].name,name)) { view.targets[i].ambiguous=true;return; }
                target=&view.targets[i];break;
            }
        }
        if(!target) {
            if(view.count==LIMIT) { view.truncated=true;return; }
            target=&view.targets[view.count++];std::memcpy(target->address,address,6);target->address_type=type;std::memcpy(target->name,name,19);
        }
        target->rssi=rssi;
    }
private:
    enum class Phase { Idle, Queued, Params, StartQueued, Starting, Scanning, Finished, Failed, Stopping, Retired };
    Phase phase=Phase::Idle;
    Snapshot view;
    std::atomic<bool> running{false};
    std::atomic<uint32_t> began{0};
    uint32_t since=0,revision=0;
    bool sealed=false,scan_may_run=false,stop_sent=false,stop_done=false;
    void fail(const char *reason) {
        if(sealed) { return; }
        sealed=true;phase=Phase::Failed;view.status=Status::Error;view.error=reason;
        view.count=0;view.truncated=false;for(auto &target:view.targets) { target={}; }view.revision=++revision;
    }
#ifdef ESP_PLATFORM
    portMUX_TYPE mutex=portMUX_INITIALIZER_UNLOCKED;
#else
    std::mutex mutex;
#endif
    class Guard {
        TeslaBleDiscovery &owner;bool held=true;
    public:
        explicit Guard(TeslaBleDiscovery &owner,bool try_only=false):owner(owner) {
#ifdef ESP_PLATFORM
            (void)try_only;portENTER_CRITICAL(&owner.mutex);
#else
            if(try_only) { held=owner.mutex.try_lock(); } else { owner.mutex.lock(); }
#endif
        }
        ~Guard() { if(held) {
#ifdef ESP_PLATFORM
            portEXIT_CRITICAL(&owner.mutex);
#else
            owner.mutex.unlock();
#endif
        } }
        explicit operator bool() const { return held; }
    };
};
