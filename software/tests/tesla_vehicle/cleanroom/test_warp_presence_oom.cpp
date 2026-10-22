// SPDX-License-Identifier: Apache-2.0
#include "target_oom_definitions.inc"
static bool inject=false;
static unsigned calls=0;
extern "C" void *__real_realloc(void *,size_t);
extern "C" void *__wrap_realloc(void *p,size_t n) { if(inject) { ++calls; return nullptr; } return __real_realloc(p,n); }
struct Layout : String { static constexpr unsigned sso_size=SSOSIZE; };
int main() {
 static_assert(Layout::sso_size==15); static_assert(sizeof(String)==16);
 const char *tokens[]={"unknown","present","absent","cleanup"};
 for(unsigned site=0;site<3;++site) for(unsigned before=0;before<4;++before) for(unsigned value=0;value<4;++value) {
  Config c; assert(c.updateStringChecked(tokens[before])); c.value.updated=0;
  inject=true; calls=0;
  presence_site(c,value==1,value!=0,value==3);
  StaticJsonDocument<96> doc;
  doc.to<JsonObject>(); doc["key_presence"] = "";
  PresenceVisitor visitor{doc["key_presence"]}; visitor(c.slot);
  char json[96]{}; serializeJson(doc,json,sizeof(json));
  inject=false;
  assert(c.slot.text==tokens[value] && calls==0);
  assert(!doc.overflowed() && std::strlen(c.slot.text.c_str())<=7);
  assert(std::strcmp(doc["key_presence"].as<const char*>(),tokens[value])==0);
  assert(bool(c.value.updated)==(before!=value));
  // Serialize the actual resulting Config string, not fake Config data.
  std::printf("R4_PRESENCE_JSON %s\n",json);
 }
 std::puts("R4 ESP32 SSO layout + unchanged Config updateString/update_value/checked path: all presence transitions allocation-free under total realloc refusal");
}
