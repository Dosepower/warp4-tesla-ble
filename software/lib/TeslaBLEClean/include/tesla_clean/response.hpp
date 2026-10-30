// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/session_crypto.hpp>
#include <tesla_clean/replay.hpp>
namespace tesla_clean {
enum class Domain : uint8_t { Vcsec=2, Infotainment=3 };
struct Request {
    std::array<uint8_t,16> uuid{};
    std::array<uint8_t,33> identifier{};
    size_t identifier_length = 0;
    uint64_t context = 0, generation = 0;
    uint32_t started = 0, transmitted = 0, total_ms = 0, response_ms = 0;
    // transmitted is taken before TX start; transmit_elapsed is the duration until
    // the complete frame was accepted. response_ms counts from that completion.
    uint32_t transmit_elapsed = 0;
};
struct Response {
    Domain domain = Domain::Vcsec;
    std::array<uint8_t,16> uuid{};
    bool has_uuid = true; // false only for a VCSEC reply without request_uuid
    uint32_t counter = 0, flags = 0, fault = 0;
    GcmPacket packet;
};
struct VerifiedReply { uint64_t context=0; uint32_t counter=0, fault=0; size_t length=0; };
// Nonsecret gate that refused the latest accept() (diagnostics only, never authority).
enum class ResponseRejection : uint8_t { None, Inactive, Route, Uuid, Deadline, Window, Metadata, Decrypt, Payload, Replay };
bool response_aad(Domain domain, Bytes vin, Bytes identifier, uint32_t counter, uint32_t flags,
                  uint32_t fault, std::array<uint8_t,32> &output);
class ResponseGuard {
public:
    // One instance per established domain/session. Crypto must outlive the guard
    // and must not be rekeyed while that guard remains in use.
    ResponseGuard(const SessionCrypto &crypto, Domain domain, Bytes vin);
    bool arm(const Request &request);
    // Retain this request's replay identity across cancellation/rearm. A distinct
    // UUID/hash establishes a new window; changing caller context is not enough.
    void cancel() { active_ = false; }
    bool pending_request(Request &output) const { output = active_ ? request_ : Request{}; return active_; }
    ResponseRejection last_rejection() const { return rejection_; }
    bool accept(const Response &response, uint32_t now, uint64_t generation,
                std::array<uint8_t,1024> &plaintext, VerifiedReply &reply,
                bool (*validate_payload)(Bytes,void *)=nullptr, void *validation_context=nullptr);
    // Optional bounded, nonreentrant validator runs after GCM and before replay/
    // request publication. Nonzero authenticated faults bypass payload decoding.
private:
    const SessionCrypto &crypto_;
    Domain domain_;
    std::array<uint8_t,17> vin_{};
    bool valid_ = false, active_ = false;
    Request request_;
    ReplayWindow replay_;
    ResponseRejection rejection_ = ResponseRejection::None;
};
}
