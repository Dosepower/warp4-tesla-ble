// Native NimBLE transport; shared Tesla lifecycle/pairing/crypto contracts retained.
void TeslaVehicle::process_disconnect()
{
    Runtime *r = tesla->runtime_owner.get();
    const auto generation = r->pending_disconnect.take();
    if (generation && *generation == r->session.connection()) {
        // Includes local RX overflow: the link may still physically be open.
        // Retire physical ownership before callbacks or replacement readiness.
        disconnect_internal();
    }
}

bool TeslaVehicle::gatt_operation(GattOperation kind, uint16_t handle, tesla_warp::FunctionRef<bool()> submit)
{
#ifdef CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU
    static_assert(CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU - 3 <= RX_NOTIFICATION_MAX, "largest negotiated notification is admitted");
#endif
    Runtime *r = tesla->runtime_owner.get();
    const auto identity = r->session.identity();
    if (!identity) { return false; } // No submission against unavailable identity.
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if (kind != Close && (!active_identity_matches(*identity) || !runtime_binding_matches())) { return false; }
#endif
    const auto generation = r->connection_generation.load();
    const auto connection = kind == Register || kind == Open ? 0 : r->gatt_connection.load();
    auto &slot = kind == Close ? r->retirement_operation : r->operation;
    const auto gated_submit = [this, identity, kind, &submit]() {
#if TESLA_BLE_MANUAL_VEHICLE_UI
        if (kind != Close && (!active_identity_matches(*identity) || !runtime_binding_matches())) { return false; }
#endif
        return submit();
    };
    // An unanswered ATT write is peer unresponsiveness, not proof that the SDK
    // submission is stuck. SDK GATT timeout is 30s, beyond our unchanged 10s
    // ownership watchdog. After a RETURNED submission, start physical retirement
    // at 7s, leaving 3s for the real DISCONNECT/procedure-drain barrier. Never
    // reset/reuse a timed-out slot or free a cookie on a guessed elapsed delay.
    const auto started = millis();
    bool draining = false, drained = false, cancelled = false;
    const bool ok = slot.run(kind, connection, handle, []() { return millis(); }, gated_submit,
        [this, r, kind, connection, handle, started, generation, identity, &slot, &draining, &drained, &cancelled]() {
            if (kind == Discover && r->discovery_ready.load()) { r->operation.complete(kind, connection, 0, true); }
            if ((kind == Write || kind == Open) && (cancelled || millis() - started >= 7000)) {
                const auto event = r->transport_cancel_event.exchange(0);
#ifndef TESLA_BLE_MODULE_HOST_TEST
                logger.printfln("BLE drain: epoch=%u attempt=%u opgen=%u stage=%u reason=%u elapsed=%u rxpeak=%u rxof=%u rxsvc=%u defof=%u",
                    unsigned(r->epoch), unsigned(r->pair_command), unsigned(slot.generation()), unsigned(kind),
                    unsigned((event >> 32) == slot.generation() ? event & 0xff : cancelled ? 3 : 4), unsigned(millis() - started),
                    unsigned(r->rx_peak.load()), unsigned(r->rx_overflows.load()), unsigned(r->rx_write_serviced),
                    unsigned(r->vehicle ? r->vehicle->deferred_overflows() : 0));
#else
                (void)event;
#endif
                draining = true;
                r->closing_link.store(true);
                auto *c = bluetooth.central();
                if (c) { if (kind == Open) { c->cancel_open(); } else { c->close(); } }
                // Keep the old operation watch active while host/main run. A
                // missing terminal callback or blocked close still quarantines
                // at the original watchdog, retaining all reachable ownership.
                while (c && !c->idle() && millis() - started <= TeslaBleOperation::TIMEOUT_MS) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                drained = c && c->idle();
#ifndef TESLA_BLE_MODULE_HOST_TEST
                logger.printfln("BLE drain end: epoch=%u attempt=%u opgen=%u drained=%u elapsed=%u",
                    unsigned(r->epoch), unsigned(r->pair_command), unsigned(slot.generation()), unsigned(drained), unsigned(millis() - started));
#endif
                if (drained) {
                    r->link_open.store(false);
                    slot.complete(kind, connection, handle, false);
                } else {
                    // A late WRITE success must not substitute for the missing
                    // terminal barrier, even when main did not sample the wait.
                    slot.supervise(millis());
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            // The worker owns RX while its own framed TX waits for the ATT
            // response; without this the queue is unserved for the whole WRITE.
            if (kind == Write && !draining && !cancelled) { service_rx_during_write(generation, *identity); }
        }, [this, r, identity, generation, kind, connection, handle, &slot, &cancelled]() {
            // A completed SDK DISCONNECT has already drained the old procedure
            // and our terminal handler. Fail the write without poisoning a
            // reusable slot merely because that same event changed session ID.
            auto *c = bluetooth.central();
            if ((kind == Write || kind == Open) && c && c->idle()) {
                slot.complete(kind, connection, handle, false);
                return TeslaBleCancellation::Proceed;
            }
#if TESLA_BLE_MANUAL_VEHICLE_UI
            if (kind != Close && !active_identity_matches(*identity)) { cancelled = true; }
#endif
            if (r->stop_requested.load() || (kind != Close && r->connection_generation.load() != generation)) {
                cancelled = true;
            }
            const auto latest = r->session.identity();
            if (latest && latest != identity) { cancelled = true; }
            // Cancellation is not expiry. Keep the original operation watch
            // running until the REAL SDK/procedure/application terminal barrier
            // drains. No slot reset, new cookie, or successor admission here.
            if (cancelled) { return kind == Write || kind == Open ? TeslaBleCancellation::Defer : TeslaBleCancellation::Cancel; }
            return !latest ? TeslaBleCancellation::Defer : TeslaBleCancellation::Proceed;
        });
    if (draining) {
        r->closing_link.store(false);
        if (drained && !slot.retired()) {
            // Logical retirement runs on the worker only, after run() has
            // finished its watch. Vehicle will reject this incomplete frame.
            // OPEN never sent a pairing frame. Let its caller publish the
            // connection failure, not a misleading possible-transmission loss.
            disconnect_internal(kind != Open);
            return false;
        }
    }
    if (slot.retired()) {
        post_event(EventType::Error, "BLE operation cancelled/timed out; restart required");
        // A cancelled WRITE must first retire its physical stream using a
        // separate, never-reused CLOSE slot. Other work is not admissible.
        if (kind != Write) { r->stop_requested.store(true); }
    }
    return ok;
}

bool TeslaVehicle::ensure_connected()
{
 Runtime *r=tesla->runtime_owner.get();process_disconnect();
 auto *c=bluetooth.central();if(!c||r->stop_requested.load()||!runtime_binding_matches())return false;
 if(r->connected.load()&&r->link_open.load())return true;
 disconnect_internal(false);if(r->stop_requested.load())return false;
 const auto generation=r->session.connection();r->physical_generation.store(generation);
 if(!gatt_operation(Open,0,[&](){return c->open(r->remote_address,r->remote_address_type,generation);}))return false;
 if(!gatt_operation(Discover,0,[&](){return c->discover();}))return false;
 if(!gatt_operation(Descriptor,0,[&](){return c->subscribe();})||!r->link_open.load())return false;
 if(!r->session.connect(generation,[&](){r->write_handle=c->tx();r->notify_handle=c->rx();r->connected=true;}))return false;
 if(!r->vehicle||!r->vehicle->open(generation)){disconnect_internal();return false;}
 if(r->stop_requested.load()||r->session.connection()!=generation||!r->connected.load()){r->vehicle->close(r->vehicle->generation());return false;}
 post_event(EventType::Connected);return true;
}
void TeslaVehicle::disconnect_internal(bool cancel_pair)
{
    Runtime *r = tesla->runtime_owner.get();
    if (r == nullptr) { return; }
    r->command_outcome.begin_worker_cleanup();
    const bool interrupted_poll = r->in_flight_started_ms != 0;
    // Invalidate approval/submission before close callbacks, but expose terminal
    // cancellation only after all old stream cleanup has finished.
    const auto cancelled = cancel_pair ? r->command_outcome.retire_pair() : 0;
    const uint64_t retired = r->vehicle ? r->vehicle->generation() : 0;
    r->session.disconnect();
    r->connected.store(false);
    r->notify_handle.store(0);
    r->write_handle = 0;
    r->advance_connection();
    r->policy.on_transport_disconnected();
    r->in_flight_started_ms = 0;
    if (interrupted_poll) { r->manual_poll_pending.store(false); }
    xQueueReset(r->queues.rx());
    // At stop, host shutdown owns the physical link; do not wait for a callback
    // after admission has closed. Never submit any IDF work from the main loop.
    if (r->link_open.exchange(false) && !r->stop_requested.load()) {
        r->close_events.store(0);
        r->closing_link.store(true);
        if (!gatt_operation(Close, 0, [this]() {
                auto *c=bluetooth.central(); return c && c->close();
            })) { r->stop_requested.store(true); }
        r->closing_link.store(false);
    }
    // Finish old physical-link cleanup before exposing completion reentry.
    // Otherwise an old unwind can exchange/close the newly connected link.
    r->pair_sending = false; r->pair_verifying = false; r->pair_status_active = false;
    r->pair_approval_waiting.store(false);
    if (r->operation.retired()) { r->stop_requested.store(true); }
    if (r->vehicle) { r->vehicle->close(retired); }
    if (cancelled) {
        cancel_pairing("Pairing link/session lost; transmission may have occurred; approval not authenticated", cancelled,
            r->operation.retired() || r->retirement_operation.retired() ? TeslaBleCommandOutcome::PairOutcome::InternalFailure : TeslaBleCommandOutcome::PairOutcome::LinkLost);
    }
    r->pending_disconnect.take(); // No old stream token may survive retirement.
    r->command_outcome.end_cleanup();
}

bool TeslaVehicle::transport_owns(uint64_t generation) const
{
    const Runtime *r = tesla->runtime_owner.get();
    if (!r || r->stop_requested.load() || !r->connected.load() || !r->link_open.load() ||
        generation != r->connection_generation.load() || generation != r->physical_generation.load()) { return false; }
    const auto identity = r->session.identity();
#if TESLA_BLE_MANUAL_VEHICLE_UI
    if (!identity || !active_identity_matches(*identity)) { return false; }
#endif
    return identity && *identity == r->observed_identity;
}

bool TeslaVehicle::transport_recover_frame(uint64_t generation)
{
    // Never assert a peer frame reset on the same physical link.
    Runtime *r = tesla->runtime_owner.get();
    if (r && generation == r->physical_generation.load()) { disconnect_internal(); }
    return false;
}

bool TeslaVehicle::transport_write_bytes(const uint8_t *data, size_t size)
{
    Runtime *r = tesla->runtime_owner.get();
    if (!r || !r->connected.load() || !r->write_handle) { return false; }
    const auto generation = r->connection_generation.load();
    // Default ATT MTU 23: no optimistic MTU assignment from Arduino setMTU.
    return tesla_ble_write_fragments(size, 20, [r, generation]() {
        const auto identity = r->session.identity();
        return r->stop_requested.load() || r->command_outcome.forgetting() || !r->link_open.load() || !identity ||
               r->connection_generation.load() != generation ||
               *identity != r->observed_identity ||
               (r->in_flight_started_ms != 0 && !r->session.matches(r->in_flight));
    }, [this, r, &data](size_t offset, size_t length) {
        const bool pairing=r->pair_sending;
        if(pairing && r->pair_tx_attempted.load()==0 && length>=2) {
            r->pair_tx_total.store(uint32_t(data[offset])*256+data[offset+1]+2);
        }
        const bool ok=gatt_operation(Write, r->write_handle, [this, r, pairing, &data, offset, length]() {
            if(pairing)r->pair_tx_attempted.store(r->pair_tx_attempted.load()+length);
            auto *c=bluetooth.central(); return c && c->write(data+offset,length);
        });
        // Conservative lower bound: only count a successful matching ATT
        // completion plus ownership checks. Refusal is NOT proof of no TX.
        if(pairing && ok)r->pair_tx_acked.store(r->pair_tx_acked.load()+length);
        return ok;
    });
}

void TeslaVehicle::gatt_callback(const TeslaNimbleEvent &e)
{
 auto *owner=gatt_owner.load();if(!owner||!owner->tesla)return;
 auto lease=owner->tesla->callback_gate.enter();if(!lease)return;
 Runtime *r=owner->tesla->runtime_owner.get();if(!r||r->stop_requested.load()||!e.epoch||e.epoch!=r->physical_generation.load())return;
 const auto conn=r->gatt_connection.load();
 if(e.kind==TeslaNimbleEvent::Open){if(!e.status){r->gatt_connection=e.connection;r->link_open=true;}r->operation.complete(Open,0,0,e.status==0);return;}
 if(e.connection!=conn)return;
 if(e.kind==TeslaNimbleEvent::Discover){r->operation.complete(Discover,conn,0,e.status==0);return;}
 if(e.kind==TeslaNimbleEvent::Descriptor){r->operation.complete(Descriptor,conn,0,e.status==0);return;}
 if(e.kind==TeslaNimbleEvent::Write){r->operation.complete(Write,conn,e.handle,e.status==0);return;}
 if(e.kind==TeslaNimbleEvent::Security){
  r->transport_cancel_event.store((uint64_t{r->operation.generation()} << 32) | 1);
  r->command_outcome.begin_cleanup();r->connected=false;r->pending_disconnect.store(r->session.disconnect());r->advance_connection();return;
 }
 if(e.kind==TeslaNimbleEvent::Disconnected){
  if(r->closing_link.load()){r->retirement_operation.complete(Close,conn,0,true);return;}
  if(!r->link_open.load()&&!r->connected.load())return;
  r->command_outcome.begin_cleanup();const auto generation=r->session.disconnect();r->connected=false;r->link_open=false;r->notify_handle=0;r->advance_connection();r->pending_disconnect.store(generation);owner->post_event(EventType::Disconnected);return;
 }
 if(e.kind==TeslaNimbleEvent::Rx){
  if(!r->connected.load()||!r->link_open.load()||e.handle!=r->notify_handle.load()||!e.data||!e.size||e.size>RX_NOTIFICATION_MAX)return;
  uint32_t peak=r->rx_peak.load();
  const bool queued=enqueue_rx(r->queues.rx(),e.epoch,e.data,e.size,&peak);
  if(peak>r->rx_peak.load())r->rx_peak.store(peak); // host task is the only writer while connected
  if(!queued){const auto overflows=r->rx_overflows.load();if(overflows!=UINT32_MAX)r->rx_overflows.store(overflows+1);r->transport_cancel_event.store((uint64_t{r->operation.generation()} << 32) | 2);r->command_outcome.begin_cleanup();r->connected=false;r->pending_disconnect.store(r->session.disconnect());r->advance_connection();}
 }
}
