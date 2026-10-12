// SPDX-License-Identifier: Apache-2.0
#define main preserved_replay_confirmation_main
#include "astra/protocol_probe_confirm.cpp"
#undef main
static Request request_for(const Sent &s,uint64_t context)
{
    Request r; r.uuid=s.uuid; r.identifier_length=17; std::copy(s.identifier.begin(),s.identifier.end(),r.identifier.begin());
    r.context=context; r.generation=0x100000005ULL; r.started=UINT32_MAX-100; r.transmitted=10;
    r.total_ms=2000; r.response_ms=500; return r;
}
static Response response_for(Harness &h,const Sent &s,Bytes payload)
{
    auto f=reply(h,s,payload,1); Response out;
    assert(decode_encrypted_response({f.data.data(),f.length},{h.address.data(),16},out)); return out;
}
int main()
{
    Harness h; SessionCrypto crypto; assert(crypto.set_key({vectors::session_key,16}));
    for(Domain domain:{Domain::Vcsec,Domain::Infotainment}) {
        ResponseGuard guard(crypto,domain,{vectors::vin,17}); std::array<uint8_t,1024> clear{}; VerifiedReply verified;
        Sent s; s.domain=domain; s.uuid.fill(4); s.identifier.fill(7); s.identifier[0]=5;
        const auto old=response_for(h,s,{soc,sizeof(soc)}); auto request=request_for(s,1);
        assert(guard.arm(request) && guard.accept(old,20,request.generation,clear,verified));
        assert(!guard.accept(old,20,request.generation,clear,verified));
        ++request.context; assert(guard.arm(request));
        assert(!guard.accept(old,20,request.generation,clear,verified)); guard.cancel();
        // A fresh UUID/hash may reuse its independently authenticated first
        // counter. None of the old packet, old hash or low-word alias is enough.
        s.uuid[0]^=1; s.identifier[1]^=1; auto fresh=response_for(h,s,{soc,sizeof(soc)}); request=request_for(s,3);
        assert(guard.arm(request)); auto changed_uuid=old; changed_uuid.uuid=s.uuid;
        assert(!guard.accept(old,20,request.generation,clear,verified));
        assert(!guard.accept(changed_uuid,20,request.generation,clear,verified));
        assert(!guard.accept(fresh,20,5,clear,verified));
        assert(!guard.accept(fresh,510,request.generation,clear,verified));
        auto bad=fresh; bad.packet.tag[0]^=1;
        assert(!guard.accept(bad,20,request.generation,clear,verified));
        assert(guard.accept(fresh,20,request.generation,clear,verified) && verified.counter==1 && verified.context==3);
        // Rearm/cancel with the SAME UUID/hash does not erase replay state.
        request.context=4; assert(guard.arm(request)); assert(!guard.accept(fresh,20,request.generation,clear,verified)); guard.cancel();
        // Same UUID but a separately authenticated request hash has its own scope.
        s.identifier[2]^=1; request=request_for(s,5); fresh=response_for(h,s,{soc,sizeof(soc)});
        assert(guard.arm(request) && guard.accept(fresh,20,request.generation,clear,verified));
    }
    // The independent sibling admission consumer must inherit the shared fix.
    ChargeStateAdmission sibling(crypto,{vectors::vin,17}); AuthenticatedSoc output;
    Sent s; s.domain=Domain::Infotainment; s.uuid.fill(6); s.identifier.fill(9); s.identifier[0]=5;
    auto request=request_for(s,1); auto old=response_for(h,s,{soc,sizeof(soc)});
    assert(sibling.arm(request) && sibling.accept(old,20,request.generation,output) && output.battery==72);
    assert(sibling.arm(request) && !sibling.accept(old,20,request.generation,output)); sibling.cancel();
    s.uuid[0]^=1; s.identifier[1]^=1; request=request_for(s,2); auto fresh=response_for(h,s,{soc,sizeof(soc)});
    assert(sibling.arm(request)); assert(!sibling.accept(old,20,request.generation,output));
    assert(sibling.accept(fresh,20,request.generation,output) && output.counter==1 && output.context==2);
    std::puts("Request replay scope: both domains/fresh UUID and hash reuse counter 1, same-request rearm remains replay, old hash/GCM/UUID/deadline/64-bit alias negatives, sibling ChargeStateAdmission passed");
    return 0;
}
