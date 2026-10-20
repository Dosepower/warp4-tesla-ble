// SPDX-License-Identifier: Apache-2.0
// Actual target Config/WString and operational UUID functions via external harness.
#include "target_oom_definitions.inc"
static bool inject=false;
static unsigned call=0,failure=0;
extern "C" void *__real_realloc(void *,size_t);
extern "C" void *__wrap_realloc(void *p,size_t n) { if(inject && ++call==failure) { return nullptr; } return __real_realloc(p,n); }
int main() {
    const char *old="Prior diagnostic text preserved across allocation refusal";
    const char *next="Independent replacement allocation regression verifies production checked Config mutation";
    Config baseline; assert(baseline.updateStringChecked(old));
    inject=true; call=failure=0; assert(baseline.updateStringChecked(next)); inject=false; const auto count=call;
    assert(count>0);
    for(unsigned i=1;i<=count;++i) {
        Config c; assert(c.updateStringChecked(old)); c.value.updated=0;
        inject=true; failure=i; call=0; const bool ok=c.updateStringChecked(next); inject=false;
        assert(!ok && call>=i && c.slot.text==old && !c.value.updated);
        assert(c.updateStringChecked(next) && c.slot.text==next && c.value.updated);
    }
    baseline.value.updated=0; inject=true; call=0; failure=1;
    assert(baseline.updateStringChecked(next) && !call && !baseline.value.updated);
    assert(baseline.updateStringChecked("") && !call); inject=false;
    for(unsigned kind=0;kind<3;++kind) {
        const char *text=kind==0?"00000211-b2d1-43f0-9b88-960cebf8b91e":kind==1?"00000212-b2d1-43f0-9b88-960cebf8b91e":"00000213-b2d1-43f0-9b88-960cebf8b91e";
        const auto expected=*BLEUUID(String(text)).getNative();
        inject=true; call=0; failure=1; const auto actual=operational_uuid(kind); inject=false;
        assert(!call && actual.len==expected.len && !std::memcmp(actual.uuid.uuid128,expected.uuid.uuid128,16));
    }
    std::printf("Independent production WString/Config: %u realloc faults, retained old value/flags, unchanged/empty no allocation, retry and three allocation-free operational UUIDs passed\n",count);
}
