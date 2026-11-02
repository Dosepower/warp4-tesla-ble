#include "modules/tesla_vehicle/tesla_ble_call_dispatch.h"
#include <cassert>
#include <cstring>
struct BoundarySdk {
 struct Addr { unsigned char bytes[7]; };
 struct ConnParams { unsigned char bytes[16]; };
 using GapCb = void(); using AttrCb = void(); using SvcCb = void();
 struct UuidAny { unsigned char bytes[20]; }; using Uuid=UuidAny;
};
int main() {
 using Exec=tesla_ble_dispatch::NimbleExec<BoundarySdk>;
 Exec::Job j{};
 unsigned char input[21]; for(unsigned i=0;i<21;++i) input[i]=i;
 assert(Exec::fill_write(j,1,2,input,20,nullptr,nullptr));
 assert(j.len==20 && std::memcmp(j.data,input,20)==0);
 Exec::Job before=j;
 assert(!Exec::fill_write(j,1,2,input,21,nullptr,nullptr));
 assert(std::memcmp(&j,&before,sizeof(j))==0);
 assert(Exec::fill_write(j,1,2,input,2,nullptr,nullptr));
 assert(!Exec::fill_write(j,1,2,nullptr,1,nullptr,nullptr));
}
