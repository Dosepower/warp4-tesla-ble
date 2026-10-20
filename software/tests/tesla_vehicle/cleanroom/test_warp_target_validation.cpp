// SPDX-License-Identifier: Apache-2.0
#include "module_validation_definitions.inc"
static bool inject=false;
static unsigned calls=0,fail=0;
extern "C" void *__real_realloc(void *,size_t);
extern "C" void *__wrap_realloc(void *p,size_t n) { if(inject && ++calls==fail) { return nullptr; } return __real_realloc(p,n); }
struct Input {
    std::array<Config,6> leaf; Config root;
    explicit Input(unsigned invalid=3) {
        root.leaves=leaf.data(); leaf[0].boolean=true;
        assert(leaf[1].updateStringChecked(invalid==0?"invalid":"5yj3e1ea7kf000001"));
        assert(leaf[2].updateStringChecked(invalid==1?"invalid":"aa:bb:cc:dd:ee:ff"));
        leaf[3].integer=invalid==2?-1:2; leaf[4].integer=300;
    }
};
int main() {
    unsigned faults=0;
    for(unsigned kind=0;kind<4;++kind) {
        TeslaVehicle owner; Input in(kind); calls=fail=0; inject=true;
        const auto baseline=owner.validate(in.root,ConfigSource::API); inject=false; const auto boundaries=calls;
        assert(baseline.isEmpty()==(kind==3));
        if(kind==3) { assert(in.leaf[1].asString()=="5YJ3E1EA7KF000001" && in.leaf[2].asString()=="AA:BB:CC:DD:EE:FF"); }
        for(unsigned n=1;n<=boundaries;++n) {
            TeslaVehicle fresh; Runtime runtime; fresh.tesla->runtime_owner.pointer=&runtime; Input candidate(kind);
            fail=n; calls=0; inject=true; const auto error=fresh.validate(candidate.root,ConfigSource::API); inject=false;
            assert(!error.isEmpty() && error=="OOM" && !fresh.tesla->runtime_update_requested.load());
            assert(fresh.tesla->config.get("vin")->asString().isEmpty() && !runtime.pair_requested && !runtime.poll_requested && !runtime.forget_requested);
            assert(fresh.validate(candidate.root,ConfigSource::API).isEmpty()==(kind==3)); ++faults;
        }
    }
    TeslaVehicle off(false); off.register_urls(); assert(api.count==0);
    TeslaVehicle owner; owner.register_urls(); Runtime runtime; owner.tesla->runtime_owner.pointer=&runtime; owner.accepting=true;
    assert(api.count==4);
    const char *topics[]={"tesla_vehicle/start","tesla_vehicle/pair","tesla_vehicle/poll","tesla_vehicle/forget_key"};
    for(unsigned i=0;i<4;++i) { assert(!std::strcmp(api.topics[i],topics[i])); }
    constexpr unsigned Start=0, Pair=1, Poll=2, Forget=3;
    // Explicit volatile consent; every admission blocker must still refuse
    // when allocating error text fails, with no vehicle command side effect.
    owner.tesla->config.get("enable")->updateBool(true);
    owner.tesla->runtime_owner.pointer=nullptr;
    { String error; api.commands[Start](0,error); assert(error.isEmpty() && owner.service_start_requested); }
    for(unsigned blocker=0;blocker<5;++blocker) {
        owner.service_start_requested=false;
        owner.bluetooth.enabled=blocker!=0;
        owner.bluetooth_restart_pending=blocker==1;
        owner.tesla->config.get("enable")->updateBool(blocker!=2);
        owner.tesla->runtime_owner.restart=blocker==3;
        owner.tesla->runtime_owner.pointer=blocker==4?&runtime:nullptr;
        String error; calls=0; fail=1; inject=true; api.commands[Start](0,error); inject=false;
        assert(error=="OOM" && !owner.service_start_requested);
        assert(!runtime.pair_requested && !runtime.poll_requested && !runtime.forget_requested); ++faults;
    }
    owner.bluetooth.enabled=true; owner.bluetooth_restart_pending=false;
    owner.tesla->config.get("enable")->updateBool(true); owner.tesla->runtime_owner.restart=false;
    owner.tesla->runtime_owner.pointer=&runtime;
    // API dispatch supplies a fresh empty error buffer for each invocation.
    { String error; api.commands[Pair](0,error); assert(error.isEmpty() && runtime.pair_requested); }
    assert(owner.tesla->state.get("pairing_pending")->asBool() && !owner.tesla->state.get("pair_request_sent")->asBool());
    // Duplicate enrollment refuses even when allocating its error text fails.
    runtime.pair_requested=false;
    { String error; calls=0; fail=1; inject=true; api.commands[Pair](0,error); inject=false;
      assert(error=="OOM" && !runtime.pair_requested); ++faults; }
    { String error; api.commands[Poll](0,error); assert(error.isEmpty() && runtime.poll_requested); }
    { String error; api.commands[Forget](0,error); assert(error.isEmpty() && runtime.forget_requested);
      assert(!runtime.pair_requested && !runtime.poll_requested); }
    // Deletion blocks all competing commands, including its own duplicate,
    // even when refusal text allocation fails (real admission, not a UI mock).
    for(unsigned i=Pair;i<=Forget;++i) {
        runtime.forget_requested=false;
        String error; calls=0; fail=1; inject=true; api.commands[i](0,error); inject=false;
        assert(error=="OOM" && !runtime.pair_requested && !runtime.poll_requested && !runtime.forget_requested); ++faults;
    }
    owner.accepting=false;
    for(unsigned i=Pair;i<=Forget;++i) {
        runtime.pair_requested=false; runtime.poll_requested=false; runtime.forget_requested=false;
        String error; calls=0; fail=1; inject=true; api.commands[i](0,error); inject=false;
        assert(error=="OOM" && !runtime.pair_requested && !runtime.poll_requested && !runtime.forget_requested); ++faults;
    }
    std::printf("Independent actual validator/command bodies + WString: %u normalization/error/command allocation faults, no live-config/runtime publication on refusal, exact normalization and retry passed\n",faults);
}
