// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/identity.hpp>
#include <tesla_clean/session_info.hpp>
#include <mbedtls/ctr_drbg.h>
#include "vectors.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace tesla_clean;
struct Rng {
    mbedtls_ctr_drbg_context drbg;
    static int entropy(void *,unsigned char *p,size_t n) { for(size_t i=0;i<n;++i) { p[i]=static_cast<uint8_t>(i*17+31); } return 0; }
    Rng() { mbedtls_ctr_drbg_init(&drbg); assert(mbedtls_ctr_drbg_seed(&drbg,entropy,nullptr,nullptr,0)==0); }
    ~Rng() { mbedtls_ctr_drbg_free(&drbg); }
    Random get() { return {mbedtls_ctr_drbg_random,&drbg}; }
};
struct Heap {
    size_t calls=0,fail=0,live=0;
    static void *get(void *c,size_t n) { auto &h=*static_cast<Heap *>(c); if(++h.calls==h.fail) { return nullptr; } void *p=std::malloc(n); if(p) { ++h.live; } return p; }
    static void put(void *c,void *p) { if(p) { --static_cast<Heap *>(c)->live; std::free(p); } }
    Allocation get() { return {get,put,this}; }
};
struct Disk : KeyStorage {
    SaveResult result=SaveResult::Committed;
    std::array<uint8_t,512> bytes{},first{}; size_t size=0,first_size=0;
    unsigned calls=0; bool after=false;
    void (*hook)(void *)=nullptr; void *context=nullptr;
    SaveResult save(Bytes pem) override {
        ++calls;
        if(calls==1) { first_size=pem.size; std::memcpy(first.data(),pem.data,pem.size); }
        else { assert(first_size==pem.size && !std::memcmp(first.data(),pem.data,pem.size)); }
        auto promote=[&] { if(committed(result)) { size=pem.size; std::memcpy(bytes.data(),pem.data,size); } };
        if(after) { promote(); }
        auto callback=hook; hook=nullptr; if(callback) { callback(context); }
        if(!after) { promote(); }
        return result;
    }
};
static std::array<uint8_t,65> point(const IdentityLease &lease)
{
    std::array<uint8_t,65> p{}; assert(lease.public_point(p)); return p;
}
static std::array<uint8_t,512> scalar_pem(Rng &rng,uint8_t value,size_t &n)
{
    P256Key key; std::array<uint8_t,32> scalar{}; scalar[31]=value;
    assert(key.import_scalar({scalar.data(),scalar.size()},rng.get()));
    std::array<uint8_t,512> pem{}; assert(key.export_pem(pem.data(),pem.size(),n)); return pem;
}
int main()
{
    constexpr SaveResult results[]={SaveResult::NotCommitted,SaveResult::NotCommittedCleanupPending,SaveResult::Committed,SaveResult::CommittedCleanupPending,SaveResult::RestartRequired};
    unsigned cases=0;
    for(bool after:{false,true}) for(auto outer:results) for(auto inner:results) {
        Rng rng; Heap heap; IdentityStore keys(heap.get()); size_t length=0; auto pem=scalar_pem(rng,1,length);
        assert(keys.import_pem({pem.data(),length},rng.get()));
        IdentityLease pending,established; assert(keys.acquire(pending) && pending.clone(established));
        const auto old=point(pending); const auto revision=keys.revision();
        Disk disk; disk.result=outer; disk.after=after;
        struct Reentry { IdentityStore *keys; Disk *disk; Rng *rng; Heap *heap; SaveResult inner,outer; bool accepted=false; } context{&keys,&disk,&rng,&heap,inner,outer};
        disk.context=&context;
        disk.hook=[](void *raw) {
            auto &c=*static_cast<Reentry *>(raw); c.disk->result=c.inner;
            c.heap->fail=c.heap->calls+1; // nothing after the first save may allocate an identity
            c.accepted=c.keys->rotate(*c.disk,c.rng->get()).committed;
            c.disk->result=c.outer;
        };
        auto outcome=keys.rotate(disk,rng.get());
        assert(context.accepted==KeyStorage::committed(inner));
        assert(outcome.committed==(KeyStorage::committed(inner)||KeyStorage::committed(outer)));
        assert(disk.calls==2 && keys.revision()==revision+(outcome.committed?1:0));
        assert(keys.quarantined()==(outer==SaveResult::RestartRequired || inner==SaveResult::RestartRequired));
        IdentityLease current; assert(keys.acquire(current));
        assert(point(pending)==old && point(established)==old);
        if(outcome.committed) {
            P256Key saved; assert(saved.import_pem({disk.bytes.data(),disk.size},rng.get()));
            std::array<uint8_t,65> stored{}; assert(saved.public_point(stored));
            assert(stored==point(current) && stored!=old);
        } else { assert(point(current)==old); }
        // The prior identity remains cryptographically usable after commit and after refusal.
        SessionInfo info; std::array<uint8_t,16> shared{};
        assert(authenticate_session_info(*pending.key(),rng.get(),{vectors::vin,17},{vectors::challenge,16},
            {vectors::encoded_info,sizeof(vectors::encoded_info)},{vectors::info_tag,32},info,shared)==SessionInfoResult::Established);
        ++cases;
    }
    Rng rng; Heap heap;
    {
        IdentityStore keys(heap.get()); size_t length=0; auto pem=scalar_pem(rng,1,length);
        assert(keys.import_pem({pem.data(),length},rng.get()));
        IdentityLease old; assert(keys.acquire(old)); const auto prior=point(old); Disk disk;
        heap.fail=heap.calls+1;
        assert(!keys.rotate(disk,rng.get()).committed && disk.calls==0);
        IdentityLease same; assert(keys.acquire(same) && point(same)==prior);
        heap.fail=0; assert(keys.rotate(disk,rng.get()).committed);
        assert(point(old)==prior);
    }
    assert(heap.live==0);
    // Different transaction/storage and mutation operations cannot join a live candidate.
    {
        IdentityStore keys; size_t n=0; auto pem=scalar_pem(rng,1,n); assert(keys.import_pem({pem.data(),n},rng.get()));
        Disk disk,other;
        struct C { IdentityStore *keys; Disk *other; Rng *rng; Bytes pem; } c{&keys,&other,&rng,{pem.data(),n}};
        disk.context=&c; disk.hook=[](void *p) { auto &c=*static_cast<C *>(p);
            assert(!c.keys->rotate(*c.other,c.rng->get()).committed && c.other->calls==0);
            assert(!c.keys->import_pem(c.pem,c.rng->get()));
            assert(!c.keys->generate(c.rng->get()));
        };
        assert(keys.rotate(disk,rng.get()).committed);
        disk.calls=0; disk.hook=[](void *p) { auto &c=*static_cast<C *>(p);
            assert(!c.keys->rotate(*c.other,c.rng->get()).committed);
            assert(!c.keys->import_pem(c.pem,c.rng->get()));
        };
        assert(keys.persist(disk).committed);
        disk.calls=0; disk.result=SaveResult::RestartRequired;
        assert(!keys.rotate(disk,rng.get()).committed && keys.quarantined());
        const auto calls=disk.calls; assert(!keys.rotate(disk,rng.get()).committed && disk.calls==calls);
        assert(!keys.import_pem({pem.data(),n},rng.get()) && !keys.generate(rng.get()));
    }
    std::printf("Clean identity: %u mixed nested before/after save outcomes, one durable publication, old pending/established crypto leases, allocation refusal and quarantine passed\n",cases);
}
