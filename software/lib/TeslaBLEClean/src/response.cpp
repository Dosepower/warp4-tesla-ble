// SPDX-License-Identifier: Apache-2.0
// Protocol sources: official peer.go:82-117, signer.go:219-241, window.go.
#include <tesla_clean/response.hpp>
#include <cstring>
namespace tesla_clean {
namespace {
bool known_domain(Domain domain) { return domain == Domain::Vcsec || domain == Domain::Infotainment; }
bool known_identifier(Domain domain, Bytes identifier)
{
    if (!identifier.valid() || !known_domain(domain) || (identifier.size != 17 && identifier.size != 33)) { return false; }
    if (identifier.data[0] == 5) { return identifier.size == 17; }
    return identifier.data[0] == 8 && identifier.size == (domain == Domain::Vcsec ? 17u : 33u);
}
}
bool response_aad(Domain domain, Bytes vin, Bytes identifier, uint32_t counter, uint32_t flags,
                  uint32_t fault, std::array<uint8_t,32> &output)
{
    output.fill(0);
    if (!known_identifier(domain, identifier)) { return false; }
    Metadata metadata;
    const uint8_t method = 9, destination = static_cast<uint8_t>(domain);
    return metadata.add(0, {&method,1}) && metadata.add(1, {&destination,1}) && metadata.add(2, vin) &&
           metadata.integer(5, counter) && metadata.integer(7, flags) && metadata.add(8, identifier) &&
           metadata.integer(9, fault) && metadata.sha256({}, output);
}
ResponseGuard::ResponseGuard(const SessionCrypto &crypto, Domain domain, Bytes vin) : crypto_(crypto), domain_(domain)
{
    valid_ = known_domain(domain) && vin.valid() && vin.size == vin_.size();
    if (valid_) { std::memcpy(vin_.data(), vin.data, vin_.size()); }
}
bool ResponseGuard::arm(const Request &request)
{
    if (!valid_ || active_ || request.context == 0 || request.total_ms == 0 || request.response_ms == 0 ||
        request.total_ms > INT32_MAX || request.response_ms > INT32_MAX || request.transmit_elapsed > INT32_MAX ||
        request.transmitted - request.started >= request.total_ms ||
        !known_identifier(domain_, {request.identifier.data(),request.identifier_length})) { return false; }
    if (request.uuid != request_.uuid || request.identifier_length != request_.identifier_length ||
        std::memcmp(request.identifier.data(), request_.identifier.data(), request.identifier_length) != 0) {
        replay_.reset(); // Official response counters are unique per request, not per session.
    }
    request_ = request;
    active_ = true;
    return true;
}
bool ResponseGuard::accept(const Response &response, uint32_t now, uint64_t generation,
                           std::array<uint8_t,1024> &plaintext, VerifiedReply &reply,
                           bool (*validate_payload)(Bytes,void *), void *validation_context)
{
    plaintext.fill(0); reply = {};
    // A missing request_uuid is admissible only for VCSEC; correlation then rests on the
    // routing address, this single armed request and the request hash in the GCM AAD.
    if (!active_ || generation != request_.generation || response.domain != domain_ ||
        (response.has_uuid ? response.uuid != request_.uuid : domain_ != Domain::Vcsec) ||
        now - request_.started >= request_.total_ms ||
        now - request_.transmitted >= request_.transmit_elapsed + request_.response_ms) { return false; }
    std::array<uint8_t,32> aad{};
    size_t length = 0;
    if (!response_aad(domain_, {vin_.data(),vin_.size()}, {request_.identifier.data(),request_.identifier_length},
                      response.counter, response.flags, response.fault, aad) ||
        !crypto_.decrypt(response.packet, {aad.data(),aad.size()}, plaintext, length) ||
        (response.fault==0 && validate_payload && !validate_payload({plaintext.data(),length},validation_context)) ||
        !replay_.admit(response.counter)) {
        plaintext.fill(0);
        return false;
    }
    reply = {request_.context, response.counter, response.fault, length};
    cancel(); // detach before publication; only this UUID/hash retains its replay history
    return true;
}
}
