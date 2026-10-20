// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/support.hpp>
#include <cassert>
#include <cstdio>
using namespace tesla_clean;
struct Backend {
    bool create_ok=true, lock_ok=true; unsigned creates=0, locks=0, unlocks=0, destroys=0; uint32_t timeout=0;
    static void *create(void *c) { auto &b=*static_cast<Backend *>(c); ++b.creates; return b.create_ok?c:nullptr; }
    static bool lock(void *c,void *h,uint32_t t) { assert(c==h); auto &b=*static_cast<Backend *>(c); ++b.locks; b.timeout=t; return b.lock_ok; }
    static void unlock(void *c,void *h) { assert(c==h); ++static_cast<Backend *>(c)->unlocks; }
    static void destroy(void *c,void *h) { assert(c==h); ++static_cast<Backend *>(c)->destroys; }
    MutexOps ops() { return {create,lock,unlock,destroy,this}; }
};
int main()
{
    Backend b;
    {
        CheckedMutex m;
        assert(!m.ready() && !m.lock(3));
        auto invalid=b.ops(); invalid.unlock=nullptr;
        assert(!m.initialize(invalid) && b.creates==0);
        b.create_ok=false; assert(!m.initialize(b.ops()) && !m.ready());
        b.create_ok=true; assert(m.initialize(b.ops()) && m.ready());
        assert(!m.initialize(b.ops()) && b.creates==2);
        b.lock_ok=false; assert(!m.lock(11) && b.timeout==11); m.unlock(); assert(b.unlocks==0);
        b.lock_ok=true; assert(m.lock(17) && b.timeout==17);
        assert(!m.lock(0) && b.locks==2); // wrapper never silently recursively locks
        m.unlock(); m.unlock(); assert(b.unlocks==1);
        assert(m.lock(19));
    }
    assert(b.unlocks==2 && b.destroys==1);
    std::puts("Clean checked mutex: invalid/refused initialization, retry, failed lock, balanced release and destruction passed");
}
