#include "modules/tesla_vehicle/tesla_brick_session_policy.h"
#include <cassert>
#include <initializer_list>
int main() {
 assert(tesla_brick_session_allowed("brick",false,false,false,false,false));
 for (const char *type : {"warp", "", "warp4"})
  assert(!tesla_brick_session_allowed(type,false,false,false,false,false));
 assert(!tesla_brick_session_allowed(nullptr,false,false,false,false,false));
 for(unsigned bits=1; bits<32; ++bits)
  assert(!tesla_brick_session_allowed("brick",bits&1,bits&2,bits&4,bits&8,bits&16));
}
