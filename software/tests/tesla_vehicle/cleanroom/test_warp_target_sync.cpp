// SPDX-License-Identifier: Apache-2.0
// Actual module paths + real CheckedMutex ESP semaphore branch. Host semaphores
// and OS/SDK remain substitutes: NOT target/FreeRTOS or race proof.
#define main target_module_main
#include "test_warp_module.cpp"
#undef main
int main(int argc,char **argv) {
 char mode[]="r5-delete-held-close";char *default_args[]={argv[0],mode};
 return target_module_main(argc==1?2:argc,argc==1?default_args:argv);
}
