// SPDX-License-Identifier: Apache-2.0
// Members of the independently authored module fixture; observable ownership only.
static void configure(TeslaVehicle &o) {
    o.pre_setup(); o.setup(); o.config.get("enable")->updateBool(true);
    o.bluetooth_boot_enabled = true; // Explicit master permission, never inferred from service config.
    o.config.get("vin")->updateString("5YJ3E1EA7KF000001");
    o.config.get("ble_address")->updateString("01:02:03:04:05:06");
    o.config.get("ev_index")->updateInt(ev.index);
}
void acceptance() {
    auto *r=module.runtime_owner.get();
    for(unsigned i=0;i<4;++i) { VehicleCompletion c; assert(r->vehicle->status(i+1,std::move(c))); }
    module.request_soc(true);
    assert(!r->in_flight_started_ms && !r->in_flight.request && r->vehicle->queued()==4 && sent.empty());
    queue_pair(); module.process_commands(); module.process_module_events();
    assert(!r->pair_sending && !r->pair_verifying && !module.state.get("pairing_pending")->asBool() && r->vehicle->queued()==4);
    module.disconnect_internal(); ready(r->connection_generation.load());
    module.request_soc(true); tick(); assert(last().hello); inject(peer.hello(last())); tick();
    inject(peer.response(last(),{asleep,sizeof(asleep)},1)); tick();
    VcsecPacket expected; assert(wake_payload(true,expected));
    assert(last().plaintext.size()==expected.length && !std::memcmp(last().plaintext.data(),expected.bytes.data(),expected.length));
    inject(peer.response(last(),{awake,sizeof(awake)},2)); tick(); inject(peer.hello(last())); tick();
    inject(peer.response(last(),{soc_bytes(),sizeof(controller_fixture::soc)},100)); module.process_module_events();
    assert(ev.applied==1 && !r->in_flight_started_ms);
    std::puts("Independent actual WARP immediate queue acceptance: refused poll/pair has no request/wake/false pending publication, next accepted manual poll uses the retained one wake and authenticates SoC");
}
void identity_busy() {
    auto *r=module.runtime_owner.get();
    const auto stable=r->session.published.version.load(); assert(!(stable&1));
    r->session.published.version.store(stable+1);
    unsigned submissions=0;
    assert(!module.gatt_operation(TeslaVehicle::Write,10,[&]() { ++submissions; return true; }));
    assert(!submissions && !r->operation.retired() && !r->stop_requested.load());
    r->session.published.version.store(stable);
    unsigned delays=0;
    on_delay=[&]() { ++delays; r->session.published.version.store(stable); };
    const bool ok=module.gatt_operation(TeslaVehicle::Write,10,[&]() {
        ++submissions; r->session.published.version.store(stable+1);
        r->operation.complete(TeslaVehicle::Write,4,10,true); return true;
    });
    on_delay={};
    assert(ok && submissions==1 && delays && !r->operation.retired() && !r->stop_requested.load());
    assert(r->connected.load() && r->vehicle->connected());
    std::puts("Independent WARP GATT identity contention: unavailable pre-submit refuses without retirement; paused unchanged identity during acknowledged submit defers and succeeds after bounded delay");
}
void storage_quarantine() {
    auto *r=module.runtime_owner.get(); const auto disk=LittleFS.files;
    TeslaBleStorageIO::close_quarantine.store(true);
    r->queues.events()->capacity=0; run_task(r->task_handle);
    assert(r->worker_stopped.load() && !r->shutdown_result.read()->key_known && LittleFS.files==disk);
    module.loop();
    assert(module.state.get("restart_required")->asBool() && module.state.get("key_presence")->asString()=="unknown");
    assert(r->teardown_handle); run_task(r->teardown_handle); module.loop();
    assert(!module.runtime_owner.get() && !module.start_runtime());
    assert(module.state.get("last_error")->asString()=="Tesla persistence close failed; storage quarantined until reboot");
    assert(queues_live==0 && LittleFS.files==disk);
    std::puts("Independent WARP storage quarantine consumer: full event queue cannot lose worker ACK/unknown presence/reboot diagnostic; teardown preserves terminal restart refusal and storage");
}
static void reset_os() {
    assert(queues_live==0); tasks.clear(); fail_task.clear(); fail_queue_at=queue_creates=0;
    ble_init_result=cleanup_result=true; host_status=controller_status=2;
    cleanup_calls.clear(); on_delay={}; sdk_init={}; sdk_disable={}; fs_fault={};
    TeslaVehicle::gatt_owner.store(nullptr); clock_ms.store(1);
}
static void finish_worker(TeslaVehicle &o) {
    auto *r=o.runtime_owner.get(); r->stop_requested.store(true);
    run_task(r->task_handle); o.config.get("enable")->updateBool(false); o.loop();
    assert(r->teardown_handle); run_task(r->teardown_handle); o.loop();
    assert(!o.runtime_owner.get() && queues_live==0);
}
static void composition() {
    unsigned cases=0;
    reset_os();
    { TeslaVehicle o; o.pre_setup(); o.setup(); assert(!o.runtime_owner.get() && tasks.empty() && queues_live==0); ++cases; }
    for(unsigned n=1;n<=2;++n) {
        reset_os(); TeslaVehicle o; configure(o); fail_queue_at=n;
        assert(!o.start_runtime() && !o.runtime_owner.get() && queues_live==0 && tasks.empty());
        fail_queue_at=0; assert(o.start_runtime()); finish_worker(o); ++cases;
    }
    reset_os();
    { TeslaVehicle o; configure(o); fail_task="tesla_ble";
      assert(!o.start_runtime() && !o.runtime_owner.get() && queues_live==0);
      fail_task.clear(); assert(o.start_runtime()); finish_worker(o); ++cases; }
    for(unsigned mode=0;mode<5;++mode) {
        reset_os(); TeslaVehicle o; configure(o); assert(o.start_runtime()); auto *r=o.runtime_owner.get();
        o.config.get("enable")->updateBool(false);
        if(mode==0) {
            o.loop(); clock_ms.fetch_add(30001); o.loop();
            assert(o.runtime_owner.get()==r && r->teardown.failed() && !r->task_handle->deleted && !r->teardown_handle);
        } else {
            r->stop_requested.store(true); run_task(r->task_handle);
            if(mode==2) { fail_task="tesla_teardown"; }
            o.loop();
            if(mode==1) {
                auto *task=r->teardown_handle; assert(task); clock_ms.fetch_add(30001); o.loop();
                assert(o.runtime_owner.get()==r && !task->deleted && r->teardown.failed());
            } else if(mode==2) { assert(!r->task_handle && !r->teardown_handle && r->teardown.failed()); }
            else {
                // Model a used/partially initialized controller independently of protocol startup.
                r->controller_used=true;
                if(mode==4) { cleanup_result=false; }
                run_task(r->teardown_handle); o.loop();
                if(mode==3) {
                    assert(!o.runtime_owner.get());
                    assert(cleanup_calls==std::vector<std::string>({"host-disable","host-deinit","controller-disable","controller-deinit","arduino-deinit","ble-memory-release"}));
                } else { assert(o.runtime_owner.get()==r && r->teardown.failed() && cleanup_calls==std::vector<std::string>{"host-disable"}); }
            }
        }
        assert(o.runtime_owner.restart_required() && !o.accepts_commands()); ++cases;
    }
    for(unsigned fault=1;fault<=2;++fault) {
        reset_os(); TeslaVehicle o; configure(o); assert(o.start_runtime()); auto *r=o.runtime_owner.get();
        r->queues.events()->capacity=0;
        ModuleAllocationFault::calls=ModuleAllocationFault::failures=0;
        ModuleAllocationFault::fail_at=fault; ModuleAllocationFault::active=true;
        run_task(r->task_handle); ModuleAllocationFault::active=false;
        assert(ModuleAllocationFault::failures==1 && !r->vehicle && !r->controller_used && r->worker_stopped.load());
        o.loop(); assert(r->teardown_handle); run_task(r->teardown_handle); o.loop();
        assert(!o.runtime_owner.get() && !o.runtime_owner.restart_required());
        assert(o.state.get("last_error")->asString()=="Tesla protocol initialization failed or cancelled");
        assert(o.start_runtime()); finish_worker(o); ++cases;
    }
    for(unsigned mode=0;mode<3;++mode) {
        reset_os(); TeslaVehicle o; configure(o); assert(o.start_runtime()); auto *r=o.runtime_owner.get();
        LittleFS.files.clear();
        if(mode==0) { LittleFS.files["/config/tesla_ble_warp_ev_mac"]={1}; }
        else { LittleFS.files["/config/tesla_ble_private_key"]={255}; }
        const auto disk=LittleFS.files;
        if(mode==2) { fs_fault=[](const std::string &op) { return op=="read"; }; }
        run_task(r->task_handle); fs_fault={};
        assert(r->worker_stopped.load() && !r->vehicle && !r->controller_used && LittleFS.files==disk);
        o.loop(); assert(r->teardown_handle); run_task(r->teardown_handle); o.loop();
        assert(!o.runtime_owner.get() && !o.runtime_owner.restart_required()); ++cases;
    }
    reset_os();
    { TeslaVehicle o; o.pre_setup(); o.setup(); LittleFS.files["/config/tesla_ble_private_key"]={1};
      const auto init=ble_init_calls; assert(o.start_runtime(true)); auto *r=o.runtime_owner.get(); run_task(r->task_handle);
      assert(r->worker_stopped.load() && LittleFS.files.empty() && ble_init_calls==init);
      o.loop(); assert(r->teardown_handle); run_task(r->teardown_handle); o.loop();
      assert(!o.runtime_owner.get() && !o.runtime_owner.restart_required()); ++cases; }
    reset_os();
    std::printf("Independent actual WARP composition: %u disabled/queue/task/stall/teardown/partial-init/worker-allocation/storage-load/storage-only schedules; ACK independent of full events, truthful failure/retry and no premature destruction passed\n",cases);
}
static void chronology() {
    unsigned cases=0;
    for(bool pair:{false,true}) for(bool reverse:{false,true}) for(bool saturated:{false,true}) {
        reset_os(); TeslaBleModuleTest t; auto *r=t.module.runtime_owner.get(); ev.applied=0;
        const auto identity=r->session.identity(); assert(identity);
        if(pair) {
            t.queue_pair();
            const auto accepted=r->command_outcome.claim_pair(); assert(accepted);
            r->pair_command=accepted->command;
            r->command_outcome.pair_sent(r->pair_command);
            r->pair_requested.store(false);
        }
        const auto token=r->session.arm(r->epoch,*identity,r->configured_ev_index); assert(token.request);
        auto outcome=[&]() { t.module.post_event(pair?TeslaVehicle::EventType::PairStarted:TeslaVehicle::EventType::KeyPresence,nullptr,1); };
        auto soc_event=[&]() { t.module.post_event(TeslaVehicle::EventType::Soc,nullptr,72,token,true,true); };
        if(saturated) { r->queues.events()->capacity=0; }
        if(reverse) { soc_event(); outcome(); } else { outcome(); soc_event(); }
        t.module.process_module_events();
        assert(ev.applied==(saturated?0:1));

        assert(t.module.state.get("paired")->asBool()==(!saturated && !reverse));
        // A generic key publication or earlier poll proof cannot release an
        // admitted enrollment. Its terminal acknowledgement is independent.
        assert(t.module.state.get("pairing_pending")->asBool()==pair);
        if(pair) { t.module.cancel_pairing("chronology cancellation",r->pair_command); t.module.process_module_events(); }
        t.module.post_event(TeslaVehicle::EventType::KeyRemoved); t.module.process_module_events();
        assert(!t.module.state.get("key_present")->asBool() && !t.module.state.get("paired")->asBool());
        t.module.post_event(TeslaVehicle::EventType::PairStarted); t.module.process_module_events();
        t.module.post_event(TeslaVehicle::EventType::Error,"pressure diagnostic"); t.module.process_module_events();
        assert(!t.module.state.get("pairing_pending")->asBool() && t.module.state.get("last_error")->asString()=="pressure diagnostic");
        ++cases;
    }
    std::printf("Independent WARP chronology: %u forward/reverse authoritative key/pair + SoC schedules; full-queue outcomes survive, lost SoC never applied, newest pairing/error order retained\n",cases);
}
