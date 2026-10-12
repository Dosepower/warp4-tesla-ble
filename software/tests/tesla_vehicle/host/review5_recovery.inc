// Protocol evidence: vehicle-command a4b43c1eff0e09d77deb9f2dce97031141fe8c8a,
// pkg/protocol/protocol.md:711-731; verifier.go:52-58,247-273;
// native.go:76-87. HMAC covers the payload/challenge, NOT the outer fault/domain.
struct RecoveryStorage final : StorageAdapter {
    std::array<uint8_t, 2048> private_key{}; size_t private_length = 0;
    std::array<uint8_t, 100> session{}; size_t session_length = 0;
    const char *session_name = nullptr;
    unsigned saves = 0, removes = 0; bool refuse = false;
    LoadResult load_into(const char *name, uint8_t *out, size_t cap, size_t &n) override {
        n = 0; const uint8_t *data = nullptr; size_t length = 0;
        if (!std::strcmp(name, "private_key")) { data = private_key.data(); length = private_length; }
        else if (session_name && !std::strcmp(name, session_name)) { data = session.data(); length = session_length; }
        if (!length) { return LoadResult::Absent; }
        if (length > cap) { return LoadResult::Error; }
        std::copy_n(data, length, out); n = length; return LoadResult::Loaded;
    }
    bool load(const std::string &, std::vector<uint8_t> &) override { return false; }
    bool save(const std::string &, const std::vector<uint8_t> &) override { return false; }
    bool remove(const std::string &) override { ++removes; return false; }
    bool remove_key(const char *) override { ++removes; return false; }
    SaveResult save_transaction(const char *name, const uint8_t *bytes, size_t n) override {
        ++saves;
        if (refuse) { return SaveResult::NotCommitted; }
        assert(n <= session.size()); session_name = name;
        std::copy_n(bytes, n, session.data()); session_length = n; return SaveResult::Committed;
    }
};
static void recovery_controls() {
    constexpr auto vcsec = UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY;
    for (auto d : {domain, vcsec}) {
        for (auto fault : {UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_INCORRECT_EPOCH,
                           UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_INVALID_SIGNATURE}) {
            AuthenticationFixture f(d); RecoveryStorage disk;
            assert(f.c->get_peer(d == domain ? vcsec : domain)->update_session(&f.info) == 0);
            f.v->storage_adapter_ = std::shared_ptr<StorageAdapter>(std::shared_ptr<StorageAdapter>{}, &disk);
            assert(f.c->get_private_key(disk.private_key.data(), disk.private_key.size(), &disk.private_length) == 0);
            auto cmd = f.enqueue(); cmd->state = CommandState::READY; f.v->loop();
            assert(f.tx.writes == 1 && cmd->state == CommandState::WAITING_FOR_RESPONSE && cmd->request_uuid_size == 16);
            // Only enqueue/process_ready ran: no explicit auth request was built.
            const RequestState old(*f.c, *f.p);
            assert(f.p->validate_response_counter(42));
            auto *other = f.c->get_peer(d == domain ? vcsec : domain);
            const auto other_counter = other->get_counter(), other_clock = other->get_clock_time();
            std::array<uint8_t, 16> old_epoch{}, old_key{};
            std::copy_n(f.p->get_epoch(), old_epoch.size(), old_epoch.data());
            std::copy_n(f.p->get_shared_secret(), old_key.size(), old_key.data());
            const auto old_clock = f.p->get_clock_time();
            f.info.epoch[0] = 2;
            // A rebooted Vehicle must actually load this saved payload; the legacy
            // loader compares its clock field to system time. No load mock is used.
            f.info.clock_time = static_cast<uint32_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
            const auto valid = recovery_response(f, *cmd.get(), fault);
            auto unchanged = [&]() {
                assert(f.completions == 0 && cmd->state == CommandState::WAITING_FOR_RESPONSE && cmd->request_uuid_size == 16);
                assert(disk.saves == 0 && disk.removes == 0 && disk.session_length == 0 && old.equals(*f.c, *f.p));
                assert(f.p->is_valid() && f.p->get_clock_time() == old_clock);
                assert(!std::memcmp(f.p->get_epoch(), old_epoch.data(), 16));
                assert(!std::memcmp(f.p->get_shared_secret(), old_key.data(), 16));
                assert(!f.p->validate_response_counter(42));
                assert(other->get_counter() == other_counter && other->get_clock_time() == other_clock && other->get_epoch()[0] == 1);
            };
            for (unsigned kind = 0; kind < 9; ++kind) {
                auto bad = valid;
                if (kind == 0) { bad.sub_sigData.signature_data.sig_type.session_info_tag.tag.bytes[0] ^= 1; }
                if (kind == 1) { bad.request_uuid.bytes[0] ^= 1; }
                if (kind == 2) { bad.from_destination.sub_destination.domain = d == domain ? vcsec : domain; }
                if (kind == 3) { bad.request_uuid.size = 0; }
                if (kind == 4) { bad.which_sub_sigData = 0; }
                if (kind == 5) { bad.has_from_destination = false; }
                if (kind == 6) { bad.payload.session_info.size = 1; bad.payload.session_info.bytes[0] = 0xff; }
                if (kind == 7) { bad.signedMessageStatus.signed_message_fault = UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_BUSY; }
                if (kind == 8) {
                    // Independently valid HMAC, but stale authenticated same-epoch clock.
                    const auto current = f.info; f.info.epoch[0] = 1; f.info.clock_time = old_clock - 1;
                    bad = recovery_response(f, *cmd.get(), fault); f.info = current;
                }
                public_receive(f, bad); unchanged();
            }
            // RX admission itself rejects an aged live command; don't run timers
            // first or replace the UUID, which would conceal the original defect.
            const auto tx_at = cmd->last_tx_at;
            cmd->last_tx_at -= Vehicle::CLOCK_SYNC_MAX_LATENCY + std::chrono::milliseconds(100);
            f.receive(valid); unchanged(); cmd->last_tx_at = tx_at;
            // Duplicates in the same RX batch must consume one exchange exactly once.
            f.v->on_rx_data(Fixture::frame(valid)); f.v->on_rx_data(Fixture::frame(valid)); f.v->loop();
            assert(disk.saves == 1 && disk.removes == 0 && f.p->get_epoch()[0] == 2 && f.p->is_valid());
            assert(f.completions == 0 && cmd->retry_count == 1 && cmd->state == CommandState::READY && f.tx.writes == 1);
            assert(cmd->request_uuid_size == 0); release_test_backoff(f, cmd); f.v->loop();
            assert(cmd->state == CommandState::WAITING_FOR_RESPONSE && f.tx.writes == 2);
            assert(cmd->request_uuid != old.uuid && cmd->request_uuid_size == 16 && !cmd->force_auth);
            assert(disk.session_length == valid.payload.session_info.size &&
                !std::memcmp(disk.session.data(), valid.payload.session_info.bytes, disk.session_length));
            assert(other->get_counter() == other_counter && other->get_clock_time() == other_clock && other->get_epoch()[0] == 1);
            public_receive(f, valid); assert(disk.saves == 1 && f.completions == 0);
            // Reconstruct actual production Vehicle/Client/Peer from persisted key
            // and SessionInfo bytes (not merely a counted save callback).
            auto restored = Vehicle::create(f.v->ble_adapter_, f.v->storage_adapter_, "5YJ3E1EA7KF000001"); assert(restored);
            auto *restored_peer = restored->client_->get_peer(d);
            assert(restored_peer->is_valid() && restored_peer->get_epoch()[0] == 2 && restored_peer->get_clock_time() == f.info.clock_time);
            assert(!std::memcmp(restored_peer->get_shared_secret(), old_key.data(), old_key.size()));
            // Same-epoch recovery must not roll back the ordinary anti-replay counter.
            const auto before_counter = f.p->get_counter(); f.info.counter = 1; ++f.info.clock_time;
            auto same_epoch = recovery_response(f, *cmd.get(), fault); f.receive(same_epoch);
            assert(disk.saves == 2 && f.p->get_counter() == before_counter && f.p->get_clock_time() == f.info.clock_time);
            assert(cmd->state == CommandState::READY && f.completions == 0);
            f.receive(same_epoch); assert(disk.saves == 2); // single-use before the next transmit
            assert(cmd->retry_count == 2); release_test_backoff(f, cmd);
            f.v->loop(); assert(cmd->state == CommandState::WAITING_FOR_RESPONSE);
            const auto success = vcsec_authenticated(f, *cmd.get(), VCSEC_FromVCSECMessage_commandStatus_tag);
            public_receive(f, success); public_receive(f, success);
            assert(f.completions == 1 && cmd->state == CommandState::COMPLETED && f.v->command_queue_.empty());
            std::printf("R2 recovery domain=%d fault=%d: bad/stale/uncorrelated rejected; one durable new-epoch update, real reload, automatic UUID-rotated retry, same-epoch monotonic counter passed\n", int(d), int(fault));
        }
    }
    for (auto d : {domain, vcsec}) {
        AuthenticationFixture f(d); RecoveryStorage disk; disk.refuse = true;
        f.v->storage_adapter_ = std::shared_ptr<StorageAdapter>(std::shared_ptr<StorageAdapter>{}, &disk);
        auto cmd = f.enqueue(); cmd->state = CommandState::READY; f.v->loop();
        const auto old_counter = f.p->get_counter(); const auto old_clock = f.p->get_clock_time();
        f.info.epoch[0] = 2; ++f.info.clock_time;
        auto reply = recovery_response(f, *cmd.get(), UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_INVALID_SIGNATURE);
        public_receive(f, reply);
        assert(disk.saves == 1 && disk.session_length == 0 && disk.removes == 0);
        assert(f.p->is_valid() && f.p->get_counter() == old_counter && f.p->get_clock_time() == old_clock && f.p->get_epoch()[0] == 1);
        assert(f.completions == 1 && cmd->state == CommandState::FAILED);
        public_receive(f, reply); assert(disk.saves == 1 && f.completions == 1);
        disk.refuse = false; cmd = f.enqueue(); cmd->state = CommandState::READY; f.v->loop();
        public_receive(f, reply); assert(disk.saves == 1); // retired command's HMAC cannot revive
        public_receive(f, recovery_response(f, *cmd.get(), UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_INVALID_SIGNATURE));
        assert(disk.saves == 2 && f.p->get_epoch()[0] == 2 && f.completions == 1);
    }
    std::puts("R2: persistence refusal is transactional and single-use; new ordinary-command recovery retries without peer reset/storage deletion");
}
static void admission_deadline_controls() {
    for (auto d : {domain, UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY}) {
        for (bool expired : {false, true}) {
            AuthenticationFixture f(d); auto cmd = f.enqueue(); f.challenge(cmd);
            const auto reply = f.response(); verify_hmac(f, reply);
            if (expired) { cmd->last_tx_at -= Vehicle::CLOCK_SYNC_MAX_LATENCY + std::chrono::milliseconds(100); }
            const auto before = f.p->get_counter();
            public_receive(f, reply);
            assert(f.disk.saves == (expired ? 0u : 1u) && f.disk.removes == 0 && f.completions == 0);
            if (expired) {
                assert(cmd->state == CommandState::AUTH_RESPONSE_WAITING && f.p->get_counter() == before);
            } else { assert(cmd->state == CommandState::WAITING_FOR_RESPONSE && !cmd->force_auth); }
        }
    }
    std::puts("R3: RX-first SessionInfo admission rejects clock-sync deadline expiry in both domains; fresh explicit responses persist and resume normally");
}
