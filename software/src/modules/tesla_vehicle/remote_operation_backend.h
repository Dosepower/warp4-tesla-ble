#pragma once
#include "remote_operation_journal.h"
#ifndef TESLA_BLE_MODULE_HOST_TEST
#include <nvs.h>
struct RemoteOperationBackend {
 static int read(void*,uint8_t*out,size_t expected){nvs_handle_t h;auto rc=nvs_open("warp_remote",NVS_READONLY,&h);if(rc==ESP_ERR_NVS_NOT_FOUND)return 0;if(rc!=ESP_OK)return -1;size_t n=expected;rc=nvs_get_blob(h,"operations_v1",out,&n);nvs_close(h);if(rc==ESP_ERR_NVS_NOT_FOUND)return 0;return rc==ESP_OK&&n==expected?1:-1;}
 static bool write(void*,const uint8_t*p,size_t n){nvs_handle_t h;if(nvs_open("warp_remote",NVS_READWRITE,&h)!=ESP_OK)return false;bool ok=nvs_set_blob(h,"operations_v1",p,n)==ESP_OK&&nvs_commit(h)==ESP_OK;nvs_close(h);return ok;}
};
#else
#include <vector>
#include <functional>
struct RemoteOperationBackend {
 static inline std::vector<uint8_t> disk;
 static inline std::function<void()> after_write;
 static inline unsigned writes=0;static inline bool fail=false,poison=false;
 static int read(void*,uint8_t*out,size_t n){if(poison)return -1;if(disk.empty())return 0;if(disk.size()!=n)return -1;memcpy(out,disk.data(),n);return 1;}
 static bool write(void*,const uint8_t*p,size_t n){writes++;if(fail)return false;disk.assign(p,p+n);if(after_write)after_write();return true;}
};
#endif
