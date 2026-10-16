// SPDX-License-Identifier: Apache-2.0
// Protocol source: official signatures.proto:87-94, signer.go:48-61, native.go:76-88.
#include <tesla_clean/session_info.hpp>
extern "C" {
#include <mbedtls/constant_time.h>
}
#include <mbedtls/platform_util.h>
#include <cstring>
namespace tesla_clean {
namespace {
class Reader {
public:
    explicit Reader(Bytes input) : input_(input) {}
    size_t remaining() const { return input_.size - position_; }
    bool varint(uint64_t &value)
    {
        value = 0;
        for (unsigned byte = 0; byte < 10; ++byte) {
            if (remaining() == 0) { return false; }
            const uint8_t part = input_.data[position_++];
            if (byte == 9 && part > 1) { return false; }
            value |= uint64_t{part & 127u} << (byte * 7);
            if ((part & 128) == 0) { return true; }
        }
        return false;
    }
    bool block(size_t length, Bytes &output)
    {
        if (length > remaining()) { return false; }
        output = {input_.data + position_,length}; position_ += length;
        return true;
    }
    bool skip(uint8_t wire)
    {
        uint64_t size = 0; Bytes unused;
        if (wire == 0) { return varint(size); }
        if (wire == 1) { return block(8, unused); }
        if (wire == 2) { return varint(size) && size <= remaining() && block(static_cast<size_t>(size), unused); }
        if (wire == 5) { return block(4, unused); }
        return false; // groups and invalid wire types are outside the supported subset
    }
private:
    Bytes input_; size_t position_ = 0;
};
}
bool decode_session_info(Bytes encoded, SessionInfo &output)
{
    output = {};
    if (!encoded.valid() || encoded.size == 0 || encoded.size > 512) { return false; }
    SessionInfo candidate;
    Reader reader(encoded); uint32_t seen = 0;
    while (reader.remaining() != 0) {
        uint64_t key = 0;
        if (!reader.varint(key) || key >> 3 == 0 || key >> 3 > 0x1fffffff) { return false; }
        const uint32_t field = static_cast<uint32_t>(key >> 3);
        const uint8_t wire = static_cast<uint8_t>(key & 7);
        if (field > 6) { if (!reader.skip(wire)) { return false; } continue; }
        const uint32_t bit = uint32_t{1} << field;
        if ((seen & bit) != 0) { return false; }
        seen |= bit;
        if (field == 2 || field == 3) {
            uint64_t length = 0; Bytes bytes;
            const size_t expected = field == 2 ? candidate.remote_public.size() : candidate.epoch.size();
            if (wire != 2 || !reader.varint(length) || length != expected || !reader.block(expected, bytes)) { return false; }
            std::memcpy(field == 2 ? candidate.remote_public.data() : candidate.epoch.data(), bytes.data, bytes.size);
        } else if (field == 4) {
            Bytes clock;
            if (wire != 5 || !reader.block(4, clock)) { return false; }
            candidate.clock = uint32_t{clock.data[0]} | uint32_t{clock.data[1]} << 8 |
                              uint32_t{clock.data[2]} << 16 | uint32_t{clock.data[3]} << 24;
        } else {
            uint64_t value = 0;
            if (wire != 0 || !reader.varint(value) || value > UINT32_MAX) { return false; }
            if (field == 1) { candidate.counter = static_cast<uint32_t>(value); }
            if (field == 5) { candidate.status = static_cast<uint32_t>(value); }
            if (field == 6) { candidate.handle = static_cast<uint32_t>(value); }
        }
    }
    if ((seen & 12) != 12 || candidate.status > 1 || candidate.remote_public[0] != 4) { return false; }
    output = candidate;
    return true;
}
SessionInfoResult authenticate_session_info(P256Key &private_key, Random random, Bytes vin,
                                           Bytes challenge, Bytes encoded, Bytes tag,
                                           SessionInfo &output, std::array<uint8_t,16> &session_key)
{
    output = {}; session_key.fill(0);
    if (!vin.valid() || vin.size != 17 || !challenge.valid() || challenge.size != 16 || !tag.valid() || tag.size != 32) {
        return SessionInfoResult::Rejected;
    }
    SessionInfo candidate;
    if (!decode_session_info(encoded, candidate)) { return SessionInfoResult::Rejected; }
    std::array<uint8_t,16> derived{};
    std::array<uint8_t,32> expected{};
    SessionCrypto crypto;
    const bool authentic = private_key.exchange({candidate.remote_public.data(),candidate.remote_public.size()}, random, derived) &&
                           crypto.set_key({derived.data(),derived.size()}) &&
                           crypto.session_info_tag(vin, challenge, encoded, expected) &&
                           mbedtls_ct_memcmp(expected.data(), tag.data, expected.size()) == 0;
    if (authentic) {
        output = candidate;
        if (candidate.status == 0) { session_key = derived; }
    }
    mbedtls_platform_zeroize(derived.data(), derived.size());
    mbedtls_platform_zeroize(expected.data(), expected.size());
    if (!authentic) { return SessionInfoResult::Rejected; }
    return candidate.status == 0 ? SessionInfoResult::Established : SessionInfoResult::KeyDenied;
}
}
