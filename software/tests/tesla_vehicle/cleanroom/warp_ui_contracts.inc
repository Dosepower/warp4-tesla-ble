// SPDX-License-Identifier: Apache-2.0
// Production API -> worker -> authenticated main publication, no physical SDK.
void ui_contract() {
    auto *r=module.tesla->runtime_owner.get();
    module.register_urls();
    String error;
    assert(!module.tesla->state.get("pairing_pending")->asBool());
    api.commands.at("tesla_vehicle/pair")(0,error);
    assert(error.length()==0 && r->pair_requested.load());
    assert(module.tesla->state.get("pairing_pending")->asBool());
    assert(!module.tesla->state.get("pair_request_sent")->asBool());
    // Duplicate HTTP submissions must not enqueue another PRESENT_KEY.
    api.commands.at("tesla_vehicle/pair")(0,error);
    assert(error.length()!=0); error="";
    module.process_commands();
    assert(!module.tesla->state.get("pair_request_sent")->asBool());
    tick(); module.process_module_events();
    assert(last().pair && module.tesla->state.get("pair_request_sent")->asBool());
    assert(!module.tesla->state.get("paired")->asBool() && !r->pair_requested.load());
    // Unknown/raw status is not authentication; waiting is bounded.
    clock_ms.fetch_add(60000); module.process_pair_verification(); module.process_module_events();
    assert(!module.tesla->state.get("pairing_pending")->asBool());
    assert(!module.tesla->state.get("pair_request_sent")->asBool());
    assert(!module.tesla->state.get("paired")->asBool());
    // A stale authenticated approval cannot elevate the UI state.
    module.post_event(TeslaVehicle::EventType::PairApproved,nullptr,NAN,r->pair_request,true,true);
    module.process_module_events(); assert(!module.tesla->state.get("paired")->asBool());
    ready(r->connection_generation.load()); // Deadline now retires old physical/status work.
    pairing(); // Existing end-to-end invalid-tag/denial/valid-proof matrix.
    assert(!module.tesla->state.get("pair_request_sent")->asBool());
    // Local deletion clears local status; it is not a Tesla-side revoke command.
    const auto before=sent.size();
    api.commands.at("tesla_vehicle/forget_key")(0,error); assert(error.length()==0);
    module.process_commands(); module.process_module_events();
    assert(sent.size()==before && !module.tesla->state.get("paired")->asBool());
    assert(module.tesla->state.get("key_presence")->asString()=="absent");
    assert(!module.tesla->state.get("pair_request_sent")->asBool());
    // Restart/config transition refusal remains authoritative, independent of UI.
    module.tesla->runtime_update_requested.store(true);
    api.commands.at("tesla_vehicle/pair")(0,error); assert(error.length()!=0);
    assert(!r->pair_requested.load());
    module.tesla->runtime_update_requested.store(false); error="";
    module.tesla->config.get("enable")->updateBool(false);
    api.commands.at("tesla_vehicle/poll")(0,error); assert(error.length()!=0);
    assert(!r->poll_requested.load());
    module.stop_runtime(); assert(!module.tesla->state.get("pair_request_sent")->asBool());
    std::puts("WARP UI contract: accepted/sent/authenticated/timeout/stale/local-delete/transition states passed");
}
