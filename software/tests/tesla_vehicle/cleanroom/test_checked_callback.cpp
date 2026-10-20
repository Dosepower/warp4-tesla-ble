// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/support.hpp>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace tesla_clean;
struct Heap {
    size_t calls=0, fail=0, live=0;
    static void *get(void *c,size_t n) { auto &h=*static_cast<Heap *>(c); if (++h.calls==h.fail) { return nullptr; } void *p=std::malloc(n); if(p) { ++h.live; } return p; }
    static void put(void *c,void *p) { if(p) { --static_cast<Heap *>(c)->live; std::free(p); } }
    Allocation allocation() { return {get,put,this}; }
};
int main()
{
    Heap heap; CheckedCallback<int(int)> cb;
    assert(cb.assign([](int n) { return n+1; },heap.allocation()));
    assert(cb && cb(4)==5 && heap.live==1);
    heap.fail=heap.calls+1;
    assert(!cb.assign([](int n) { return n+9; },heap.allocation()));
    assert(cb(4)==5 && heap.live==1); // failed replacement retains prior registration
    heap.fail=0;
    assert(cb.assign([](int n) { return n*2; },heap.allocation()));
    assert(cb(4)==8 && heap.live==1);
    cb.clear(); assert(!cb && heap.live==0);
    // A move-only owned capture stays alive while nested clearing/replacement occurs.
    auto capture=std::make_unique<int>(37);
    assert(cb.assign([&,owned=std::move(capture)](int n) {
        cb.clear(); assert(heap.live==1 && *owned==37);
        assert(cb.assign([](int m) { return m*3; },heap.allocation()));
        assert(cb(n)==n*3 && *owned==37 && heap.live==2);
        return *owned+n;
    },heap.allocation()));
    assert(cb(5)==42 && cb(5)==15 && heap.live==1);
    CheckedCallback<int(int)> moved=std::move(cb);
    assert(!cb && moved(7)==21); moved.clear(); assert(heap.live==0);
    assert(cb(3)==0); // an intentionally empty optional callback is safe
    CheckedCallback<void()> empty; empty();
    Allocation invalid{}; invalid.allocate=nullptr;
    assert(!empty.assign([] {},invalid));
    assert(heap.live==0);
    std::puts("Clean checked callbacks: failure retention, move-only captures, nested lifetime, clear and empty invocation passed");
}
