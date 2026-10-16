// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace tesla_clean {
struct Bytes {
    const uint8_t *data = nullptr;
    size_t size = 0;
    bool valid() const { return data != nullptr || size == 0; }
};
class Metadata {
public:
    bool add(uint8_t tag, Bytes value);
    bool integer(uint8_t tag, uint32_t value);
    bool finish(Bytes payload = {});
    bool sha256(Bytes payload, std::array<uint8_t,32> &output);
    Bytes bytes() const { return {storage_.data(), length_}; }
private:
    std::array<uint8_t,512> storage_{};
    size_t length_ = 0;
    int previous_ = -1;
    bool finalized_ = false;
    bool failed_ = false;
};
}
