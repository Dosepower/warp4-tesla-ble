// SPDX-License-Identifier: Apache-2.0
// Retain the clean primitive fault tracker, deterministic PUBLIC RNG and assertions.
#define main retained_primitive_fault_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "test_faults.cpp"
#pragma GCC diagnostic pop
#undef main
#include <tesla_clean/authentication.hpp>
#include <tesla_clean/identity.hpp>
#include <tesla_clean/messages.hpp>
#include "wire_vectors.hpp"
struct Disk : KeyStorage {
    unsigned calls=0;
    SaveResult save(Bytes data) override { assert(data.valid() && data.size>0); ++calls; return SaveResult::Committed; }
};
static bool send(void *,const ChallengeView &) { return true; }
static int rng(void *p,unsigned char *out,size_t n) {
    if(n==16) { std::memcpy(out,vectors::challenge,n); return 0; }
    return mbedtls_ctr_drbg_random(static_cast<mbedtls_ctr_drbg_context *>(p),out,n);
}
static size_t run_operation(unsigned operation,size_t fault)
{
    Rng random; const Random source{rng,&random.drbg};
    P256Key seed; std::array<uint8_t,32> scalar{}; scalar[31]=1;
    assert(seed.import_scalar({scalar.data(),32},source));
    std::array<uint8_t,512> pem{}; size_t length=0; assert(seed.export_pem(pem.data(),pem.size(),length));
    IdentityStore keys; assert(keys.import_pem({pem.data(),length},source));
    IdentityLease before,after; assert(keys.acquire(before)); std::array<uint8_t,65> initial{},current{}; assert(before.public_point(initial));
    Disk disk; DomainAuthentication auth(Domain::Infotainment,{vectors::vin,17});
    SessionReply reply{Domain::Infotainment,1,{}, {vectors::encoded_info,sizeof(vectors::encoded_info)},{vectors::info_tag,32}};
    std::memcpy(reply.uuid.data(),vectors::challenge,16);
    if(operation>=2) { assert(auth.begin(keys,{1,1,0,1000,500},0,source,{send,nullptr})==ChallengeResult::Sent); }
    if(operation==3) {
        assert(auth.receive(reply,1,source)==SessionAdmission::Established);
        // Refresh needs a new UUID: compute its correct HMAC before allocation tracking.
        const auto alternate=[](void *p,unsigned char *out,size_t n) {
            if(n==16) { for(size_t i=0;i<n;++i) { out[i]=static_cast<unsigned char>(i+1); } return 0; }
            return mbedtls_ctr_drbg_random(static_cast<mbedtls_ctr_drbg_context *>(p),out,n);
        };
        assert(auth.begin(keys,{2,1,100,1000,500},100,{alternate,&random.drbg},{send,nullptr})==ChallengeResult::Sent);
        for(size_t i=0;i<16;++i) { reply.uuid[i]=static_cast<uint8_t>(i+1); }
    }
    std::array<uint8_t,32> updated_tag{};
    if(operation==3) {
        SessionCrypto crypto; assert(crypto.set_key({vectors::session_key,16}));
        assert(crypto.session_info_tag({vectors::vin,17},{reply.uuid.data(),16},reply.encoded,updated_tag)); reply.tag={updated_tag.data(),32};
    }
    const auto revision=keys.revision(); attempts=hits=throwing_new=0; fail_at=fault;
    SessionCrypto outgoing_crypto; assert(outgoing_crypto.set_key({wire_vectors::key,16}));
    SessionInfo session; session.counter=7; session.clock=1234;
    std::memcpy(session.epoch.data(),wire_vectors::epoch,16);
    OutgoingMessages outgoing(outgoing_crypto,session,{vectors::vin,17},{wire_vectors::public_point,65});
    Route route; route.domain=Domain::Infotainment; EncodedRequest encoded;
    watching=true;
    bool ok=false;
    switch(operation) {
    case 0: ok=keys.import_pem({pem.data(),length},source); break;
    case 1: ok=keys.rotate(disk,source).committed; break;
    case 2: ok=auth.receive(reply,1,source)==SessionAdmission::Established; break;
    case 3: ok=auth.receive(reply,101,source)==SessionAdmission::Updated; break;
    case 4: ok=outgoing.build({wire_vectors::query,sizeof(wire_vectors::query)},route,0,5,source,encoded); break;
    default: std::abort();
    }
    watching=false; const size_t boundaries=attempts; assert(throwing_new==0);
    if(fault) {
        assert(hits==1 && !ok); assert(keys.acquire(after) && after.public_point(current) && current==initial && keys.revision()==revision);
        if(operation==1) { assert(disk.calls==0); }
        if(operation>=2) { assert(auth.pending() && auth.ready()==(operation==3)); }
        if(operation==4) { assert(outgoing.counter()==7 && encoded.length==0); }
        switch(operation) {
        case 0: assert(keys.import_pem({pem.data(),length},source)); break;
        case 1: assert(keys.rotate(disk,source).committed); break;
        case 2: assert(auth.receive(reply,1,source)==SessionAdmission::Established); break;
        case 3: assert(auth.receive(reply,101,source)==SessionAdmission::Updated); break;
        case 4: assert(outgoing.build({wire_vectors::query,sizeof(wire_vectors::query)},route,0,5,source,encoded) && outgoing.counter()==8); break;
        }
    } else { assert(ok); }
    return boundaries;
}
int main()
{
    size_t total=0;
    for(unsigned operation=0;operation<5;++operation) {
        const size_t boundaries=run_operation(operation,0);
        for(size_t fault=1;fault<=boundaries;++fault) {
            run_operation(operation,fault); for(auto pointer:live) { assert(pointer==nullptr); }
        }
        total+=boundaries;
        std::printf("Continuation fault operation %u: %zu calloc boundaries, retained identity/session/pending and same-object retry passed\n",operation,boundaries);
    }
    std::printf("Continuation allocation matrix: %zu deterministic injected calloc failures, no tracked crypto leaks, zero feature-owned throwing new\n",total);
}
