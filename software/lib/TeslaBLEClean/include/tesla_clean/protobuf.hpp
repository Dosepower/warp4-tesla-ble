// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/metadata.hpp>
#include <cstring>
namespace tesla_clean {
// Bounded ordinary protobuf wire subset: varint/fixed64/bytes/fixed32, no groups.
struct ProtoField { uint32_t number=0; uint8_t wire=0; uint64_t integer=0; Bytes bytes; };
class ProtoReader {
    Bytes input_;
    size_t position_=0;
    bool valid_;
    bool varint(uint64_t &output)
    {
        output=0;
        for(unsigned n=0;n<10;++n) {
            if(position_==input_.size) { return false; }
            const uint8_t part=input_.data[position_++];
            if(n==9 && part>1) { return false; }
            output|=uint64_t{part&127u}<<(7*n);
            if(!(part&128)) { return true; }
        }
        return false;
    }
    bool field(ProtoField &output)
    {
        uint64_t tag=0;
        if(!varint(tag) || !(tag>>3) || (tag>>3)>0x1fffffff) { return false; }
        output.number=static_cast<uint32_t>(tag>>3); output.wire=static_cast<uint8_t>(tag&7);
        if(output.wire==0) { return varint(output.integer); }
        uint64_t size=0;
        if(output.wire==2) { if(!varint(size)) { return false; } }
        else if(output.wire==1) { size=8; }
        else if(output.wire==5) { size=4; }
        else { return false; }
        if(size>input_.size-position_) { return false; }
        output.bytes={input_.data+position_,static_cast<size_t>(size)}; position_+=static_cast<size_t>(size);
        return true;
    }
public:
    explicit ProtoReader(Bytes input) : input_(input),valid_(input.valid() && input.size<=1024) {}
    bool next(ProtoField &output)
    {
        output={};
        if(!valid_ || position_==input_.size) { return false; }
        valid_=field(output); return valid_;
    }
    bool done() const { return valid_ && position_==input_.size; }
};
template<size_t Capacity> class ProtoWriter {
    std::array<uint8_t,Capacity> buffer_{};
    size_t length_=0;
    bool valid_=true;
    bool byte(uint8_t value)
    {
        if(!valid_ || length_==Capacity) { valid_=false; return false; }
        buffer_[length_++]=value; return true;
    }
    bool varint(uint64_t value)
    {
        while(value>127) { if(!byte(static_cast<uint8_t>(value)|128)) { return false; } value>>=7; }
        return byte(static_cast<uint8_t>(value));
    }
public:
    bool block(uint32_t field,Bytes input)
    {
        if(!input.valid() || field==0 || field>0x1fffffff || !varint(uint64_t{field}*8+2) ||
           !varint(input.size) || input.size>Capacity-length_) { valid_=false; return false; }
        if(input.size) { std::memcpy(buffer_.data()+length_,input.data,input.size); }
        length_+=input.size; return true;
    }
    bool integer(uint32_t field,uint32_t value)
    {
        return field>0 && field<=0x1fffffff && varint(uint64_t{field}*8) && varint(value);
    }
    bool fixed32(uint32_t field,uint32_t value)
    {
        if(field==0 || field>0x1fffffff || !varint(uint64_t{field}*8+5)) { return false; }
        for(unsigned n=0;n<4;++n) { if(!byte(static_cast<uint8_t>(value>>(8*n)))) { return false; } }
        return true;
    }
    Bytes bytes() const { return valid_?Bytes{buffer_.data(),length_}:Bytes{}; }
};
}
