// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/framing.hpp>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tesla_clean;
struct Receiver {
    FrameReceiver receiver;
    unsigned calls=0;
    bool reenter=false;
    Frame last{};
    static void emit(void *context,const Frame &frame) {
        auto &self=*static_cast<Receiver *>(context); self.last=frame; ++self.calls;
        if (self.reenter) {
            self.reenter=false; self.receiver.reset(0x100000000ULL);
            const uint8_t fresh[]{0,3,4};
            assert(self.receiver.receive({fresh,3},102,0x100000000ULL,emit,&self));
        }
    }
};
struct Writer {
    uint64_t current=UINT64_MAX;
    std::array<uint8_t,1026> all{}; size_t length=0;
    unsigned calls=0; bool retire=false,refuse=false;
    static bool owns(void *p,uint64_t g) { return static_cast<Writer *>(p)->current==g; }
    static bool write(void *p,Bytes b,uint64_t generation) {
        auto &w=*static_cast<Writer *>(p); assert(generation==w.current); ++w.calls;
        if (w.refuse) { return false; }
        std::memcpy(w.all.data()+w.length,b.data,b.size); w.length+=b.size;
        if (w.retire) { w.current=0; }
        return true;
    }
};
int main()
{
    Receiver r; r.receiver.reset(UINT64_MAX);
    const uint8_t framed[]{0,3,1,2,3,0,1,9};
    for (size_t split=0; split<=sizeof(framed); ++split) {
        r.calls=0; r.receiver.reset(UINT64_MAX);
        assert(r.receiver.receive({framed,split},100,UINT64_MAX,Receiver::emit,&r));
        assert(r.receiver.receive({framed+split,sizeof(framed)-split},100,UINT64_MAX,Receiver::emit,&r));
        assert(r.calls==2 && r.last.length==1 && r.last.data[0]==9);
    }
    r.receiver.reset(0); const uint8_t partial[]{0,3,1};
    assert(r.receiver.receive({partial,3},UINT32_MAX-20,0,Receiver::emit,&r));
    const auto count=r.calls;
    assert(r.receiver.receive({framed,5},1100,0,Receiver::emit,&r)); // old partial times out through clock wrap
    assert(r.calls==count+1 && r.last.length==3);
    assert(!r.receiver.receive({framed,5},1100,0x100000000ULL,Receiver::emit,&r));
    const uint8_t oversized[]{4,1}; assert(!r.receiver.receive({oversized,2},1100,0,Receiver::emit,&r));
    const uint8_t empty[]{0,0}; assert(!r.receiver.receive({empty,2},1100,0,Receiver::emit,&r));
    assert(!r.receiver.receive({nullptr,1},1100,0,Receiver::emit,&r));
    r.receiver.reset(UINT64_MAX); r.reenter=true;
    assert(!r.receiver.receive({framed,sizeof(framed)},101,UINT64_MAX,Receiver::emit,&r));
    const uint8_t tail[]{5,6}; assert(r.receiver.receive({tail,2},103,0x100000000ULL,Receiver::emit,&r));
    assert(r.last.length==3 && r.last.data[0]==4 && r.last.data[2]==6);
    std::array<uint8_t,1024> maximum{}; maximum.fill(7); Writer w;
    assert(send_frame({maximum.data(),maximum.size()},20,w.current,Writer::write,Writer::owns,&w));
    assert(w.length==1026 && w.calls==52 && w.all[0]==4 && w.all[1]==0);
    r.receiver.reset(w.current);
    assert(r.receiver.receive({w.all.data(),w.length},120,w.current,Receiver::emit,&r));
    assert(r.last.length==1024 && r.last.data[1023]==7);
    Writer stale; stale.retire=true;
    assert(!send_frame({maximum.data(),maximum.size()},20,stale.current,Writer::write,Writer::owns,&stale));
    assert(stale.calls==1); // no old fragment submitted on successor connection
    Writer refusal; refusal.refuse=true;
    assert(!send_frame({maximum.data(),maximum.size()},20,refusal.current,Writer::write,Writer::owns,&refusal));
    assert(refusal.calls==1);
    assert(!send_frame({maximum.data(),1024},0,w.current,Writer::write,Writer::owns,&w));
    assert(!send_frame({maximum.data(),1025},20,w.current,Writer::write,Writer::owns,&w));
    std::puts("Clean framing: all split points, concatenation, maximum frame, timeout/wrap, malformed length, detached reentry, wide/zero generation and TX refusal/retirement passed");
}
