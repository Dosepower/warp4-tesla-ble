// SPDX-License-Identifier: Apache-2.0
// Shared PUBLIC deterministic host fixture and unchanged lifecycle assertions.
#define main retained_challenge_main
#include "test_authentication.cpp"
#undef main
static std::array<uint8_t,32> sign_info(Bytes data,const std::array<uint8_t,16> &uuid,Bytes remote_point)
{
    Rng rng; P256Key key; std::array<uint8_t,32> scalar{}; scalar[31]=1;
    assert(key.import_scalar({scalar.data(),32},rng.get()));
    std::array<uint8_t,16> secret{}; assert(key.exchange(remote_point,rng.get(),secret));
    SessionCrypto crypto; assert(crypto.set_key({secret.data(),secret.size()}));
    std::array<uint8_t,32> tag{};
    assert(crypto.session_info_tag({vectors::vin,17},{uuid.data(),uuid.size()},data,tag)); return tag;
}
int main()
{
    for(auto d:{Domain::Vcsec,Domain::Infotainment}) {
        Rng rng; IdentityStore keys; seed(keys,rng); DomainAuthentication auth(d,{vectors::vin,17}); Link link;
        OperationTime op{1,77,100,5000,500};
        assert(auth.begin(keys,op,100,rng.get(),link.get())==ChallengeResult::Sent);
        assert(auth.receive(reply(d,77,link.last.uuid),101,rng.get())==SessionAdmission::Established);
        const auto original=auth.info();
        rng.id=1; op.context=2; op.started=300;
        assert(auth.begin(keys,op,300,rng.get(),link.get())==ChallengeResult::Sent);
        // A newly chosen remote key must not authoritatively deny an established peer.
        P256Key stranger; std::array<uint8_t,32> scalar{}; scalar[31]=3;
        assert(stranger.import_scalar({scalar.data(),32},rng.get()));
        std::array<uint8_t,65> point{}; assert(stranger.public_point(point));
        auto changed=std::array<uint8_t,sizeof(vectors::denied_info)>{};
        std::memcpy(changed.data(),vectors::denied_info,changed.size()); std::memcpy(changed.data()+4,point.data(),point.size());
        auto tag=sign_info({changed.data(),changed.size()},link.last.uuid,{point.data(),point.size()});
        SessionReply intrusion{d,77,link.last.uuid,{changed.data(),changed.size()},{tag.data(),tag.size()}};
        assert(auth.receive(intrusion,301,rng.get())==SessionAdmission::Rejected && auth.pending() && auth.ready());
        assert(auth.info().remote_public==original.remote_public && auth.info().counter==original.counter);
        // Stale authenticated clock/counter cannot move either backwards in one epoch.
        auto older=std::array<uint8_t,sizeof(vectors::encoded_info)>{}; std::memcpy(older.data(),vectors::encoded_info,older.size());
        older[1]=2; const size_t clock_offset=older.size()-4; older[clock_offset]=1; older[clock_offset+1]=older[clock_offset+2]=older[clock_offset+3]=0;
        tag=sign_info({older.data(),older.size()},link.last.uuid,{original.remote_public.data(),65});
        SessionReply update{d,77,link.last.uuid,{older.data(),older.size()},{tag.data(),tag.size()}};
        assert(auth.receive(update,302,rng.get())==SessionAdmission::Updated && !auth.pending());
        assert(auth.info().clock==original.clock && auth.info().counter==original.counter);
        // A changed epoch admits its authenticated clock, but never decrements the outgoing counter.
        rng.id=2; op.context=3; op.started=500;
        assert(auth.begin(keys,op,500,rng.get(),link.get())==ChallengeResult::Sent);
        older[71]^=0x40; tag=sign_info({older.data(),older.size()},link.last.uuid,{original.remote_public.data(),65});
        update.uuid=link.last.uuid; update.tag={tag.data(),tag.size()};
        assert(auth.receive(update,501,rng.get())==SessionAdmission::Updated);
        assert(auth.info().clock==1 && auth.info().counter==7 && auth.info().epoch!=original.epoch);
        // A newer clock updates; malformed/unauthenticated/deadline-expired replies do not consume the challenge.
        rng.id=3; op.context=4; op.started=700;
        assert(auth.begin(keys,op,700,rng.get(),link.get())==ChallengeResult::Sent);
        older[1]=9; older[clock_offset]=5; tag=sign_info({older.data(),older.size()},link.last.uuid,{original.remote_public.data(),65});
        update.uuid=link.last.uuid; update.tag={tag.data(),tag.size()};
        assert(auth.receive(update,1200,rng.get())==SessionAdmission::Rejected && auth.pending());
        assert(auth.receive(update,701,rng.get())==SessionAdmission::Updated && auth.info().clock==5 && auth.info().counter==9);
        assert(auth.receive(update,702,rng.get())==SessionAdmission::Rejected);
        auth.reset(); assert(!auth.crypto());
    }
    std::puts("Clean signed session update: existing-peer HMAC trust including denial, authenticated epoch/clock update, nondecreasing counter and deadline/once-only admission passed");
    return 0;
}
