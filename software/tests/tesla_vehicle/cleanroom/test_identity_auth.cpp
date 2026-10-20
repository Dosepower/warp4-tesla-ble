// SPDX-License-Identifier: Apache-2.0
// PUBLIC clean host fixture; never follows legacy includes or internals.
#define main retained_authentication_main
#include "test_authentication.cpp"
#undef main
struct Disk : KeyStorage {
    SaveResult result=SaveResult::Committed;
    void (*hook)(void *)=nullptr; void *context=nullptr;
    SaveResult save(Bytes bytes) override { assert(bytes.valid() && bytes.size>0); if(hook) { hook(context); } return result; }
};
int main()
{
    unsigned cases=0;
    for(auto outcome:{SaveResult::NotCommitted,SaveResult::NotCommittedCleanupPending,SaveResult::Committed,SaveResult::CommittedCleanupPending,SaveResult::RestartRequired}) {
        Rng rng; IdentityStore keys; seed(keys,rng);
        DomainAuthentication vcsec(Domain::Vcsec,{vectors::vin,17}),info(Domain::Infotainment,{vectors::vin,17}); Link vlink,ilink;
        assert(vcsec.begin(keys,{1,19,0,1000,500},0,rng.get(),vlink.get())==ChallengeResult::Sent);
        assert(vcsec.receive(reply(Domain::Vcsec,19,vlink.last.uuid),1,rng.get())==SessionAdmission::Established);
        Disk disk; disk.result=outcome;
        struct C { IdentityStore *keys; DomainAuthentication *info; Rng *rng; Link *link; } c{&keys,&info,&rng,&ilink};
        disk.context=&c; disk.hook=[](void *p) { auto &c=*static_cast<C *>(p);
            assert(c.info->begin(*c.keys,{2,19,0,1000,500},1,c.rng->get(),c.link->get())==ChallengeResult::Sent);
        };
        assert(keys.rotate(disk,rng.get()).committed==KeyStorage::committed(outcome));
        assert(info.pending() && !info.ready() && vcsec.ready());
        assert(info.receive(reply(Domain::Infotainment,19,ilink.last.uuid),2,rng.get())==SessionAdmission::Established);
        IdentityLease a,b,current; assert(vcsec.identity(a) && info.identity(b) && a.same(b) && keys.acquire(current));
        assert(a.same(current)!=KeyStorage::committed(outcome));
        assert(info.receive(reply(Domain::Infotainment,19,ilink.last.uuid),3,rng.get())==SessionAdmission::Rejected);
        vcsec.reset(); info.reset(); assert(!vcsec.identity(a) && !info.identity(b));
        ++cases;
    }
    std::printf("Clean identity/auth composition: %u save outcomes preserve one established domain and another domain's callback-started pending exchange through real HMAC admission and duplicate rejection\n",cases);
}
