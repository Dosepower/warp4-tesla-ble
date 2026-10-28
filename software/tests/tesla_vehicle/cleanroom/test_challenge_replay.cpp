// SPDX-License-Identifier: Apache-2.0
// SessionInfo challenge retry semantics through the production Vehicle path.
// Pinned embedded reference tesla-ble v6.2.0 (a00f3872) src/vehicle.cpp:320-342:
//   an unanswered session request is resent UNCHANGED every TRANSPORT_RETRY_INTERVAL
//   (1 s) and rebuilt with a new UUID only once CLOCK_SYNC_MAX_LATENCY (4 s) has
//   elapsed; handle_session_info_message_ (1038-1042) admits a reply to that UUID
//   while it is younger than 4 s. Official vehicle-command a4b43c1e ble.go maxLatency
//   = 4 s bounds SessionInfo freshness from request creation.
// WARP keeps: response budget from full ATT-acknowledged framed TX (resend cadence),
// separate total deadline and challenge freshness, no stale/foreign UUID/domain/
// generation, no authentication or authenticated acceptance after the total
// deadline, started frames completed (bounded), no counter rollback.
#include "controller_fixture.hpp"
#include <cstdio>
#include <cstring>
using namespace controller_fixture;
namespace {
constexpr VehicleTiming verify{5000,1000,10000}; // production pair-verification status poll
struct Plan { uint32_t delay=0; bool armed=false; uint32_t at=0; Frame frame; void (*action)(Harness &)=nullptr; } plan;
void timed_write(Harness &h)
{
    h.clock+=plan.delay;
    if(plan.armed && h.clock>=plan.at) {
        plan.armed=false;
        if(plan.action) { auto a=plan.action; plan.action=nullptr; a(h); } else { h.inject(plan.frame); }
    }
    h.hook=timed_write;
}
void reset_plan(uint32_t delay) { plan=Plan{}; plan.delay=delay; }
bool same(const Sent &a,const Sent &b) { return a.uuid==b.uuid; }
// Tick in 10 ms steps until `frames` frames were sent or a result arrived.
void run_until(Harness &h,std::vector<VehicleResult> &results,size_t frames,uint32_t limit)
{
    const uint32_t end=h.clock+limit;
    while(h.sent.size()<frames && results.empty() && h.clock<end) { h.hook=timed_write; h.vehicle.tick(); h.clock+=10; }
}
int fail(const char *message) { std::puts(message); return 1; }
const char *only=nullptr;
bool run(const char *name) { return !only || std::strcmp(only,name)==0; }
}
int main(int argc,char **argv)
{
    if(argc>1) { only=argv[1]; }
    // A) Slow ATT TX (6 x 150 ms) and a valid SessionInfo for the FIRST copy that
    //    arrives 1.3 s after that copy's ATT-acknowledged completion (2.2 s after
    //    creation), i.e. while the retry is on the air. It is staged/deferred and
    //    must authenticate; the encrypted status then completes authenticated.
    if(run("A")) {
        Harness h; std::vector<VehicleResult> results; reset_plan(150); h.hook=timed_write;
        assert(h.vehicle.open(h.link) && h.vehicle.status(31,h.callback(results),verify));
        const uint32_t created=h.clock; h.vehicle.tick();
        const auto first=h.last(); assert(first.hello && h.clock-created==900);
        plan.armed=true; plan.at=created+2200; plan.frame=h.hello(first);
        run_until(h,results,2,3000);
        if(h.sent.size()<2 || !h.sent[1].hello) { return fail("RED: no session request retry observed"); }
        const auto second=h.sent[1];
        std::printf("A: retry hello uuid %s first; reply staged during retry TX at +%u ms\n",same(first,second)?"==":"!=",plan.at-created);
        assert(!plan.armed); // the reply was injected during the retry's framed TX
        run_until(h,results,3,3000);
        if(h.sent.size()<3 || h.sent[2].hello) {
            return fail("RED: delayed valid SessionInfo for a still-fresh challenge was discarded because the retry replaced its UUID");
        }
        assert(same(first,second));
        const auto status=h.sent[2]; assert(status.domain==Domain::Vcsec && status.plaintext==std::vector<uint8_t>({0x0a,0}));
        // A late duplicate reply (vehicle answered the retry copy too) is not a
        // second establishment and cannot roll the counter back.
        const uint32_t high=h.vehicle.high_water(Domain::Vcsec); assert(high==status.counter);
        h.hook=nullptr; h.inject(h.hello(second,false,3,10)); h.vehicle.tick();
        assert(h.sent.size()==3 && results.empty() && h.vehicle.high_water(Domain::Vcsec)==high);
        h.clock+=300; h.inject(h.response(status,{awake,sizeof(awake)},1)); h.vehicle.tick();
        assert(results.size()==1 && results[0].code==VehicleResultCode::Status && results[0].authenticated && results[0].context==31);
        assert(h.clock-created<verify.total_ms);
    }
    // B) Silent vehicle, instant writes: identical copies every response budget
    //    (1 s after completion) while fresh, then a NEW challenge at the 4 s
    //    freshness boundary; replays are bounded and the total deadline is not
    //    extended. The predecessor UUID is never readmitted afterwards.
    if(run("B")) {
        Harness h; std::vector<VehicleResult> results; reset_plan(0); h.hook=timed_write;
        assert(h.vehicle.open(h.link) && h.vehicle.status(32,h.callback(results),verify));
        const uint32_t start=h.clock; std::vector<uint32_t> at;
        while(results.empty() && h.clock-start<6000) {
            const size_t n=h.sent.size(); h.hook=timed_write; h.vehicle.tick();
            if(h.sent.size()!=n) { at.push_back(h.clock-start); } h.clock+=10;
        }
        std::printf("B: %zu session requests at",h.sent.size()); for(auto t:at) { std::printf(" +%u",t); } std::puts(" ms");
        if(h.sent.size()<4 || !same(h.sent[0],h.sent[1])) { return fail("RED: unanswered fresh challenge was not resent unchanged"); }
        assert(results.size()==1 && results[0].code==VehicleResultCode::Expired && !results[0].authenticated);
        assert(h.sent.size()==5 && at[0]==0 && at[1]==1000 && at[2]==2000 && at[3]==3000 && at[4]==4100);
        for(size_t i=0;i<5;++i) { assert(h.sent[i].hello && h.sent[i].domain==Domain::Vcsec); }
        for(size_t i=1;i<4;++i) { assert(same(h.sent[0],h.sent[i])); }
        assert(!same(h.sent[0],h.sent[4])); // renewed at the freshness boundary
        assert(h.clock-start>=verify.total_ms && h.clock-start<verify.total_ms+20); // expired exactly, not extended
        // Next operation: neither the renewed nor the original challenge of the
        // expired operation is admitted (no cross-operation correlation).
        std::vector<VehicleResult> next; assert(h.vehicle.status(33,h.callback(next),verify));
        h.inject(h.hello(h.sent[4])); h.inject(h.hello(h.sent[0])); h.vehicle.tick();
        assert(h.sent.size()==6 && h.last().hello && !same(h.last(),h.sent[4]) && !same(h.last(),h.sent[0]));
        h.inject(h.hello(h.sent[4])); h.vehicle.tick(); assert(next.empty() && h.sent.size()==6);
        h.clock+=200; h.inject(h.hello(h.last())); h.vehicle.tick(); assert(h.sent.size()==7 && !h.last().hello);
    }
    // C) Slow TX: a replay is only sent when it can still complete while the
    //    challenge is fresh; otherwise a new challenge is created. A reply to the
    //    renewed (old) UUID is refused, even if captured < 4 s after creation, as
    //    are wrong UUID/domain/generation/HMAC. The new UUID authenticates.
    if(run("C")) {
        Harness h; std::vector<VehicleResult> results; reset_plan(150); h.hook=timed_write;
        assert(h.vehicle.open(h.link) && h.vehicle.status(34,h.callback(results),{9000,1000,10000}));
        const uint32_t created=h.clock; h.vehicle.tick(); const auto u1=h.last();
        run_until(h,results,3,5000);
        if(h.sent.size()<3 || !same(u1,h.sent[1])) { return fail("RED: fresh challenge was not replayed before renewal"); }
        const auto u2=h.sent[2]; assert(u2.hello && !same(u1,u2));
        std::printf("C: renewed challenge TX completed at +%u ms after first creation\n",h.clock-10-created);
        // During/after renewal: the predecessor's reply is not admitted.
        h.hook=nullptr; h.inject(h.hello(u1)); h.vehicle.tick(); assert(h.sent.size()==3 && results.empty());
        auto wrong=u2; wrong.uuid[0]^=1; h.inject(h.hello(wrong)); h.vehicle.tick(); assert(h.sent.size()==3);
        wrong=u2; wrong.domain=Domain::Infotainment; h.inject(h.hello(wrong)); h.vehicle.tick(); assert(h.sent.size()==3);
        h.inject(h.hello(u2),h.link+1,size_t{7}); h.vehicle.tick(); assert(h.sent.size()==3);
        h.inject(h.hello(u2,false,7,1234,true)); h.vehicle.tick(); assert(h.sent.size()==3 && results.empty());
        h.clock+=500; h.inject(h.hello(u2)); h.vehicle.tick(); const auto status=h.last(); assert(h.sent.size()==4 && !status.hello);
        h.clock+=200; h.inject(h.response(status,{awake,sizeof(awake)},1)); h.vehicle.tick();
        assert(results.size()==1 && results[0].code==VehicleResultCode::Status && results[0].authenticated);
    }
    // D) No late authentication: a SessionInfo captured before the total deadline
    //    while the request's last fragment awaited its ATT response is dispatched
    //    only after TX completion, which is past the deadline. The started frame is
    //    completed (no recover/link loss), but no session may be established: the
    //    next operation must start with a fresh challenge.
    if(run("D")) {
        Harness h; std::vector<VehicleResult> results; reset_plan(200); h.hook=timed_write; h.recoverable=false;
        assert(h.vehicle.open(h.link) && h.vehicle.status(41,h.callback(results),{1100,1000,10000}));
        const uint32_t start=h.clock;
        h.on_frame=[](Harness &x) { x.inject(x.hello(x.last())); };
        h.vehicle.tick(); assert(h.sent.size()==1 && h.sent[0].hello && h.clock-start==1200 && h.recoveries==0 && h.pending.empty());
        h.hook=nullptr; h.vehicle.tick(); assert(results.size()==1 && results[0].code==VehicleResultCode::Expired && !results[0].authenticated);
        std::vector<VehicleResult> next; assert(h.vehicle.connected() && h.vehicle.status(42,h.callback(next),verify)); h.vehicle.tick();
        if(h.sent.size()!=2 || !h.last().hello) {
            return fail("RED: SessionInfo dispatched after the total deadline authenticated a session (late authentication)");
        }
    }
    // E) Same for the encrypted status reply: captured during the status
    //    request's final ATT wait before the deadline, dispatched after it. The
    //    frame completes, but no authenticated success is published post-deadline.
    if(run("E")) {
        Harness h; std::vector<VehicleResult> results; reset_plan(0); h.hook=timed_write;
        assert(h.vehicle.open(h.link) && h.vehicle.status(43,h.callback(results),{2000,1000,10000}));
        h.vehicle.tick(); h.clock+=50; h.inject(h.hello(h.last()));
        plan.delay=200; h.hook=timed_write;
        h.on_frame=[](Harness &x) { x.inject(x.response(x.last(),{awake,sizeof(awake)},1)); };
        h.vehicle.tick(); assert(h.sent.size()==2 && !h.sent[1].hello && h.clock-100>=2000 && h.pending.empty());
        if(!results.empty() && results[0].authenticated) {
            return fail("RED: encrypted status dispatched after the total deadline was published as authenticated success");
        }
        h.hook=nullptr; h.vehicle.tick(); assert(results.size()==1 && results[0].code==VehicleResultCode::Expired && !results[0].authenticated);
    }
    // F) Cancellation during a replay: link close inside the retry TX retires the
    //    operation as Disconnected; the old challenge never authenticates on the
    //    successor generation and the successor starts with a new UUID.
    if(run("F")) {
        Harness h; std::vector<VehicleResult> results; reset_plan(150); h.hook=timed_write; h.recoverable=false;
        assert(h.vehicle.open(h.link) && h.vehicle.status(51,h.callback(results),verify));
        const uint32_t created=h.clock; h.vehicle.tick(); const auto u1=h.last();
        plan.armed=true; plan.at=created+2100; plan.action=[](Harness &x) { x.vehicle.close(x.link); x.link+=1; };
        run_until(h,results,3,3000);
        assert(results.size()==1 && results[0].code==VehicleResultCode::Disconnected && !h.vehicle.connected());
        assert(h.vehicle.open(h.link)); h.pending.clear();
        std::vector<VehicleResult> next; assert(h.vehicle.status(52,h.callback(next),verify));
        h.hook=nullptr; h.inject(h.hello(u1)); h.vehicle.tick(); const auto fresh=h.last();
        assert(fresh.hello && !same(fresh,u1) && next.empty());
        h.inject(h.hello(u1)); h.vehicle.tick(); assert(h.last().hello && next.empty());
        h.clock+=100; h.inject(h.hello(fresh)); h.vehicle.tick(); assert(!h.last().hello);
    }
    // G) DomainAuthentication contract directly: per-copy response budget kept,
    //    replay keeps UUID/public point/freshness anchor/owner budget, is bounded
    //    (count, freshness incl. last TX duration, total, backoff), refused replay
    //    keeps the live challenge, staged reply rechecked at dispatch time.
    if(run("G")) {
        struct Link {
            uint32_t clock=100,tx=0; unsigned sends=0; bool accept=true; ChallengeView last;
            DomainAuthentication *auth=nullptr; Random random; bool stage=false; SessionReply staged;
            static bool send(void *p,const ChallengeView &v) {
                auto &l=*static_cast<Link *>(p); ++l.sends; l.last=v;
                if(l.stage) { l.stage=false; assert(l.auth->receive(l.staged,l.clock+l.tx/3,l.random)==SessionAdmission::Deferred); }
                l.clock+=l.tx; return l.accept;
            }
            static uint32_t now(void *p) { return static_cast<Link *>(p)->clock; }
            ChallengeTransport get() { return {send,this,now}; }
        };
        std::array<uint8_t,32> tag{};
        auto make=[&tag](const std::array<uint8_t,16> &u) {
            tag=session_tag({vectors::encoded_info,sizeof(vectors::encoded_info)},{u.data(),16});
            SessionReply r; r.domain=Domain::Vcsec; r.generation=9; r.uuid=u;
            r.encoded={vectors::encoded_info,sizeof(vectors::encoded_info)}; r.tag={tag.data(),tag.size()}; return r;
        };
        {   // slow copies: budget per copy, staged reply during identical replay
            Rng rng; IdentityStore keys; seed(keys,rng); DomainAuthentication auth(Domain::Vcsec,{vectors::vin,17});
            Link l; l.auth=&auth; l.random=rng.get(); l.tx=900; const OperationTime op{1,9,100,9000,1000};
            assert(auth.begin(keys,op,100,rng.get(),l.get())==ChallengeResult::Sent && l.clock==1000);
            const auto first=l.last;
            assert(auth.receive(make(first.uuid),2000,rng.get())==SessionAdmission::Rejected && auth.pending()); // budget of copy 1 spent
            assert(!auth.replayable({2,9,100,9000,1000},2000) && !auth.replayable({1,10,100,9000,1000},2000));
            assert(!auth.replayable({1,9,50,9000,1000},2000) && !auth.replayable({1,9,100,9900,1000},2000) && !auth.replayable({1,9,100,9000,900},2000));
            assert(auth.replayable(op,2000) && auth.replayable(op,3000) && !auth.replayable(op,3200)); // 3100+900 ms TX would end stale
            l.clock=2000; l.stage=true; l.staged=make(first.uuid);
            assert(auth.replay(op,2000,rng.get(),l.get())==ChallengeResult::Established && l.sends==2 && auth.ready() && !auth.pending());
            assert(l.last.uuid==first.uuid && l.last.public_point==first.public_point && l.last.context==first.context);
            assert(!auth.replayable(op,2100) && auth.receive(make(first.uuid),2100,rng.get())==SessionAdmission::Rejected);
        }
        {   // instant copies: cap 3 replays, then a NEW challenge; predecessor refused
            Rng rng; IdentityStore keys; seed(keys,rng); DomainAuthentication auth(Domain::Vcsec,{vectors::vin,17});
            Link l; const OperationTime op{1,9,100,9000,1000};
            assert(auth.begin(keys,op,100,rng.get(),l.get())==ChallengeResult::Sent); const auto first=l.last;
            assert(auth.replay(op,150,rng.get(),l.get())==ChallengeResult::Backoff && l.sends==1);
            for(uint32_t t:{1100u,2100u,3100u}) { l.clock=t; assert(auth.replay(op,t,rng.get(),l.get())==ChallengeResult::Sent && l.last.uuid==first.uuid); }
            assert(!auth.replayable(op,3500) && auth.replay(op,3500,rng.get(),l.get())==ChallengeResult::Invalid && l.sends==4);
            l.clock=3500; assert(auth.begin(keys,op,3500,rng.get(),l.get())==ChallengeResult::Sent && l.last.uuid!=first.uuid);
            assert(auth.receive(make(first.uuid),3510,rng.get())==SessionAdmission::Rejected);
            assert(auth.receive(make(l.last.uuid),3510,rng.get())==SessionAdmission::Established);
        }
        {   // refused replay keeps the live challenge and its earlier copy's budget; reset ends replay
            Rng rng; IdentityStore keys; seed(keys,rng); DomainAuthentication auth(Domain::Vcsec,{vectors::vin,17});
            Link l; const OperationTime op{1,9,100,9000,1000};
            assert(auth.begin(keys,op,100,rng.get(),l.get())==ChallengeResult::Sent); const auto first=l.last;
            l.accept=false; l.clock=600; assert(auth.replay(op,600,rng.get(),l.get())==ChallengeResult::Refused && auth.pending());
            l.accept=true; assert(auth.receive(make(first.uuid),700,rng.get())==SessionAdmission::Established);
            assert(auth.begin(keys,{2,9,800,9000,1000},800,rng.get(),l.get())==ChallengeResult::Sent);
            auth.reset(); assert(!auth.replayable({2,9,800,9000,1000},1900) && !auth.pending());
        }
        {   // total deadline: no replay after it; staged reply dispatched after it is refused
            Rng rng; IdentityStore keys; seed(keys,rng); DomainAuthentication auth(Domain::Vcsec,{vectors::vin,17});
            Link l; l.auth=&auth; l.random=rng.get(); const OperationTime op{1,9,100,2000,1000};
            assert(auth.begin(keys,op,100,rng.get(),l.get())==ChallengeResult::Sent); const auto first=l.last;
            assert(!auth.replayable(op,2100));
            l.clock=1500; l.tx=900; l.stage=true; l.staged=make(first.uuid); // captured 1800, dispatched 2400
            assert(auth.replay(op,1500,rng.get(),l.get())==ChallengeResult::Sent && !auth.ready() && auth.pending());
        }
    }
    std::puts("Challenge replay: same fresh UUID resent after full-TX response budget, delayed/staged valid reply admitted, renewal at 4 s freshness, stale/wrong UUID/domain/generation/HMAC refused, no post-deadline authentication, cancel/generation and counter controls passed");
}
