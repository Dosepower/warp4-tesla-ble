// SPDX-License-Identifier: Apache-2.0
// Run inside actual task_loop startup: production storage mutation guard installed.
template<class Body> void ui_with_worker_guard(Body body) {
    auto *r=module.tesla->runtime_owner.get();
    r->next_poll_ms=millis()+600000;
    on_delay=[&]() {
        on_delay={}; // First worker pass, after the production storage guard is installed.
        peer.address=r->routing_address;
        ready(r->connection_generation.load());
        body();
        r->stop_requested.store(true);
    };
    run_task(r->task_handle); on_delay={};
    assert(r->worker_stopped.load());
}
void ui_approve_without_main() {
    auto *r=module.tesla->runtime_owner.get();
    clock_ms.fetch_add(1000); module.process_pair_verification(); tick();
    if(last().hello) { inject(peer.hello(last())); tick(); }
    inject(peer.response(last(),{awake,sizeof(awake)}));
    assert(r->pair_approval_waiting.load());
}
void ui_r2_approval_disconnect() {
    ui_with_worker_guard([&]() {
        auto *r=module.tesla->runtime_owner.get(); queue_pair(); module.process_commands(); tick();
        ui_approve_without_main();
        esp_ble_gattc_cb_param_t p; p.disconnect.conn_id=4;
        TeslaVehicle::gatt_callback(ESP_GATTC_DISCONNECT_EVT,1,&p);
        module.process_module_events(); // Approval cannot consume the invalid session.
        String error; api.commands.at("tesla_vehicle/pair")(0,error);
        assert(error.length()!=0 && !r->pair_requested.load());
        // Old queued disconnect must finish before a new command is admitted.
        module.process_disconnect(); module.process_module_events();
        assert(!module.tesla->state.get("paired")->asBool() && !module.tesla->state.get("pairing_pending")->asBool());
        ready(r->connection_generation.load()); const auto before=sent.size(); queue_pair();
        module.process_commands(); tick(); assert(sent.size()==before+1 && last().pair);
        ui_approve_without_main(); module.process_module_events();
        assert(module.tesla->state.get("paired")->asBool() && !r->pair_requested.load());
        module.process_disconnect(); module.process_module_events();
        assert(module.tesla->state.get("paired")->asBool());
        std::puts("R2 Approval/disconnect: ownership retained through old cleanup; successor consumed/authenticated");
    });
}
void ui_r2_generated_key_failure(bool identity_change) {
    LittleFS.files.clear();
    module.tesla->runtime_owner.get()->bound_vehicle_mac.store(0);
    module.tesla->runtime_owner.get()->session.bind(0);
    ui_with_worker_guard([&]() {
        auto *r=module.tesla->runtime_owner.get(); module.process_module_events();
        assert(module.tesla->state.get("key_presence")->asString()=="absent");
        IdentityLease absent; assert(!r->keys->acquire(absent));
        bool binding_attempted=false;
        fs_fault=[&](const std::string &op) {
            if(op=="open:/config/tesla_ble_warp_ev_mac.tmp:w") {
                binding_attempted=true;
                assert(r->storage->private_key_presence()==TeslaBleStorageAdapter::KeyPresence::Present);
                if(identity_change) { ++ev.generation; module.loop(); return false; }
                return true;
            }
            return false;
        };
        queue_pair(); module.process_commands(); fs_fault={}; assert(binding_attempted);
        module.process_module_events();
        assert(sent.empty() && !r->bound_vehicle_mac.load() && !r->pair_requested.load());
        assert(module.tesla->state.get("key_present")->asBool());
        assert(module.tesla->state.get("key_presence")->asString()=="present");
        assert(!module.tesla->state.get("paired")->asBool() && !module.tesla->state.get("pairing_pending")->asBool());
        // This exact state enables existing UI deletion policy; do not weaken it.
        std::printf("R2_DELETION_STATE {\"running\":%s,\"key_present\":%s,\"key_presence\":\"%s\",\"paired\":false,\"pairing_pending\":false}\n",
            module.tesla->state.get("running")->asBool() ? "true" : "false",
            module.tesla->state.get("key_present")->asBool() ? "true" : "false",
            module.tesla->state.get("key_presence")->asString().c_str());
        assert(module.tesla->state.get("last_error")->asString().length()!=0);
        module.update_active_vehicle();
        String error; api.commands.at("tesla_vehicle/forget_key")(0,error); assert(error.length()==0);
        module.process_commands(); module.process_module_events();
        assert(r->storage->private_key_presence()==TeslaBleStorageAdapter::KeyPresence::Absent);
        assert(module.tesla->state.get("key_presence")->asString()=="absent" && sent.empty());
        std::puts("R2 generated key: guarded post-persistence binding failure publishes present; local deletion succeeds");
    });
}
void ui_r2_contention(bool diagnostic_error, bool terminal_failure) {
    ui_with_worker_guard([&]() {
        auto *r=module.tesla->runtime_owner.get(); queue_pair(); module.process_commands(); tick();
        if(diagnostic_error) { module.post_event(TeslaVehicle::EventType::Error,"older generic error"); }
        std::atomic<bool> locked{false},release{false};
        std::thread worker([&]() {
            {
                tesla_warp::CheckedMutex::Guard lock(r->command_outcome.mutex);
                locked.store(true);
                while(!release.load()) { std::this_thread::yield(); }
            }
            if(terminal_failure) { clock_ms.fetch_add(60001); module.process_pair_verification(); }
            else { ui_approve_without_main(); }
        });
        while(!locked.load()) { std::this_thread::yield(); }
        on_failed_trylock=[&]() { release.store(true); worker.join(); };
        module.process_module_events(); on_failed_trylock={};
        assert(!worker.joinable()); // real take() returned contention, terminal consumed next
        assert(module.tesla->state.get("paired")->asBool()==!terminal_failure);
        assert(!module.tesla->state.get("pairing_pending")->asBool());
        const auto error=module.tesla->state.get("last_error")->asString();
        for(unsigned i=0;i<3;++i) { module.process_module_events(); }
        assert(module.tesla->state.get("paired")->asBool()==!terminal_failure);
        assert(!module.tesla->state.get("pairing_pending")->asBool() && !module.tesla->state.get("pair_request_sent")->asBool());
        assert(module.tesla->state.get("last_error")->asString()==error);
        std::puts("R2 contended generic take: delayed PairStarted/Error cannot overwrite authenticated/failure terminal");
    });
}
void ui_r2_deadline_successor() {
    ui_with_worker_guard([&]() {
        auto *r=module.tesla->runtime_owner.get(); queue_pair(); module.process_commands(); tick(); module.process_module_events();
        clock_ms.fetch_add(1000); module.process_pair_verification(); tick();
        assert(r->pair_status_active); // status controller awaiting a response
        clock_ms.fetch_add(60001); module.process_pair_verification(); module.process_module_events();
        assert(!module.tesla->state.get("pairing_pending")->asBool());
        ready(r->connection_generation.load()); const auto before=sent.size(); queue_pair();
        module.process_commands(); tick(); assert(sent.size()==before+1 && last().pair);
        ui_approve_without_main(); module.process_module_events();
        assert(module.tesla->state.get("paired")->asBool() && !module.tesla->state.get("pairing_pending")->asBool());
        std::puts("R2 deadline: old status retired; fresh successor consumed, transmitted and authenticated");
    });
}
