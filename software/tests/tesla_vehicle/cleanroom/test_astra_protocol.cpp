// SPDX-License-Identifier: Apache-2.0
// Exact review probes retained separately, with product-pass assertions here.
#define main astra_confirmation_main
#include "astra/protocol_probe_confirm.cpp"
#undef main
int main()
{
    for(const char *mode:{"replay-vcsec","replay-info","update-vcsec","update-info"}) {
        char program[]="test_astra_protocol";
        char *argv[]={program,const_cast<char *>(mode)};
        assert(astra_confirmation_main(2,argv)==0);
    }
    return 0;
}
