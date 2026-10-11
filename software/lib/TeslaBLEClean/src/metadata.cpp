// SPDX-License-Identifier: Apache-2.0
// Protocol source: official internal/authentication/metadata.go (see PROVENANCE.md).
#include <tesla_clean/metadata.hpp>
#include <mbedtls/sha256.h>
#include <cstring>
namespace tesla_clean {
bool Metadata::add(uint8_t tag, Bytes value)
{
    if (failed_ || finalized_ || tag == 255 || tag <= previous_ || !value.valid() ||
        value.size > 255 || value.size + 3 > storage_.size() - length_) {
        failed_ = true;
        return false;
    }
    storage_[length_++] = tag;
    storage_[length_++] = static_cast<uint8_t>(value.size);
    if (value.size != 0) { std::memcpy(storage_.data() + length_, value.data, value.size); }
    length_ += value.size;
    previous_ = tag;
    return true;
}
bool Metadata::integer(uint8_t tag, uint32_t value)
{
    const uint8_t encoded[]{static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
                            static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    return add(tag, {encoded, sizeof(encoded)});
}
bool Metadata::finish(Bytes payload)
{
    if (failed_ || finalized_ || !payload.valid() || payload.size >= storage_.size() - length_) {
        failed_ = true;
        return false;
    }
    storage_[length_++] = 255;
    if (payload.size != 0) { std::memcpy(storage_.data() + length_, payload.data, payload.size); }
    length_ += payload.size;
    finalized_ = true;
    return true;
}
bool Metadata::sha256(Bytes payload, std::array<uint8_t,32> &output)
{
    output.fill(0);
    return finish(payload) && mbedtls_sha256(storage_.data(), length_, output.data(), 0) == 0;
}
}
