// SPDX-License-Identifier: Apache-2.0
#ifdef TESLA_BLE_MODULE_POSIX_TEST
#define TESLA_BLE_R6_POSIX_INCLUDE
#include "test_warp_module_posix.cpp"
#else
#define main unused_r6_module_main
#include "test_warp_module.cpp"
#endif
#undef main
int main(int argc,char **argv) {
#ifdef TESLA_BLE_MODULE_POSIX_TEST
 std::filesystem::remove_all(TESLA_CLEAN_STORAGE_PREFIX);
 std::filesystem::create_directories(TESLA_CLEAN_STORAGE_PREFIX "/config");
 r6_unknown_probe=[](){fail_close=true;};
#endif
 const unsigned scenario=argc>1?std::strtoul(argv[1],nullptr,10):5;
 assert(scenario<=11);
 {TeslaBleModuleTest t(1,true);t.ui_r6_run(scenario);}
#ifdef TESLA_BLE_MODULE_POSIX_TEST
 std::filesystem::remove_all(TESLA_CLEAN_STORAGE_PREFIX);
#endif
}
