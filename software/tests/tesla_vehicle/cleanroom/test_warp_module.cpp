// SPDX-License-Identifier: Apache-2.0
// Real WARP module + clean controller; only SDK/OS/Config/filesystem boundaries fake.

#include <cstdlib>
#include <new>
namespace ModuleAllocationFault {
static bool active=false;
static unsigned calls=0,fail_at=0,failures=0;
static void *live[64]{};
static unsigned count() { unsigned n=0; for(auto p:live) { n+=p!=nullptr; } return n; }
}
void *operator new(std::size_t n) { void *p=std::malloc(n?n:1); if(!p) { std::abort(); } return p; }
void *operator new[](std::size_t n) { return ::operator new(n); }
void *operator new(std::size_t n,const std::nothrow_t &) noexcept {
    using namespace ModuleAllocationFault;
    if(active && ++calls==fail_at) { ++failures; return nullptr; }
    void *p=std::malloc(n?n:1);
    if(active && p) { for(auto &slot:live) { if(!slot) { slot=p; return p; } } std::abort(); }
    return p;
}
void operator delete(void *p) noexcept { for(auto &slot:ModuleAllocationFault::live) { if(slot==p) { slot=nullptr; } } std::free(p); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete(void *p,std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p,std::size_t) noexcept { ::operator delete(p); }
void operator delete(void *p,const std::nothrow_t &) noexcept { ::operator delete(p); }
// Pause only the allowed WARP snapshot's coherence tag, as a host scheduling seam.
// No Tesla protocol implementation internals or legacy source are exposed.
#include "../../../src/modules/tesla_vehicle/tesla_ble_clean_platform.h"
#include <cmath>
#include <optional>
#define private public
#include "../../../src/modules/tesla_vehicle/tesla_ble_result.h"
#undef private
#include "../../../src/modules/tesla_vehicle/tesla_vehicle.cpp"
#include "../../../src/modules/tesla_vehicle/tesla_ble_storage_io.h"
#include "controller_fixture.hpp"
using namespace controller_fixture;
struct TeslaBleModuleTest {
    TeslaVehicle module;
    Harness peer;
    std::vector<uint8_t> prefix;
    std::vector<Sent> sent;
    unsigned closes=0;
    explicit TeslaBleModuleTest(uint64_t initial=0x100000007ULL) {
        module.pre_setup(); module.setup(); assert(!module.runtime_owner.get());
        module.config.get("enable")->updateBool(true);
        module.config.get("vin")->updateString("5YJ3E1EA7KF000001");
        module.config.get("ble_address")->updateString("01:02:03:04:05:06");
        module.config.get("ev_index")->updateInt(ev.index);
        assert(module.start_runtime()); auto *r=module.runtime_owner.get();
        assert(r->vin.assign("5YJ3E1EA7KF000001"));
        // Use the public fixture identity (scalar=1); no secrets read or emitted.
        IdentityLease key; assert(peer.keys.acquire(key)); uint8_t pem[512]{}; size_t n=0;
        assert(key.key()->export_pem(pem,sizeof(pem),n));
        LittleFS.files["/config/tesla_ble_private_key"]={pem,pem+n};
        assert(module.create_vehicle()); peer.address=r->routing_address;
        r->observed_identity=*r->session.identity();
        r->policy.on_vehicle_changed(ev.generation,ev.index);
        r->bound_vehicle_mac.store(ev.mac); r->session.bind(ev.mac);
        ready(initial);
        TeslaVehicle::gatt_owner.store(&module);
        gatt_write_result=ESP_OK; gatt_close_result=ESP_OK;
        on_gatt_bytes=[this](const uint8_t *b,size_t n) {
            prefix.insert(prefix.end(),b,b+n);
            if(prefix.size()<2) { return; }
            size_t length=size_t{prefix[0]}*256+prefix[1];
            assert(prefix.size()<=length+2);
            if(prefix.size()==length+2) {
                Frame f; f.length=length; std::memcpy(f.data.data(),prefix.data()+2,length);
                sent.push_back(decode(f)); prefix.clear();
            }
        };
        on_gatt_write=[this]() { auto *r=module.runtime_owner.get(); r->operation.complete(TeslaVehicle::Write,4,10,true); };
        on_gatt_close=[this]() { ++closes; auto *r=module.runtime_owner.get(); r->retirement_operation.complete(TeslaVehicle::Close,4,0,true); prefix.clear(); };
    }
    ~TeslaBleModuleTest() {
        on_gatt_bytes={}; on_gatt_write={}; on_gatt_close={}; TeslaVehicle::gatt_owner.store(nullptr);
        tasks.clear();
    }
    void ready(uint64_t generation) {
        auto *r=module.runtime_owner.get(); r->connection_generation.store(generation);
        r->physical_generation.store(generation);
        r->gatt_if.store(1); r->gatt_connection.store(4); r->write_handle=10; r->notify_handle.store(20);
        r->connected.store(true); r->link_open.store(true); r->session.connect(); assert(r->vehicle->open(generation));
    }
    void inject(const Frame &frame) {
        auto *r=module.runtime_owner.get(); std::vector<uint8_t> bytes{uint8_t(frame.length>>8),uint8_t(frame.length)};
        bytes.insert(bytes.end(),frame.data.begin(),frame.data.begin()+frame.length);
        for(size_t offset=0;offset<bytes.size();offset+=7) {
            esp_ble_gattc_cb_param_t p; p.notify.conn_id=4; p.notify.handle=20;
            std::memcpy(p.notify.remote_bda,r->remote_address,6);
            p.notify.value=bytes.data()+offset; p.notify.value_len=std::min<size_t>(7,bytes.size()-offset);
            TeslaVehicle::gatt_callback(ESP_GATTC_NOTIFY_EVT,1,&p); module.process_rx();
        }
    }
    Sent last() { assert(!sent.empty()); return sent.back(); }
    void tick() { module.runtime_owner.get()->vehicle->tick(); }
    void pairing(bool pressure=false) {
        auto *r=module.runtime_owner.get(); r->pair_requested.store(true); module.process_commands();
        assert(!module.state.get("paired")->asBool()); tick(); assert(last().pair);
        module.process_module_events(); assert(module.state.get("pairing_pending")->asBool());
        assert(!module.state.get("paired")->asBool());
        // A raw status OK is not proof of physical approval or key possession.
        Frame raw; raw.length=4; raw.data[0]=0x22; raw.data[1]=2; raw.data[2]=0x08; raw.data[3]=0;
        inject(raw); module.process_module_events(); assert(!module.state.get("paired")->asBool());
        clock_ms.fetch_add(1000); module.process_pair_verification(); tick();
        assert(last().hello); // Behavioral RED: no verification request yet.
        inject(peer.hello(last(),true)); module.process_module_events();
        assert(!module.state.get("paired")->asBool() && module.state.get("pairing_pending")->asBool());
        clock_ms.fetch_add(1000); module.process_pair_verification(); tick(); assert(last().hello);
        inject(peer.hello(last(),false,7,1234,true)); tick(); assert(last().hello);
        inject(peer.hello(last())); tick(); auto status=last(); assert(!status.hello);
        inject(peer.response(status,{awake,sizeof(awake)},1,0,true));
        module.process_module_events(); assert(!module.state.get("paired")->asBool());
        if (pressure) { r->queues.events()->capacity=0; }
        inject(peer.response(status,{awake,sizeof(awake)})); module.process_module_events();
        assert(module.state.get("paired")->asBool() && !module.state.get("pairing_pending")->asBool());
        assert(ev.applied==0); // Status approval has no charging/SoC side effect.
    }
    void soc(bool stale=false) {
        auto *r=module.runtime_owner.get(); module.request_soc(false); tick();
        auto vc=last(); if(vc.hello) { inject(peer.hello(vc)); tick(); vc=last(); }
        inject(peer.response(vc,{awake,sizeof(awake)},2)); tick();
        assert(last().hello && last().domain==Domain::Infotainment); inject(peer.hello(last())); tick();
        auto charge=last(); inject(peer.response(charge,{soc_bytes(),sizeof(controller_fixture::soc)},1,0,true));
        module.process_module_events(); assert(ev.applied==0);
        Frame good=peer.response(charge,{soc_bytes(),sizeof(controller_fixture::soc)});
        inject(good);
        if(stale) { ev.generation+=uint64_t{1}<<32; module.loop(); }
        module.process_module_events(); assert(ev.applied==(stale?0:1));
        inject(good); module.process_module_events(); assert(ev.applied==(stale?0:1));
        assert(r->in_flight_started_ms==0);
    }
    static const uint8_t *soc_bytes() { return controller_fixture::soc; }
    void retirement() {
        auto *r=module.runtime_owner.get(); module.request_soc(false);
        on_gatt_write={}; gatt_write_result=-1; tick();
        assert(closes==1 && !r->link_open.load() && !r->connected.load()); // Behavioral retirement RED.
        assert(!r->vehicle->connected() && r->connection_generation.load()>0x100000007ULL);
    }
    void generation_refusal() {
        auto *r=module.runtime_owner.get(); module.request_soc(false);
        unsigned chunks=0;
        on_gatt_bytes=[&](const uint8_t *,size_t) {
            if (++chunks==1) { r->connection_generation.fetch_add(uint64_t{1}<<32); }
        };
        tick();
        assert(chunks==1 && closes==1 && !r->link_open.load() && !r->vehicle->connected());
    }
    void overflow() {
        auto *r=module.runtime_owner.get(); uint8_t byte=0;
        esp_ble_gattc_cb_param_t p; p.notify.conn_id=4; p.notify.handle=20;
        std::memcpy(p.notify.remote_bda,r->remote_address,6); p.notify.value=&byte; p.notify.value_len=1;
        for (unsigned i=0;i<9;++i) { TeslaVehicle::gatt_callback(ESP_GATTC_NOTIFY_EVT,1,&p); }
        module.process_disconnect();
        assert(closes==1 && !r->link_open.load() && !r->vehicle->connected());
    }
    void lifecycle() {
        auto *r=module.runtime_owner.get(); const auto old_generation=r->connection_generation.load();
        std::array<unsigned,4> completions{}; unsigned fresh=0;
        for(unsigned i=0;i<4;++i) {
            VehicleCompletion cb; assert(cb.assign([&,i](const VehicleResult &v) {
                assert(v.code==VehicleResultCode::Disconnected && v.generation==old_generation); ++completions[i];
                if(i==0) {
                    ready(r->connection_generation.load());
                    VehicleCompletion successor; assert(successor.assign([&](const VehicleResult &v) { assert(v.authenticated); ++fresh; }));
                    assert(r->vehicle->status(99,std::move(successor))); tick();
                    auto hello=peer.hello(last()); uint8_t first=hello.length>>8;
                    assert(r->vehicle->receive({&first,1},r->connection_generation.load()));
                }
            }));
            assert(r->vehicle->status(i+1,std::move(cb)));
        }
        VehicleCompletion refused; assert(refused.assign([](const VehicleResult &) { assert(false); }));
        assert(!r->vehicle->status(5,std::move(refused)));
        module.disconnect_internal();
        for(auto count:completions) { assert(count==1); }
        assert(closes==1 && r->vehicle->queued()==1 && fresh==0);
        // Deliver the rest via the real module RX queue, preserving callback RX.
        auto hello=peer.hello(last()); std::vector<uint8_t> rest{uint8_t(hello.length)};
        rest.insert(rest.end(),hello.data.begin(),hello.data.begin()+hello.length);
        TeslaVehicle::RxFragment chunk; chunk.connection_generation=r->connection_generation.load();
        chunk.length=rest.size(); std::memcpy(chunk.data,rest.data(),rest.size());
        assert(xQueueSend(r->queues.rx(),&chunk,0)==pdTRUE); module.process_rx(); tick();
        inject(peer.response(last(),{awake,sizeof(awake)})); assert(fresh==1);
        chunk.connection_generation=old_generation; assert(xQueueSend(r->queues.rx(),&chunk,0)==pdTRUE);
        module.process_rx(); assert(fresh==1);
    }
    void teardown() {
        auto *r=module.runtime_owner.get();
        // Drive the REAL worker startup, runtime-disable ACK, host shutdown and
        // main-side RAII teardown. No physical SDK/network exists in this test.
        on_delay=[&]() { r->stop_requested.store(true); };
        run_task(r->task_handle); on_delay={};
        assert(r->worker_stopped.load()); module.config.get("enable")->updateBool(false);
        module.loop(); assert(r->teardown_handle);
        run_task(r->teardown_handle); module.loop();
        assert(!module.runtime_owner.get() && queues_live==0);
        assert(!module.state.get("running")->asBool() && module.state.get("restart_required")->asBool());
        assert(std::find(cleanup_calls.begin(),cleanup_calls.end(),"host-disable")!=cleanup_calls.end());
    }
    void no_wake() {
        auto *r=module.runtime_owner.get(); module.request_soc(false); tick();
        assert(last().hello); inject(peer.hello(last())); tick();
        inject(peer.response(last(),{asleep,sizeof(asleep)})); tick(); module.process_module_events();
        const auto initial=sent.size(); assert(ev.applied==0 && r->in_flight_started_ms==0);
        assert(std::all_of(sent.begin(),sent.end(),[](const Sent &s) { return s.domain==Domain::Vcsec; }));
        module.request_soc(true); tick(); inject(peer.response(last(),{asleep,sizeof(asleep)},2)); tick();
        VcsecPacket expected; assert(wake_payload(true,expected));
        assert(last().plaintext.size()==expected.length && !std::memcmp(last().plaintext.data(),expected.bytes.data(),expected.length));
        inject(peer.response(last(),{awake,sizeof(awake)},3)); tick();
        assert(last().hello && last().domain==Domain::Infotainment);
        inject(peer.hello(last())); tick(); inject(peer.response(last(),{soc_bytes(),sizeof(controller_fixture::soc)}));
        module.process_module_events(); assert(ev.applied==1);
        module.request_soc(true); tick(); inject(peer.response(last(),{asleep,sizeof(asleep)},4)); tick();
        assert(r->in_flight_started_ms==0);
        unsigned wakes=0;
        for(const auto &s:sent) { wakes+=s.plaintext.size()==expected.length && !std::memcmp(s.plaintext.data(),expected.bytes.data(),expected.length); }
        assert(wakes==1 && sent.size()>initial && ev.applied==1);
    }
    void simultaneous_commands() {
        auto *r=module.runtime_owner.get();
        r->pair_requested.store(true); r->poll_requested.store(true); module.process_commands();
        assert(r->vehicle->queued()==1 && r->poll_requested.load());
        tick(); module.process_module_events();
        assert(last().pair && r->pair_verifying && !module.state.get("paired")->asBool());
    }
    void boundary_schedule(Domain domain,bool deferred,bool denied,bool nested) {
        auto *r=module.runtime_owner.get(); const auto initial=r->connection_generation.load();
        const auto old_session=r->session.connection(); unsigned retired=0,middle=0,success=0;
        std::array<unsigned,4> counts{}; std::vector<uint8_t> fresh_wire;
        auto enqueue=[&](uint64_t id,VehicleCompletion &&cb) {
            return domain==Domain::Vcsec?r->vehicle->status(id,std::move(cb)):
                r->vehicle->charge_state_poll(WakePolicy::NoWakeSkip,id,std::move(cb));
        };
        auto disconnect=[&](bool sdk) {
            if(sdk) {
                esp_ble_gattc_cb_param_t p; p.disconnect.conn_id=4;
                TeslaVehicle::gatt_callback(ESP_GATTC_DISCONNECT_EVT,1,&p); module.process_disconnect();
            } else { module.disconnect_internal(); }
        };
        auto successor=[&]() {
            ready(r->connection_generation.load());
            VehicleCompletion cb; assert(cb.assign([&](const VehicleResult &v) { assert(v.authenticated); ++success; }));
            assert(enqueue(99,std::move(cb))); tick(); assert(last().hello);
            auto reply=peer.hello(last()); fresh_wire={uint8_t(reply.length>>8),uint8_t(reply.length)};
            fresh_wire.insert(fresh_wire.end(),reply.data.begin(),reply.data.begin()+reply.length);
            esp_ble_gattc_cb_param_t p; p.notify.conn_id=4; p.notify.handle=20;
            std::memcpy(p.notify.remote_bda,r->remote_address,6); p.notify.value=fresh_wire.data(); p.notify.value_len=1;
            TeslaVehicle::gatt_callback(ESP_GATTC_NOTIFY_EVT,1,&p);
            r->pending_disconnect.store(old_session); module.process_disconnect();
            assert(r->connected.load() && r->queues.rx()->data.size()==1);
        };
        for(unsigned i=0;i<4;++i) {
            VehicleCompletion cb; assert(cb.assign([&,i](const VehicleResult &v) {
                assert(v.generation==initial && ++counts[i]==1); ++retired;
                assert(v.code==(denied && i==0?VehicleResultCode::KeyDenied:VehicleResultCode::Disconnected));
                if(denied && i==0) { disconnect(deferred); }
                if(i==1) {
                    if(nested) {
                        ready(r->connection_generation.load());
                        VehicleCompletion mid; assert(mid.assign([&](const VehicleResult &x) {
                            assert(x.code==VehicleResultCode::Disconnected); ++middle; successor();
                        }));
                        assert(enqueue(98,std::move(mid))); tick(); disconnect(!deferred);
                    } else { successor(); }
                }
                tick();
            }));
            assert(enqueue(i+1,std::move(cb)));
        }
        tick();
        if(domain==Domain::Infotainment) {
            inject(peer.hello(last())); tick(); inject(peer.response(last(),{awake,sizeof(awake)},40)); tick();
        }
        const auto previous=peer.hello(last(),denied);
        if(denied) { inject(peer.hello(last(),true,7,1234,true)); assert(!retired); inject(previous); }
        else { disconnect(deferred); }
        assert(retired==4 && middle==unsigned(nested) && success==0 && r->vehicle->queued()==1);
        module.process_rx();
        TeslaVehicle::RxFragment fragment; fragment.connection_generation=r->connection_generation.load()-(uint64_t{1}<<32);
        fragment.length=1; fragment.data[0]=255;
        assert(xQueueSend(r->queues.rx(),&fragment,0)==pdTRUE); module.process_rx(); module.process_disconnect();
        fragment.connection_generation=r->connection_generation.load(); fragment.length=fresh_wire.size()-1;
        std::memcpy(fragment.data,fresh_wire.data()+1,fragment.length);
        assert(xQueueSend(r->queues.rx(),&fragment,0)==pdTRUE); module.process_rx(); tick();
        inject(previous); assert(!success);
        inject(peer.response(last(),{awake,sizeof(awake)},100)); tick();
        if(domain==Domain::Infotainment) { inject(peer.hello(last())); tick(); inject(peer.response(last(),{soc_bytes(),sizeof(controller_fixture::soc)},100)); }
        assert(success==1 && retired==4 && r->vehicle->queued()==0);
    }
    void wrap_refusal() {
        auto *r=module.runtime_owner.get(); assert(r->connection_generation.load()==UINT64_MAX);
        unsigned done=0; VehicleCompletion cb; assert(cb.assign([&](const VehicleResult &v) {
            assert(v.code==VehicleResultCode::Disconnected); ++done;
        }));
        assert(r->vehicle->status(1,std::move(cb))); module.disconnect_internal();
        assert(done==1 && r->stop_requested.load() && !r->vehicle->connected());
        assert(!r->vehicle->open(0) && !module.transport_owns(0));
    }
    void text_retry() {
        module.publish_error("retained diagnostic"); fail_checked_string=true;
        module.publish_error("first deferred diagnostic"); module.publish_error("latest deferred diagnostic");
        assert(module.state.get("last_error")->asString()=="retained diagnostic" && module.error_pending);
        fail_checked_string=false; module.flush_error();
        assert(module.state.get("last_error")->asString()=="latest deferred diagnostic" && !module.error_pending);
        fail_checked_string=true;
        auto *r=module.runtime_owner.get();
        on_delay=[&]() { module.post_event(TeslaVehicle::EventType::Error,"post teardown diagnostic"); r->stop_requested.store(true); };
        run_task(r->task_handle); on_delay={}; module.config.get("enable")->updateBool(false); module.loop();
        assert(r->teardown_handle); run_task(r->teardown_handle); module.loop();
        assert(!module.runtime_owner.get());
        fail_checked_string=false; module.flush_error();
        assert(module.state.get("last_error")->asString()=="post teardown diagnostic");
    }
    static void allocations() {
        using namespace ModuleAllocationFault;
        unsigned startup=0,protocol=0;
        for(unsigned fault=0;;++fault) {
            assert(!count());
            {
                TeslaVehicle owner; owner.pre_setup(); owner.setup();
                owner.config.get("enable")->updateBool(true); owner.config.get("vin")->updateString("5YJ3E1EA7KF000001");
                owner.config.get("ble_address")->updateString("01:02:03:04:05:06");
                calls=failures=0; fail_at=fault; active=true; const bool ok=owner.start_runtime(); active=false;
                if(!fault) { startup=calls; assert(ok && startup>=4); }
                else { assert(failures==1 && !ok && !owner.runtime_owner.get() && queues_live==0); assert(owner.start_runtime()); }
                assert(!owner.runtime_owner.restart_required()); tasks.clear();
            }
            assert(!count() && queues_live==0); if(fault==startup) { break; }
        }
        for(unsigned fault=0;;++fault) {
            assert(!count());
            {
                TeslaBleModuleTest t; auto *r=t.module.runtime_owner.get(); const auto before=LittleFS.files;
                calls=failures=0; fail_at=fault; active=true; const bool ok=t.module.create_vehicle(); active=false;
                if(!fault) { protocol=calls; assert(ok && protocol>1); }
                else { assert(failures==1 && !ok && !r->vehicle); assert(t.module.create_vehicle()); }
                assert(LittleFS.files==before);
            }
            assert(!count() && queues_live==0); if(fault==protocol) { break; }
        }
        std::printf("Independent WARP allocation matrix: %u runtime/synchronization and %u protocol nothrow faults; no published failed runtime/vehicle, resource balance, unchanged storage and same-owner retry passed\n",startup,protocol);
    }
    #include "warp_module_contracts.inc"
    void forget() {
        auto *r=module.runtime_owner.get(); r->forget_requested.store(true); module.process_commands();
        assert(r->vehicle && !r->vehicle->connected() && r->bound_vehicle_mac.load()==0);
        assert(r->storage->private_key_presence()==TeslaBleStorageAdapter::KeyPresence::Absent);
        module.process_module_events(); assert(!module.state.get("key_present")->asBool());
        IdentityLease absent; assert(!r->keys->acquire(absent));
        ready(r->connection_generation.load());
        r->pair_requested.store(true); module.process_commands(); tick();
        assert(last().pair && r->storage->private_key_presence()==TeslaBleStorageAdapter::KeyPresence::Present);
    }
};
int main(int argc,char **argv) {
    assert(argc==2);
    if(std::strcmp(argv[1],"boundary-matrix")==0) {
        unsigned cases=0;
        for(uint64_t initial:{uint64_t{5},uint64_t{UINT32_MAX}-2,uint64_t{UINT32_MAX}-1,uint64_t{UINT32_MAX},uint64_t{UINT32_MAX}+1})
            for(auto domain:{Domain::Vcsec,Domain::Infotainment}) for(bool deferred:{false,true})
                for(bool denied:{false,true}) for(bool nested:{false,true}) {
                    TeslaBleModuleTest t(initial); t.boundary_schedule(domain,deferred,denied,nested); ++cases;
                }
        { TeslaBleModuleTest t(UINT64_MAX-1); t.boundary_schedule(Domain::Vcsec,false,false,false); ++cases; }
        { TeslaBleModuleTest t(UINT64_MAX); t.wrap_refusal(); ++cases; }
        std::printf("Independent R9 WARP boundary: %u full-width direct/deferred denial/nested schedules, stale cleanup/RX, successor authentication and terminal wrap refusal passed\n",cases);
        return 0;
    }
    if(std::strcmp(argv[1],"allocations")==0) { TeslaBleModuleTest::allocations(); return 0; }
    if(std::strcmp(argv[1],"composition")==0) { TeslaBleModuleTest::composition(); return 0; }
    if(std::strcmp(argv[1],"chronology")==0) { TeslaBleModuleTest::chronology(); return 0; }
    TeslaBleModuleTest t;
    if(std::strcmp(argv[1],"pair")==0) { t.pairing(); t.soc(); }
    else if(std::strcmp(argv[1],"retire")==0) { t.retirement(); }
    else if(std::strcmp(argv[1],"pressure")==0) { t.pairing(true); }
    else if(std::strcmp(argv[1],"generation")==0) { t.generation_refusal(); }
    else if(std::strcmp(argv[1],"overflow")==0) { t.overflow(); }
    else if(std::strcmp(argv[1],"lifecycle")==0) { t.lifecycle(); }
    else if(std::strcmp(argv[1],"teardown")==0) { t.teardown(); }
    else if(std::strcmp(argv[1],"no-wake")==0) { t.no_wake(); }
    else if(std::strcmp(argv[1],"stale-soc")==0) { t.soc(true); }
    else if(std::strcmp(argv[1],"simultaneous")==0) { t.simultaneous_commands(); }
    else if(std::strcmp(argv[1],"acceptance")==0) { t.acceptance(); }
    else if(std::strcmp(argv[1],"identity-busy")==0) { t.identity_busy(); }
    else if(std::strcmp(argv[1],"storage-quarantine")==0) { t.storage_quarantine(); }
    else if(std::strcmp(argv[1],"forget")==0) { t.forget(); }
    else if(std::strcmp(argv[1],"text")==0) { t.text_retry(); }
    else { assert(false); }
}
