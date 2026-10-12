// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/metadata.hpp>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tesla_clean;
int main()
{
    const uint8_t signature[]{5}, domain[]{2};
    const uint8_t epoch[]{0xaa,0xda,0x92,0x8a,0x4f,0x21,0x5f,0x55,0xf9,0xe6,0xe4,0x5e,0x66,0xb6,0x52,0x1e};
    const uint8_t expected[]{0xab,0xab,0x04,0xd8,0x04,0x49,0x98,0x13,0x38,0x2e,0xfd,0x74,0xa0,0x67,0x91,0xce,0x2d,0xe7,0x77,0x43,0x96,0x03,0x24,0x6d,0xfb,0xaa,0x83,0x92,0xca,0x05,0x86,0x8e};
    Metadata m;
    assert(m.add(0,{signature,1})); assert(m.add(1,{domain,1}));
    assert(m.add(2,{reinterpret_cast<const uint8_t *>("testVIN"),7}));
    assert(m.add(3,{epoch,16})); assert(m.integer(4,0xe74)); assert(m.integer(5,0x53a));
    std::array<uint8_t,32> hash{};
    assert(m.sha256({},hash)); assert(std::memcmp(hash.data(),expected,32)==0);
    assert(m.bytes().size==46); assert(m.bytes().data[45]==255);
    assert(!m.add(6,{domain,1})); assert(!m.sha256({},hash)); // finalized once
    Metadata invalid;
    assert(invalid.add(2,{domain,1})); assert(!invalid.add(1,{domain,1}));
    assert(!invalid.sha256({},hash)); // poison, do not sign a partial transcript
    Metadata duplicate;
    assert(duplicate.integer(5,8)); assert(!duplicate.integer(5,9));
    Metadata oversized;
    std::array<uint8_t,256> long_field{};
    assert(!oversized.add(2,{long_field.data(),long_field.size()}));
    Metadata null_input; assert(!null_input.add(0,{nullptr,1}));
    Metadata capacity;
    assert(capacity.add(0,{long_field.data(),255}));
    assert(!capacity.add(1,{long_field.data(),255})); // bounded 512-byte transcript
    Metadata payload;
    assert(payload.add(0,{signature,1}));
    assert(payload.finish({domain,1}));
    const uint8_t transcript[]{0,1,5,255,2};
    assert(payload.bytes().size==sizeof(transcript));
    assert(std::memcmp(payload.bytes().data,transcript,sizeof(transcript))==0);
    std::puts("Clean metadata: official SHA256 vector, ordered TLV, big endian, terminal, bounds/refusal passed");
}
