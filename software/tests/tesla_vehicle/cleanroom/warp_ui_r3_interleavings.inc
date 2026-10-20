// SPDX-License-Identifier: Apache-2.0
#ifdef TESLA_BLE_MODULE_POSIX_TEST
void ui_r3_posix(bool guard_refusal, bool deletion_failure, bool close_failure, bool session_failure=false);
#endif
void ui_r3_failure_disconnect(bool status) {
    ui_with_worker_guard([&]() {
        auto *r=module.tesla->runtime_owner.get(); queue_pair(); module.process_commands();
        Frame response;
        if(status) {
            tick(); clock_ms.fetch_add(1000); module.process_pair_verification(); tick();
            if(last().hello) { inject(peer.hello(last())); tick(); }
            response=peer.response(last(),{awake,sizeof(awake)});
        }
        // Capture actual outcome mutex at its OS boundary, without changing code.
        capture_unlock=true;
        { tesla_warp::CheckedMutex::Guard lock(r->command_outcome.mutex); }
        unsigned preemptions=0;
        const auto preempt=[&]() {
            ++preemptions;
            std::thread callback([&]() {
                esp_ble_gattc_cb_param_t p; p.disconnect.conn_id=4;
                TeslaVehicle::gatt_callback(ESP_GATTC_DISCONNECT_EVT,1,&p);
            }); callback.join();
        };
        auto original_write=on_gatt_write;
        if(status) { on_selected_unlock=preempt; }
        else { on_gatt_write=[&]() {
            original_write();
            // The final fragment's post-write cancellation probe releases the
            // same lock once; preempt the NEXT unlock: completion pair_current.
            if(prefix.empty() && !sent.empty()) { on_selected_unlock=[&]() { on_selected_unlock=preempt; }; }
        }; }
        if(status) { inject(response); } else { tick(); }
        on_gatt_write=original_write;
        on_selected_unlock={}; assert(preemptions==1);
        module.process_module_events();
        String duplicate; api.commands.at("tesla_vehicle/pair")(0,duplicate);
        assert(duplicate.length()!=0 && !r->pair_requested.load());
        module.process_disconnect(); module.process_module_events();
        ready(r->connection_generation.load()); queue_pair(); const auto before=sent.size();
        module.process_commands(); tick(); assert(sent.size()==before+1 && last().pair);
        ui_approve_without_main(); module.process_module_events();
        assert(module.tesla->state.get("paired")->asBool());
        std::puts("R3 completion preemption: Failed cannot ACK before pending old disconnect cleanup; fresh successor completes");
    });
}
void ui_r3_close_composition(bool delayed, bool reversed=false) {
    ui_with_worker_guard([&]() {
        auto *r=module.tesla->runtime_owner.get(); queue_pair(); module.process_commands(); tick();
        bool disconnected=false;
        const auto disconnect=[&]() {
            esp_ble_gattc_cb_param_t p; p.disconnect.conn_id=4;
            TeslaVehicle::gatt_callback(ESP_GATTC_DISCONNECT_EVT,1,&p); disconnected=true;
        };
        on_gatt_close=[&]() {
            if(reversed) { disconnect(); }
            esp_ble_gattc_cb_param_t p; p.close.conn_id=4;
            TeslaVehicle::gatt_callback(ESP_GATTC_CLOSE_EVT,1,&p);
            if(!delayed && !reversed) { disconnect(); }
            else if(delayed) { on_delay=[&]() { on_delay={}; disconnect(); }; }
        };
        module.disconnect_internal(); on_delay={};
        assert(disconnected); // CLOSE ACK alone is not physical retirement proof.
        module.process_module_events(); ready(r->connection_generation.load()); queue_pair();
        module.process_disconnect(); // Old CLOSE/DISCONNECT must not select B.
        assert(r->pair_requested.load());
        module.process_commands(); tick(); assert(last().pair);
        ui_approve_without_main(); module.process_module_events(); assert(module.tesla->state.get("paired")->asBool());
        std::puts("R3 pinned SDK CLOSE then DISCONNECT composition: cleanup acknowledged before successor; no stale cancellation");
    });
}
void ui_r3_reverse_publication(bool terminal_first, bool contended) {
    ui_with_worker_guard([&]() {
        auto *r=module.tesla->runtime_owner.get();
        fs_fault=[](const std::string &s) { return s=="open:/config/tesla_ble_warp_ev_mac.tmp:w"; };
        queue_pair(); module.process_commands(); fs_fault={}; // Failed n, no pending protocol command
        if(terminal_first) { module.process_module_events(); }
        module.request_soc(true); tick();
        if(last().hello) { inject(peer.hello(last())); tick(); }
        inject(peer.response(last(),{awake,sizeof(awake)},2)); tick();
        assert(last().hello && last().domain==Domain::Infotainment);
        inject(peer.hello(last())); tick(); inject(peer.response(last(),{soc_bytes(),sizeof(controller_fixture::soc)}));
        if(contended) {
            std::atomic<bool> held{false},release{false};
            std::thread worker([&]() { tesla_warp::CheckedMutex::Guard lock(r->command_outcome.mutex); held=true; while(!release.load()) { std::this_thread::yield(); } });
            while(!held.load()) { std::this_thread::yield(); }
            on_failed_trylock=[&]() { release=true; worker.join(); };
            module.process_module_events(); on_failed_trylock={};
        } else { module.process_module_events(); }
        module.process_module_events();
        assert(ev.applied==1 && module.tesla->state.get("paired")->asBool());
        assert(module.tesla->state.get("last_error")->asString().length()==0);
        assert(!module.tesla->state.get("pairing_pending")->asBool());
        std::puts("R3 reverse Failed/SoC order and contention: newest authenticated revision wins state/error; ownership ACK retained");
    });
}
void ui_r3_sequence() {
    for(unsigned kind=0;kind<5;++kind) {
        TeslaBleCommandOutcome outcome;
        const TeslaBleIdentity identity{1,2,1};
        auto command=outcome.accept_pair(identity); assert(command);
        assert(outcome.claim_pair()); outcome.pair_sent(command);
        outcome.sequence=UINT64_MAX;
        if(kind==0) { outcome.publish(TeslaBleCommandOutcome::Kind::Error,false); }
        if(kind==1) { outcome.publish_approval({}); }
        if(kind==2) { assert(outcome.next_order()==0); }
        if(kind==3) { outcome.pair_approved(command,{}); }
        if(kind==4) { outcome.cancel_pair(command,"full",false,true,false,[](){}); }
        assert(outcome.sequence==UINT64_MAX && outcome.publication_exhausted());
        assert(!outcome.forward_available() && !outcome.accept_pair(identity));
        assert(!outcome.take()); assert(!outcome.take_pair_state([](auto){return true;})->completed);
    }
    TeslaBleSession session; TeslaBleIdentity identity{1,2,1}; session.publish(identity); session.bind(1); session.connect();
    session.sequence=UINT64_MAX-1;
    assert(session.arm(1,identity,2).request==UINT64_MAX);
    assert(session.arm(1,identity,2).request==0 && session.sequence==UINT64_MAX);
    session.current.connection=UINT64_MAX; session.disconnect(); session.connect();
    assert(session.connection()==UINT64_MAX && !session.connected);
    auto *r=module.tesla->runtime_owner.get();
    r->command_outcome.sequence=UINT64_MAX-1;
    module.post_event(TeslaVehicle::EventType::KeyPresence,nullptr,1);
    module.post_event(TeslaVehicle::EventType::Error,"must not wrap");
    assert(r->command_outcome.sequence==UINT64_MAX);
    module.loop();
    assert(r->stop_requested.load() && module.tesla->state.get("restart_required")->asBool());
    std::puts("R3 publication sequence exhaustion: no wrapped generic order, runtime fails closed");
}
