// SPDX-License-Identifier: Apache-2.0
#include "controller_fixture.hpp"
#include <cstdio>
#include "../../../src/modules/ev/ev_session_generation.h"
using namespace tesla_clean;
template<class Tag,typename Tag::type member> struct CounterAccess { friend typename Tag::type counter_member(Tag) { return member; } };
struct AuthSerial { using type=uint64_t DomainAuthentication::*; friend type counter_member(AuthSerial); };
template struct CounterAccess<AuthSerial,&DomainAuthentication::serial_>;
int main() {
 uint64_t generation=UINT64_MAX-1,mac=1; int index=2; bool retired=false;
 assert(ev_advance_session(generation,retired,index,mac) && generation==UINT64_MAX);
 for(unsigned i=0;i<4;++i) { assert(!ev_advance_session(generation,retired,index,mac) && retired && generation==UINT64_MAX && index==-1 && mac==0); }
 const uint8_t vin[]="5YJ3E1EA7KF000001";
 DomainAuthentication auth(Domain::Vcsec,{vin,17});
 auth.*counter_member(AuthSerial{})=UINT64_MAX-1;
 auth.reset(); assert(auth.exhausted());
 for(unsigned i=0;i<4;++i) { auth.reset(); assert(auth.exhausted() && (auth.*counter_member(AuthSerial{}))==UINT64_MAX && !auth.ready() && !auth.pending()); }
 std::puts("R4 actual EV checked transition and domain-auth reset: UINT64_MAX boundary, repeated calls, invalidation and no wrap passed");
}
