// SPDX-License-Identifier: Apache-2.0
// Protocol source: official pkg/connector/ble/ble.go:67-127.
#include <tesla_clean/framing.hpp>
#include <algorithm>
#include <cstring>
namespace tesla_clean {
void FrameReceiver::reset(uint64_t generation)
{
    generation_ = generation;
    if (revision_ == UINT64_MAX) { exhausted_ = true; }
    else { ++revision_; }
    filled_ = wanted_ = 0;
    last_byte_ = 0;
}
bool FrameReceiver::receive(Bytes input, uint32_t now, uint64_t generation, EmitFrame emit, void *context)
{
    if (revision_ == UINT64_MAX) { exhausted_ = true; }
    if (exhausted_ || !input.valid() || input.size > 8208 || generation != generation_ || emit == nullptr) { return false; }
    const uint64_t invocation = ++revision_;
    if (filled_ != 0 && now - last_byte_ > 1000) { filled_ = wanted_ = 0; }
    if (input.size != 0) { last_byte_ = now; }
    for (size_t index = 0; index < input.size; ++index) {
        pending_[filled_++] = input.data[index];
        if (filled_ == 2) {
            wanted_ = size_t{pending_[0]} * 256 + pending_[1];
            if (wanted_ == 0 || wanted_ > 1024) { filled_ = wanted_ = 0; return false; }
        }
        if (wanted_ != 0 && filled_ == wanted_ + 2) {
            Frame detached;
            detached.length = wanted_;
            std::memcpy(detached.data.data(), pending_.data() + 2, wanted_);
            filled_ = wanted_ = 0;
            emit(context, detached);
            // A reentrant receiver/reset owns its new partial frame, even with the same link token.
            if (exhausted_ || generation != generation_ || invocation != revision_) { return false; }
        }
    }
    return true;
}
bool send_frame(Bytes payload, size_t chunk_size, uint64_t generation, WriteChunk write, OwnsLink owns, void *context)
{
    if (!payload.valid() || payload.size == 0 || payload.size > 1024 || chunk_size == 0 ||
        write == nullptr || owns == nullptr) { return false; }
    std::array<uint8_t,1026> wire{};
    wire[0] = static_cast<uint8_t>(payload.size >> 8);
    wire[1] = static_cast<uint8_t>(payload.size);
    std::memcpy(wire.data() + 2, payload.data, payload.size);
    const size_t length = payload.size + 2;
    for (size_t offset = 0; offset < length;) {
        const size_t count = std::min(chunk_size, length - offset);
        if (!owns(context, generation) || !write(context, {wire.data() + offset,count}, generation) ||
            !owns(context, generation)) { return false; }
        offset += count;
    }
    return true;
}
}
