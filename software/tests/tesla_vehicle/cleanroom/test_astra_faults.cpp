// SPDX-License-Identifier: Apache-2.0
// Checked allocator tracker from current Clean primitives only.
#define main preserved_primitive_fault_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "test_faults.cpp"
#pragma GCC diagnostic pop
#undef main
#define main preserved_astra_probe_main
#include "astra/protocol_probe_confirm.cpp"
#undef main
static void fixed_inject(controller_fixture::Harness &h,const Frame &frame)
{
    std::array<uint8_t,1026> bytes{}; bytes[0]=static_cast<uint8_t>(frame.length>>8); bytes[1]=static_cast<uint8_t>(frame.length);
    std::memcpy(bytes.data()+2,frame.data.data(),frame.length);
    assert(h.vehicle.receive({bytes.data(),frame.length+2},h.link));
}
static size_t fault_case(bool info,bool synchronization,size_t fault)
{
    controller_fixture::Harness h; std::vector<VehicleResult> out; out.reserve(4);
    assert(h.vehicle.open(h.link)); start(h,out,info,1);
    const Bytes payload=info?Bytes{soc,sizeof(soc)}:Bytes{awake,sizeof(awake)};
    if(!synchronization) {
        h.inject(reply(h,h.last(),payload,1)); assert(out.size()==1); start(h,out,info,2);
    }
    const auto request=h.last(); const auto high=h.vehicle.high_water(request.domain);
    const auto other=h.vehicle.high_water(info?Domain::Vcsec:Domain::Infotainment);
    const size_t completed=out.size();
    const Frame frame=synchronization?synchronization_error(h,request):reply(h,request,payload,1);
    attempts=hits=throwing_new=0; fail_at=fault; watching=true;
    fixed_inject(h,frame);
    watching=false; const size_t boundaries=attempts;
    assert(throwing_new==0);
    if(fault) {
        assert(hits==1 && out.size()==completed && h.vehicle.high_water(request.domain)==high &&
            h.vehicle.high_water(info?Domain::Vcsec:Domain::Infotainment)==other);
        // SAME request and object still admit the healthy update/reused counter.
        fixed_inject(h,frame);
    }
    if(synchronization) {
        assert(out.size()==completed && h.vehicle.high_water(request.domain)==100);
        // Accepted synchronization is consumed; re-injection cannot finish or
        // reset the 100ms backoff, nor allocate/persist a replacement identity.
        fixed_inject(h,frame); h.clock+=100; h.vehicle.tick();
        assert(h.last().counter==101); h.inject(reply(h,h.last(),payload,1));
    }
    assert(out.size()==completed+1 && out.back().authenticated &&
        out.back().code==(info?VehicleResultCode::Soc:VehicleResultCode::Status));
    fixed_inject(h,frame); assert(out.size()==completed+1); h.vehicle.close(h.link);
    return boundaries;
}
int main()
{
    size_t total=0;
    for(bool info:{false,true}) { for(bool synchronization:{false,true}) {
        const auto boundaries=fault_case(info,synchronization,0); assert(boundaries>0);
        for(size_t fault=1;fault<=boundaries;++fault) {
            fault_case(info,synchronization,fault); for(auto p:live) { assert(!p); }
        }
        total+=boundaries;
        std::printf("Astra fix allocator case info=%d synchronization=%d: %zu calloc failures, no state/publication on refusal, same-object admission, no throwing new\n",info,synchronization,boundaries);
    } }
    std::printf("Astra fix fault matrix: %zu deterministic injected calloc failures, both domains/request replay and command SessionInfo, balanced crypto ownership\n",total);
    return 0;
}
