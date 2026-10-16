// SPDX-License-Identifier: Apache-2.0
// Actual HTTP callbacks, worker/controller, main publication; no physical SDK.
void ui_acceptance_interleaving(bool error_outcome) {
    auto *r=module.runtime_owner.get(); module.register_urls();
    module.post_event(error_outcome ? TeslaVehicle::EventType::Error : TeslaVehicle::EventType::KeyPresence,
                      error_outcome ? "older unrelated error" : nullptr, 1);
    String error;
    api.commands.at("tesla_vehicle/pair")(0,error); assert(error.length()==0);
    module.process_commands(); assert(!r->pair_requested.load()); // consumed, not transmitted
    module.process_module_events(); // delayed older KeyLoaded/Error
    assert(module.state.get("pairing_pending")->asBool());
    api.commands.at("tesla_vehicle/pair")(0,error); assert(error.length()!=0);
    assert(!r->pair_requested.load());
    tick(); module.process_module_events(); assert(last().pair);
    // Later unrelated diagnostics must not terminate the accepted pairing either.
    module.post_event(TeslaVehicle::EventType::Error,"unrelated poll error");
    module.process_module_events(); assert(module.state.get("pairing_pending")->asBool());
    clock_ms.fetch_add(60001); module.process_pair_verification(); module.process_module_events();
    const auto before=sent.size(); module.process_commands(); tick(); module.process_module_events();
    assert(sent.size()==before && !r->pair_requested.load());
    assert(!module.state.get("pairing_pending")->asBool());
    std::puts("UI delayed key/error acceptance ownership: duplicate never retained or enrolled");
}
void ui_delete_interleaving(bool deferred_poll) {
    auto *r=module.runtime_owner.get(); module.register_urls(); String error;
    if(deferred_poll) { module.request_soc(true); assert(r->in_flight_started_ms); }
    api.commands.at("tesla_vehicle/pair")(0,error); assert(error.length()==0);
    if(deferred_poll) { module.process_commands(); assert(r->pair_requested.load()); }
    api.commands.at("tesla_vehicle/forget_key")(0,error); assert(error.length()==0);
    // Admission after delete acceptance is backend-refused, not only UI-disabled.
    String competing;
    api.commands.at("tesla_vehicle/pair")(0,competing); assert(competing.length()!=0);
    competing=""; api.commands.at("tesla_vehicle/poll")(0,competing); assert(competing.length()!=0);
    const auto before=sent.size(); module.process_commands(); tick();
    assert(sent.size()==before && !r->pair_requested.load() && !r->poll_requested.load());
    assert(r->storage->private_key_presence()==TeslaBleStorageAdapter::KeyPresence::Absent);
    IdentityLease absent; assert(!r->keys->acquire(absent));
    assert(!r->bound_vehicle_mac.load() && !r->in_flight_started_ms);
    assert(!r->pair_sending && !r->pair_verifying && !r->pair_status_active);
    competing=""; api.commands.at("tesla_vehicle/pair")(0,competing); assert(competing.length()!=0);
    module.process_module_events();
    assert(!module.state.get("pairing_pending")->asBool() && !module.state.get("pair_request_sent")->asBool());
    for(unsigned i=0;i<3;++i) { module.process_commands(); tick(); }
    assert(sent.size()==before && r->storage->private_key_presence()==TeslaBleStorageAdapter::KeyPresence::Absent);
    // No retained enrollment: only a fresh explicit request may create a key.
    ready(r->connection_generation.load()); error="";
    api.commands.at("tesla_vehicle/pair")(0,error); assert(error.length()==0);
    module.process_commands(); tick(); assert(last().pair && sent.size()==before+1);
    std::puts("UI delete barrier: forward intents cancelled, absent material/binding, no retained transmission");
}
void ui_retirement_interleaving(bool started, bool slac, bool consumed=true) {
    auto *r=module.runtime_owner.get(); module.register_urls(); String error;
    const auto original=*r->session.identity();
    api.commands.at("tesla_vehicle/pair")(0,error); assert(error.length()==0);
    if(consumed) { module.process_commands(); }
    if(started) { tick(); module.process_module_events(); assert(module.state.get("pair_request_sent")->asBool()); }
    if(slac) {
        ++ev.generation; ev.index=-1; ev.mac=0; module.loop(); module.update_active_vehicle();
    } else {
        esp_ble_gattc_cb_param_t p; p.disconnect.conn_id=4;
        TeslaVehicle::gatt_callback(ESP_GATTC_DISCONNECT_EVT,1,&p); module.process_disconnect();
        ++ev.generation; ev.index=-1; ev.mac=0; module.loop();
    }
    clock_ms.fetch_add(60001); module.process_pair_verification(); module.process_module_events();
    assert(!module.state.get("pairing_pending")->asBool() && !module.state.get("pair_request_sent")->asBool());
    assert(!module.state.get("paired")->asBool() && module.state.get("last_error")->asString().length()!=0);
    const auto before=sent.size(); module.process_commands(); tick(); assert(sent.size()==before);
    error=""; api.commands.at("tesla_vehicle/pair")(0,error); assert(error.length()!=0);
    assert(!r->pair_requested.load());
    ev.generation=original.generation+2; ev.index=original.index; ev.mac=original.mac;
    module.loop(); module.update_active_vehicle(); ready(r->connection_generation.load());
    queue_pair(); module.process_commands(); tick(); assert(last().pair && sent.size()==before+1);
    std::puts("UI link/SLAC retirement: terminal cancellation, no false approval or deferred enrollment");
}
void ui_silent_cancel(bool link_only) {
    auto *r=module.runtime_owner.get(); queue_pair(); bool changed=false;
    fs_fault=[&](const std::string &op) {
        if(op.rfind("rename:",0)==0 && !changed) {
            changed=true;
            if(link_only) { r->connected.store(false); }
            else { auto identity=*r->session.identity(); ++identity.generation; r->session.publish(identity); }
        }
        return false;
    };
    module.process_commands(); fs_fault={}; assert(changed);
    module.process_module_events(); clock_ms.fetch_add(60001); module.process_pair_verification();
    assert(sent.empty() && !module.state.get("pairing_pending")->asBool());
    assert(!module.state.get("pair_request_sent")->asBool() && !module.state.get("paired")->asBool());
    assert(module.state.get("last_error")->asString().length()!=0);
    std::puts("UI post-commit silent exit now publishes terminal cancellation without transmission");
}
void ui_delete_during(bool writing) {
    auto *r=module.runtime_owner.get(); queue_pair(); unsigned barriers=0,chunks=0;
    const auto deletion=[&]() {
        String error; api.commands.at("tesla_vehicle/forget_key")(0,error);
        assert(error.length()==0); ++barriers;
        String forward; api.commands.at("tesla_vehicle/pair")(0,forward); assert(forward.length()!=0);
        forward=""; api.commands.at("tesla_vehicle/poll")(0,forward); assert(forward.length()!=0);
    };
    if(writing) {
        on_gatt_bytes=[&](const uint8_t *,size_t) { ++chunks; if(!barriers) { deletion(); } };
    } else {
        fs_fault=[&](const std::string &op) { if(op.rfind("rename:",0)==0 && !barriers) { deletion(); } return false; };
    }
    module.process_commands(); if(writing) { tick(); }
    fs_fault={}; assert(barriers==1 && chunks==unsigned(writing));
    module.process_commands(); tick(); module.process_module_events();
    assert(!r->pair_requested.load() && !r->poll_requested.load() && !r->bound_vehicle_mac.load());
    assert(r->storage->private_key_presence()==TeslaBleStorageAdapter::KeyPresence::Absent);
    IdentityLease absent; assert(!r->keys->acquire(absent));
    assert(!module.state.get("pairing_pending")->asBool() && !module.state.get("pair_request_sent")->asBool());
    assert(sent.empty()); // first partial chunk, if any, preceded deletion acceptance
    for(unsigned i=0;i<3;++i) { module.process_commands(); tick(); }
    assert(chunks==unsigned(writing) && sent.empty());
    std::puts("UI overlapping storage/write deletion: barrier aborts forward progress; no later chunks or recreated key");
}
void ui_status_deadline() {
    auto *r=module.runtime_owner.get(); queue_pair(); module.process_commands(); tick(); module.process_module_events();
    clock_ms.fetch_add(1000); module.process_pair_verification(); assert(r->pair_status_active);
    clock_ms.fetch_add(60001); module.process_pair_verification(); module.process_module_events();
    assert(!module.state.get("pairing_pending")->asBool() && !r->pair_status_active);
    assert(!module.state.get("paired")->asBool() && module.state.get("last_error")->asString().length()!=0);
    // A late cancellation of the prior command cannot clear a successor intent.
    const auto previous=r->pair_command; queue_pair();
    module.cancel_pairing("late prior cancellation",previous);
    assert(r->pair_requested.load());
    std::puts("UI active verification deadline and command-specific late cancellation passed");
}
void ui_retirement_handoff() {
    auto *r=module.runtime_owner.get(); queue_pair(); module.process_commands(); tick();
    unsigned closed=0;
    on_gatt_close=[&]() {
        ++closed; module.process_module_events();
        assert(module.state.get("pairing_pending")->asBool());
        String duplicate; api.commands.at("tesla_vehicle/pair")(0,duplicate); assert(duplicate.length()!=0);
        esp_ble_gattc_cb_param_t p; p.close.conn_id=4;
        TeslaVehicle::gatt_callback(ESP_GATTC_CLOSE_EVT,1,&p);
        p.disconnect.conn_id=4; TeslaVehicle::gatt_callback(ESP_GATTC_DISCONNECT_EVT,1,&p);
    };
    module.disconnect_internal(); assert(closed==1 && !r->pair_requested.load());
    module.process_module_events(); assert(!module.state.get("pairing_pending")->asBool());
    ready(r->connection_generation.load()); queue_pair(); const auto before=sent.size();
    module.process_commands(); tick(); assert(sent.size()==before+1 && last().pair);
    std::puts("UI retirement handoff: no successor admission during close/unwind, exactly one fresh retry");
}
