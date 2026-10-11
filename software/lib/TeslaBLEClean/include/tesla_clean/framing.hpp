// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/metadata.hpp>
namespace tesla_clean {
struct Frame { std::array<uint8_t,1024> data{}; size_t length=0; };
using EmitFrame = void (*)(void *, const Frame &);
using WriteChunk = bool (*)(void *, Bytes, uint64_t);
using OwnsLink = bool (*)(void *, uint64_t);
class FrameReceiver {
public:
    void reset(uint64_t generation);
    bool receive(Bytes input, uint32_t now, uint64_t generation, EmitFrame emit, void *context);
private:
    std::array<uint8_t,1026> pending_{};
    size_t filled_ = 0;
    size_t wanted_ = 0;
    uint32_t last_byte_ = 0;
    uint64_t generation_ = 0;
    uint64_t revision_ = 0;
};
bool send_frame(Bytes payload, size_t chunk_size, uint64_t generation, WriteChunk write, OwnsLink owns, void *context);
}
