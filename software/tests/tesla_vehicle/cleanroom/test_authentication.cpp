// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/authentication.hpp>
#include <mbedtls/ctr_drbg.h>
#include "vectors.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tesla_clean;
struct Rng {
    mbedtls_ctr_drbg_context drbg; uint8_t id=0; bool fail_uuid=false;
    static int entropy(void *,unsigned char *p,size_t n) { std::memset(p,17,n); return 0; }
    static int fill(void *p,unsigned char *out,size_t n) {
        auto &r=*static_cast<Rng *>(p);
        if(n==16) { if(r.fail_uuid) { return -1; } for(size_t i=0;i<n;++i) { out[i]=static_cast<uint8_t>(i+r.id); } return 0; }
        return mbedtls_ctr_drbg_random(&r.drbg,out,n);
    }
    Rng() { mbedtls_ctr_drbg_init(&drbg); assert(mbedtls_ctr_drbg_seed(&drbg,entropy,nullptr,nullptr,0)==0); }
    ~Rng() { mbedtls_ctr_drbg_free(&drbg); }
    Random get() { return {fill,this}; }
};
struct Link {
    bool accepted=true; unsigned sends=0; ChallengeView last;
    void (*hook)(void *)=nullptr; void *context=nullptr;
    static bool send(void *p,const ChallengeView &view) {
        auto &l=*static_cast<Link *>(p); ++l.sends; l.last=view;
        auto h=l.hook; l.hook=nullptr; if(h) { h(l.context); } return l.accepted;
    }
    ChallengeTransport get() { return {send,this}; }
};
static void seed(IdentityStore &keys,Rng &rng)
{
    P256Key key; std::array<uint8_t,32> scalar{}; scalar[31]=1;
    assert(key.import_scalar({scalar.data(),32},rng.get()));
    std::array<uint8_t,512> pem{}; size_t n=0;
    assert(key.export_pem(pem.data(),pem.size(),n) && keys.import_pem({pem.data(),n},rng.get()));
}
static SessionReply reply(Domain d,uint64_t g,const std::array<uint8_t,16> &uuid,bool denied=false)
{
    SessionReply r; r.domain=d; r.generation=g; r.uuid=uuid;
    r.encoded=denied?Bytes{vectors::denied_info,sizeof(vectors::denied_info)}:Bytes{vectors::encoded_info,sizeof(vectors::encoded_info)};
    r.tag=denied?Bytes{vectors::denied_tag,32}:Bytes{vectors::info_tag,32}; return r;
}
static void retag(SessionReply &r,std::array<uint8_t,32> &tag)
{
    SessionCrypto crypto; assert(crypto.set_key({vectors::session_key,16}));
    assert(crypto.session_info_tag({vectors::vin,17},{r.uuid.data(),16},r.encoded,tag));
    r.tag={tag.data(),tag.size()};
}
int main()
{
    for(auto d:{Domain::Vcsec,Domain::Infotainment}) for(uint64_t g:{uint64_t{5},(uint64_t{1}<<32)+5,UINT64_MAX}) {
        Rng rng; IdentityStore keys; seed(keys,rng); DomainAuthentication auth(d,{vectors::vin,17}); Link link;
        OperationTime owner{43,g,1000,5000,500};
        assert(auth.begin(keys,owner,1000,rng.get(),link.get())==ChallengeResult::Sent && auth.pending());
        const auto uuid=link.last.uuid;
        auto r=reply(d,g,uuid); auto wrong=r; wrong.generation=g^ (uint64_t{1}<<32);
        assert(auth.receive(wrong,1001,rng.get())==SessionAdmission::Rejected && auth.pending());
        wrong=r; wrong.domain=d==Domain::Vcsec?Domain::Infotainment:Domain::Vcsec;
        assert(auth.receive(wrong,1001,rng.get())==SessionAdmission::Rejected);
        wrong=r; wrong.uuid[15]^=1; assert(auth.receive(wrong,1001,rng.get())==SessionAdmission::Rejected);
        auto corrupt=std::array<uint8_t,32>{}; wrong=r; wrong.tag={corrupt.data(),32};
        assert(auth.receive(wrong,1001,rng.get())==SessionAdmission::Rejected && !auth.ready());
        assert(auth.receive(r,1001,rng.get())==SessionAdmission::Established && auth.ready() && !auth.pending());
        assert(auth.info().counter==7 && auth.info().clock==1234);
        IdentityLease established; assert(auth.identity(established));
        std::array<uint8_t,65> before{},after{}; assert(established.public_point(before));
        assert(auth.receive(r,1002,rng.get())==SessionAdmission::Rejected);
        // Key rotation must not relabel established or subsequent refresh identities.
        assert(keys.generate(rng.get()));
        rng.id=1;
        owner.context=44; owner.started=1200;
        assert(auth.begin(keys,owner,1200,rng.get(),link.get())==ChallengeResult::Sent);
        assert(link.last.public_point==before);
        assert(auth.receive(r,1201,rng.get())==SessionAdmission::Rejected);
        auto denied=reply(d,g,link.last.uuid,true); std::array<uint8_t,32> denial_tag{}; retag(denied,denial_tag);
        wrong=denied; wrong.tag={corrupt.data(),32};
        assert(auth.receive(wrong,1201,rng.get())==SessionAdmission::Rejected && auth.ready());
        assert(auth.receive(denied,1201,rng.get())==SessionAdmission::KeyDenied && !auth.pending());
        assert(auth.identity(established) && established.public_point(after) && after==before);
        assert(auth.receive(denied,1202,rng.get())==SessionAdmission::Rejected);
        auth.reset(); assert(!auth.ready() && !auth.pending() && !auth.identity(established));
        rng.id=2;
        owner.context=45; owner.started=1300;
        assert(auth.begin(keys,owner,1300,rng.get(),link.get())==ChallengeResult::Sent);
        assert(link.last.public_point!=before);
    }
    // Refused recovery preserves original live challenge; attempts/backoff/deadline are bounded.
    {
        Rng rng; IdentityStore keys; seed(keys,rng); DomainAuthentication auth(Domain::Vcsec,{vectors::vin,17}); Link link;
        OperationTime owner{1,7,100,1000,500};
        assert(auth.begin(keys,owner,100,rng.get(),link.get())==ChallengeResult::Sent);
        const auto uuid=link.last.uuid;
        rng.id=1; link.accepted=false;
        assert(auth.begin(keys,owner,150,rng.get(),link.get())==ChallengeResult::Backoff && link.sends==1);
        assert(auth.begin(keys,owner,200,rng.get(),link.get())==ChallengeResult::Refused && auth.pending());
        rng.id=2;
        assert(auth.begin(keys,owner,300,rng.get(),link.get())==ChallengeResult::Refused);
        assert(auth.begin(keys,owner,400,rng.get(),link.get())==ChallengeResult::Exhausted && link.sends==3);
        auto r=reply(Domain::Vcsec,7,uuid);
        assert(auth.receive(r,401,rng.get())==SessionAdmission::Established);
        owner.context=2; owner.started=100; assert(auth.begin(keys,owner,1100,rng.get(),link.get())==ChallengeResult::Expired && link.sends==3);
    }
    // Transport refusal cannot publish a synchronous staged response.
    for(bool accepted:{false,true}) {
        Rng rng; IdentityStore keys; seed(keys,rng); DomainAuthentication auth(Domain::Vcsec,{vectors::vin,17}); Link link;
        struct C { DomainAuthentication *auth; Link *link; Rng *rng; } c{&auth,&link,&rng};
        link.context=&c; link.accepted=accepted;
        link.hook=[](void *p) { auto &c=*static_cast<C *>(p); auto r=reply(Domain::Vcsec,9,c.link->last.uuid);
            assert(c.auth->receive(r,101,c.rng->get())==SessionAdmission::Deferred && !c.auth->ready());
        };
        assert(auth.begin(keys,{1,9,100,1000,500},100,rng.get(),link.get())==(accepted?ChallengeResult::Established:ChallengeResult::Refused));
        assert(auth.ready()==accepted && !auth.pending());
    }
    // Full-width disconnect/nested replacement must survive the old transmit unwind.
    for(bool accepted:{false,true}) {
        Rng rng; IdentityStore keys; seed(keys,rng); DomainAuthentication auth(Domain::Vcsec,{vectors::vin,17}); Link old,fresh;
        struct C { DomainAuthentication *auth; IdentityStore *keys; Link *fresh; Rng *rng; } c{&auth,&keys,&fresh,&rng};
        old.accepted=accepted; old.context=&c;
        old.hook=[](void *p) { auto &c=*static_cast<C *>(p); c.auth->reset(); c.rng->id=1;
            assert(c.auth->begin(*c.keys,{2,(uint64_t{1}<<32)+9,100,1000,500},101,c.rng->get(),c.fresh->get())==ChallengeResult::Sent);
        };
        assert(auth.begin(keys,{1,9,100,1000,500},100,rng.get(),old.get())==ChallengeResult::Superseded && auth.pending());
        auto stale=reply(Domain::Vcsec,9,old.last.uuid); assert(auth.receive(stale,102,rng.get())==SessionAdmission::Rejected);
        assert(fresh.sends==1);
    }
    std::puts("Clean challenge lifecycle: both domains/full-width generations, HMAC admission, leased identity, denial once, bounded refusal recovery, deadlines, synchronous RX staging and nested successor preservation passed");
    return 0;
}
