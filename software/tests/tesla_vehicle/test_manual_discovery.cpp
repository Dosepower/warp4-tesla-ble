// Real production discovery policy; deterministic clock and SDK event schedules.
#include "../../src/modules/tesla_vehicle/tesla_ble_discovery.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
using D = TeslaBleDiscovery;
static std::vector<uint8_t> adv(const char *name) {
    std::vector<uint8_t> b{static_cast<uint8_t>(strlen(name)+1),9};
    b.insert(b.end(),name,name+strlen(name));return b;
}
static void start(D &d, uint32_t now=1) {
    assert(d.request(now)); assert(!d.request(now));
    assert(d.step(now)==D::Action::Params); d.submitted(D::Action::Params,true);
    d.event(D::Event::Params,true);
    assert(d.step(now+1)==D::Action::Start);d.submitted(D::Action::Start,true);
    d.event(D::Event::Start,true);
}
static void observe(D &d, const char *name, unsigned address=1, uint8_t type=0, unsigned event=0) {
    auto b=adv(name);uint8_t a[6]={1,2,3,4,5,static_cast<uint8_t>(address)};
    d.observe(b.data(),b.size(),0,a,type,-50,event);
}
int main() {
    D d;D::Snapshot s;
    static_assert(sizeof(D::Snapshot)<768,"Discovery result storage must stay bounded");
    D concurrent;std::atomic<unsigned> admissions{0};std::vector<std::thread> clients;
    for(unsigned i=0;i<8;++i) clients.emplace_back([&] { if(concurrent.request(1)) { ++admissions; } });
    for(auto &client:clients) { client.join(); }
    assert(admissions==1);
    assert(d.snapshot(s));assert(s.status==D::Status::Idle && !d.busy());
    assert(d.step(100000)==D::Action::None); // No boot/timer scan.
    start(d);assert(d.busy());
    observe(d,"headphones");observe(d,"Tesla");observe(d,"S0123456789abcdegC");
    observe(d,"S0123456789abcdefC",1,2);observe(d,"S0123456789abcdefC",1,0,3);
    assert(d.snapshot(s) && s.count==0);
    observe(d,"S0123456789abcdefC");observe(d,"S0123456789abcdefC");
    assert(d.snapshot(s) && s.count==1 && s.targets[0].rssi==-50);
    // A malformed AD block, conflicting names, and invalid addresses are rejected.
    auto b=adv("S0123456789abcdefC"); b[0]=31;uint8_t a[6]={1,2,3,4,5,2};
    d.observe(b.data(),b.size(),0,a,0,-20,0);
    auto good=adv("S0123456789abcdefC");auto conflict=adv("Sfedcba9876543210C");
    auto both=good;both.insert(both.end(),conflict.begin(),conflict.end());
    d.observe(both.data(),good.size(),conflict.size(),a,0,-20,0);
    uint8_t zero[6]={};d.observe(good.data(),good.size(),0,zero,0,-20,0);
    assert(d.snapshot(s) && s.count==1);
    // Name in scan response is accepted only for connectable original ADV.
    uint8_t flags[]={2,1,6};auto payload=std::vector<uint8_t>(flags,flags+3);payload.insert(payload.end(),good.begin(),good.end());
    d.observe(payload.data(),3,good.size(),a,1,-60,0);
    for(unsigned i=3;i<20;++i) observe(d,"S0123456789abcdefC",i);
    assert(d.snapshot(s) && s.count==8 && s.truncated);
    d.event(D::Event::Done,true);assert(d.step(10002)==D::Action::None);
    assert(!d.busy());assert(d.snapshot(s) && s.status==D::Status::Complete && s.count==8);
    observe(d,"S0123456789abcdefC",55);assert(d.snapshot(s) && s.count==8);
    assert(d.step(130003)==D::Action::None);assert(d.snapshot(s) && s.count==0 && s.status==D::Status::Idle);
    start(d,140000);d.event(D::Event::Done,true);d.step(150001);assert(d.snapshot(s) && s.count==0 && s.status==D::Status::Complete);
    // Timeout has one stop attempt and permanent admission quarantine, no retry.
    D timeout;start(timeout);assert(timeout.step(15002)==D::Action::Stop);
    timeout.submitted(D::Action::Stop,true);assert(timeout.busy());
    timeout.event(D::Event::Stop,true);assert(timeout.step(15003)==D::Action::Retire);
    assert(timeout.snapshot(s) && s.status==D::Status::Error && s.count==0);
    assert(!timeout.request(16000));timeout.quiesced();assert(!timeout.busy());assert(!timeout.request(17000));
    D missing;start(missing);assert(missing.step(15002)==D::Action::Stop);missing.submitted(D::Action::Stop,true);
    assert(missing.step(18003)==D::Action::Retire);assert(!missing.request(19000));
    D cancel;start(cancel);cancel.cancel();assert(cancel.step(4)==D::Action::Stop);cancel.submitted(D::Action::Stop,false);
    assert(cancel.step(5)==D::Action::Retire);cancel.quiesced();assert(!cancel.busy());
    D fail;assert(fail.request(1));assert(fail.step(1)==D::Action::Params);fail.submitted(D::Action::Params,false);
    assert(fail.step(2)==D::Action::Retire);assert(!fail.request(3));
    D negative;assert(negative.request(1));negative.step(1);negative.submitted(D::Action::Params,true);negative.event(D::Event::Params,false);
    assert(negative.step(2)==D::Action::Retire);
    D lost;assert(lost.request(1));lost.step(1);lost.submitted(D::Action::Params,true);
    assert(lost.step(3002)==D::Action::Retire);
    D startfail;assert(startfail.request(1));startfail.step(1);startfail.submitted(D::Action::Params,true);startfail.event(D::Event::Params,true);
    assert(startfail.step(2)==D::Action::Start);startfail.submitted(D::Action::Start,false);
    assert(startfail.step(3)==D::Action::Stop);startfail.submitted(D::Action::Stop,false);assert(startfail.step(4)==D::Action::Retire);
    // Wrap-safe finite timeout and late callbacks cannot reopen a failed scan.
    D wrap;start(wrap,0xfffffff0u);assert(wrap.step(15000)==D::Action::Stop);
    wrap.event(D::Event::Params,true);wrap.event(D::Event::Start,true);wrap.event(D::Event::Done,true);
    assert(!wrap.request(15001));
    std::puts("MANUAL_DISCOVERY_POLICY_PASS filter/dedupe/cap/expiry/empty/timeouts/teardown/late-events/no-auto-retry");
}
