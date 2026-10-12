// Fifth Astra review controls. Linked production crypto, no signature mocks.
static UniversalMessage_RoutableMessage vcsec_plain(pb_size_t kind, bool awake = false) {
    UniversalMessage_RoutableMessage msg = UniversalMessage_RoutableMessage_init_default;
    msg.has_from_destination = true;
    msg.from_destination.which_sub_destination = UniversalMessage_Destination_domain_tag;
    msg.from_destination.sub_destination.domain = UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY;
    msg.which_payload = UniversalMessage_RoutableMessage_protobuf_message_as_bytes_tag;
    VCSEC_FromVCSECMessage inner = VCSEC_FromVCSECMessage_init_default;
    inner.which_sub_message = kind;
    if (kind == VCSEC_FromVCSECMessage_vehicleStatus_tag) {
        inner.sub_message.vehicleStatus.vehicleSleepStatus = awake
            ? VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE
            : VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_ASLEEP;
    }
    auto stream = pb_ostream_from_buffer(msg.payload.protobuf_message_as_bytes.bytes,
        sizeof(msg.payload.protobuf_message_as_bytes.bytes));
    assert(pb_encode(&stream, VCSEC_FromVCSECMessage_fields, &inner));
    msg.payload.protobuf_message_as_bytes.size = stream.bytes_written;
    return msg;
}
static UniversalMessage_RoutableMessage vcsec_authenticated(AuthenticationFixture &f, const Command &cmd,
        pb_size_t kind) {
    auto msg = vcsec_plain(kind);
    if (f.d == domain) {
        msg.from_destination.sub_destination.domain = domain;
        CarServer_Response inner = CarServer_Response_init_default;
        inner.has_actionStatus = true; inner.actionStatus.result = CarServer_OperationStatus_E_OPERATIONSTATUS_OK;
        auto stream = pb_ostream_from_buffer(msg.payload.protobuf_message_as_bytes.bytes,
            sizeof(msg.payload.protobuf_message_as_bytes.bytes));
        assert(pb_encode(&stream, CarServer_Response_fields, &inner));
        msg.payload.protobuf_message_as_bytes.size = stream.bytes_written;
    }
    const auto plain = msg.payload.protobuf_message_as_bytes;
    msg.which_sub_sigData = UniversalMessage_RoutableMessage_signature_data_tag;
    msg.request_uuid.size = cmd.request_uuid_size;
    std::copy_n(cmd.request_uuid.data(), cmd.request_uuid_size, msg.request_uuid.bytes);
    auto &sig = msg.sub_sigData.signature_data;
    sig.which_sig_type = Signatures_SignatureData_AES_GCM_Response_data_tag;
    auto &aes = sig.sig_type.AES_GCM_Response_data;
    aes.counter = 1; aes.nonce[0] = 19;
    size_t hs = 0; const auto *hash = f.c->get_last_request_hash(&hs);
    uint8_t md[80], aad[32]; size_t used = 0;
    auto add = [&](uint8_t t, const uint8_t *p, size_t n) {
        md[used++] = t; md[used++] = n; std::memcpy(md + used, p, n); used += n;
    };
    auto num = [&](uint8_t t, uint32_t value) {
        uint8_t b[4] = {uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value)}; add(t, b, 4);
    };
    uint8_t type = Signatures_SignatureType_SIGNATURE_TYPE_AES_GCM_RESPONSE;
    uint8_t d = f.d;
    add(Signatures_Tag_TAG_SIGNATURE_TYPE, &type, 1); add(Signatures_Tag_TAG_DOMAIN, &d, 1);
    add(Signatures_Tag_TAG_PERSONALIZATION, reinterpret_cast<const uint8_t *>("5YJ3E1EA7KF000001"), 17);
    num(Signatures_Tag_TAG_COUNTER, 1); num(Signatures_Tag_TAG_FLAGS, 0);
    add(Signatures_Tag_TAG_REQUEST_HASH, hash, hs); num(Signatures_Tag_TAG_FAULT, 0); md[used++] = Signatures_Tag_TAG_END;
    assert(mbedtls_sha256(md, used, aad, 0) == 0);
    mbedtls_gcm_context g; mbedtls_gcm_init(&g);
    assert(mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, f.p->get_shared_secret(), 128) == 0);
    assert(mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, plain.size, aes.nonce, 12, aad, 32,
        plain.bytes, msg.payload.protobuf_message_as_bytes.bytes, 16, aes.tag) == 0);
    mbedtls_gcm_free(&g); return msg;
}
static void vcsec_terminal_controls() {
    constexpr auto vcsec = UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY;
    for (auto d : {domain, vcsec}) {
        for (bool auth : {false, true}) {
            AuthenticationFixture f(d); auto cmd = f.enqueue();
            if (auth) { f.challenge(cmd); } else { cmd->state = CommandState::READY; f.v->loop(); }
            const auto state = cmd->state;
            for (auto kind : {VCSEC_FromVCSECMessage_nominalError_tag, VCSEC_FromVCSECMessage_commandStatus_tag}) {
                auto msg = vcsec_plain(kind);
                // Even an echoed public UUID is not authentication for an ordinary command.
                msg.request_uuid.size = cmd->request_uuid_size;
                std::copy_n(cmd->request_uuid.data(), cmd->request_uuid_size, msg.request_uuid.bytes);
                public_receive(f, msg);
                assert(f.completions == 0 && f.v->command_queue_.size() == 1 && cmd->state == state);
                assert(f.disk.saves == 0 && f.disk.removes == 0 && f.p->is_valid());
            }
            if (auth) {
                unsigned observations = 0;
                f.v->set_vehicle_status_callback([&](const auto &) { ++observations; });
                public_receive(f, vcsec_plain(VCSEC_FromVCSECMessage_vehicleStatus_tag, true));
                assert(observations == 1 && f.v->sleep_state() == SleepState::AWAKE);
                assert(f.completions == 0 && cmd->state == CommandState::AUTH_RESPONSE_WAITING && f.disk.saves == 0);
                public_receive(f, f.response()); assert(f.disk.saves == 1 && f.completions == 0);
            }
        }
    }
    for (auto kind : {VCSEC_FromVCSECMessage_nominalError_tag, VCSEC_FromVCSECMessage_commandStatus_tag}) {
        AuthenticationFixture f(vcsec); auto cmd = f.enqueue(); cmd->state = CommandState::READY; f.v->loop();
        const auto good = vcsec_authenticated(f, *cmd.get(), kind);
        auto bad = good; bad.request_uuid.bytes[0] ^= 1; public_receive(f, bad);
        bad = good; bad.sub_sigData.signature_data.sig_type.AES_GCM_Response_data.tag[0] ^= 1; public_receive(f, bad);
        assert(f.completions == 0 && cmd->state == CommandState::WAITING_FOR_RESPONSE);
        public_receive(f, good);
        assert(f.completions == 1 && f.v->command_queue_.empty());
        assert(cmd->state == (kind == VCSEC_FromVCSECMessage_nominalError_tag ? CommandState::FAILED : CommandState::COMPLETED));
        public_receive(f, good); assert(f.completions == 1);
    }
    // Explicit legacy compatibility: unsolicited observations, GET_STATUS polls,
    // wake observations, and the present-key pairing command's unsigned status.
    {
        AuthenticationFixture f(vcsec); f.v->vcsec_poll();
        auto cmd = f.v->command_queue_.front(); f.challenge(cmd);
        public_receive(f, vcsec_plain(VCSEC_FromVCSECMessage_vehicleStatus_tag, true));
        // Legacy GET_STATUS observations must not answer the poll's prerequisite
        // session exchange. Only its fresh HMAC may finish authentication.
        assert(cmd->state == CommandState::AUTH_RESPONSE_WAITING && f.disk.saves == 0 && f.v->command_queue_.size() == 1);
        public_receive(f, f.response()); assert(f.disk.saves == 1);
    }
    {
        AuthenticationFixture f(vcsec); unsigned observed = 0;
        f.v->set_vehicle_status_callback([&](const auto &) { ++observed; });
        public_receive(f, vcsec_plain(VCSEC_FromVCSECMessage_vehicleStatus_tag, true));
        assert(observed == 1 && f.v->sleep_state() == SleepState::AWAKE);
        f.v->vcsec_poll(); f.v->loop(); f.v->loop();
        auto cmd = f.v->command_queue_.front(); assert(cmd->state == CommandState::WAITING_FOR_RESPONSE);
        public_receive(f, vcsec_plain(VCSEC_FromVCSECMessage_vehicleStatus_tag, true));
        assert(cmd->state == CommandState::COMPLETED && f.v->command_queue_.empty() && observed == 2);
    }
    {
        AuthenticationFixture f(domain); auto cmd = f.enqueue();
        cmd->current_auth_domain = UniversalMessage_Domain_DOMAIN_BROADCAST;
        f.v->initiate_wake_sequence_(cmd);
        public_receive(f, vcsec_plain(VCSEC_FromVCSECMessage_vehicleStatus_tag, true));
        assert(f.completions == 0 && cmd->current_auth_domain == domain && f.disk.saves == 0);
    }
    {
        AuthenticationFixture f(vcsec); assert(f.v->pair()); f.v->loop(); f.v->loop();
        auto cmd = f.v->command_queue_.front(); assert(cmd->name == "Whitelist Add Key");
        public_receive(f, vcsec_plain(VCSEC_FromVCSECMessage_commandStatus_tag));
        assert(cmd->state == CommandState::COMPLETED && f.v->command_queue_.empty());
    }
    std::puts("R1: plaintext nominal/status cannot terminate ordinary/auth exchanges in either domain; authenticated terminal controls and legacy observation/poll/wake/pairing retained");
}
