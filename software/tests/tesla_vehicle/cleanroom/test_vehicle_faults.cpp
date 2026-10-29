// SPDX-License-Identifier: Apache-2.0
// Reuse the unchanged primitive calloc tracker, not any old implementation.
#define main retained_primitive_fault_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "test_faults.cpp"
#pragma GCC diagnostic pop
#undef main
#include "controller_fixture.hpp"
struct ControllerFaultHarness {
    controller_fixture::Harness oracle;
    controller_fixture::Rng rng;
    IdentityStore keys;
    uint32_t clock=100;
    std::array<uint8_t,16> address{};
    std::array<uint8_t,1026> tx{};
    size_t filled=0,count=0;
    std::array<Frame,16> sent{};
    std::array<VehicleResult,4> results{};
    size_t completions=0;
    Vehicle vehicle;
    static uint32_t now(void *p) { return static_cast<ControllerFaultHarness *>(p)->clock; }
    static bool owns(void *,uint64_t g) { return g==5; }
    static bool write(void *p,Bytes bytes,uint64_t g) {
        auto &h=*static_cast<ControllerFaultHarness *>(p); assert(g==5 && h.filled+bytes.size<=h.tx.size());
        std::memcpy(h.tx.data()+h.filled,bytes.data,bytes.size); h.filled+=bytes.size;
        if(h.filled>=2 && h.filled==size_t{h.tx[0]}*256+h.tx[1]+2) {
            assert(h.count<h.sent.size()); auto &f=h.sent[h.count++]; f.length=h.filled-2;
            std::memcpy(f.data.data(),h.tx.data()+2,f.length); h.filled=0;
        }
        return true;
    }
    VehiclePorts ports() { return {rng.get(),now,write,owns,this,20,nullptr}; }
    ControllerFaultHarness() : vehicle(keys,{vectors::vin,17},{address.data(),16},ports()) { controller_fixture::seed(keys,rng); assert(vehicle.open(5)); }
    void inject(const Frame &f) {
        std::array<uint8_t,1026> wire{}; wire[0]=static_cast<uint8_t>(f.length>>8); wire[1]=static_cast<uint8_t>(f.length);
        std::memcpy(wire.data()+2,f.data.data(),f.length); assert(vehicle.receive({wire.data(),f.length+2},5));
    }
    controller_fixture::Sent last() { assert(count); return controller_fixture::decode(sent[count-1]); }
    void start(bool infotainment) {
        VehicleCompletion cb; assert(cb.assign([this](const VehicleResult &result) { assert(completions<results.size()); results[completions++]=result; }));
        if(infotainment) { assert(vehicle.charge_state_poll(WakePolicy::NoWakeSkip,1,std::move(cb))); }
        else { assert(vehicle.status(1,std::move(cb))); }
        vehicle.tick();
        if(infotainment) {
            inject(oracle.hello(last())); vehicle.tick();
            inject(oracle.response(last(),{controller_fixture::awake,sizeof(controller_fixture::awake)})); vehicle.tick();
        }
        assert(last().hello);
    }
};
static size_t operation(unsigned op,size_t fault)
{
    ControllerFaultHarness h; const bool info=op>=3; const unsigned kind=op%3; h.start(info);
    Frame reply=h.oracle.hello(h.last());
    if(kind>=1) { h.inject(reply); }
    if(kind==2) {
        h.vehicle.tick(); auto s=h.last();
        reply=h.oracle.response(s,info?Bytes{controller_fixture::soc,sizeof(controller_fixture::soc)}:Bytes{controller_fixture::awake,sizeof(controller_fixture::awake)});
    }
    const size_t count=h.count; const uint32_t high=h.vehicle.high_water(info?Domain::Infotainment:Domain::Vcsec);
    attempts=hits=throwing_new=0; fail_at=fault; watching=true;
    if(kind==1) { h.vehicle.tick(); } else { h.inject(reply); }
    watching=false; const size_t boundaries=attempts; assert(throwing_new==0);
    if(fault) {
        assert(hits==1 && h.completions==0 && h.count==count && h.vehicle.high_water(info?Domain::Infotainment:Domain::Vcsec)==high);
        if(kind==1) { h.clock+=Vehicle::initial_retry_delay_ms; h.vehicle.tick(); } // first request backoff
        else { h.inject(reply); }
    }
    if(kind==0) { h.vehicle.tick(); }
    if(kind<=1) {
        assert(h.count==count+1 && !h.last().hello);
        h.inject(h.oracle.response(h.last(),info?Bytes{controller_fixture::soc,sizeof(controller_fixture::soc)}:Bytes{controller_fixture::awake,sizeof(controller_fixture::awake)}));
    }
    assert(h.completions==1 && h.results[0].authenticated && h.results[0].code==(info?VehicleResultCode::Soc:VehicleResultCode::Status));
    h.inject(reply); assert(h.completions==1); h.vehicle.close(5); return boundaries;
}
int main()
{
    size_t total=0;
    for(unsigned op=0;op<6;++op) {
        const size_t boundaries=operation(op,0);
        for(size_t fault=1;fault<=boundaries;++fault) { operation(op,fault); for(auto pointer:live) { assert(!pointer); } }
        total+=boundaries; std::printf("Controller fault operation %u: %zu deterministic calloc boundaries, no publication/counter mutation on refusal, same-object completion\n",op,boundaries);
    }
    std::printf("Controller allocation matrix: %zu deterministic calloc failures, both domains SessionInfo/outgoing/response composition, zero feature-owned throwing new and no tracked crypto leaks\n",total);
}
