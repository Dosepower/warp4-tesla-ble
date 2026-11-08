#pragma once
#include "remote_binding_store.h"
#ifndef TESLA_BLE_MODULE_HOST_TEST
#include <nvs.h>
// Only dedicated application namespace; owner-phone/Tesla/global NVS untouched.
struct RemoteBindingBackend {
 static int read(void *,uint8_t *out,size_t expected){
  nvs_handle_t handle;
  esp_err_t rc=nvs_open("warp_remote",NVS_READONLY,&handle);
  if(rc==ESP_ERR_NVS_NOT_FOUND)return 0;
  if(rc!=ESP_OK)return -1;
  size_t size=expected;rc=nvs_get_blob(handle,"bindings_v1",out,&size);nvs_close(handle);
  if(rc==ESP_ERR_NVS_NOT_FOUND)return 0;
  return rc==ESP_OK&&size==expected?1:-1;
 }
 static bool write(void *,const uint8_t *data,size_t size){
  nvs_handle_t handle;if(nvs_open("warp_remote",NVS_READWRITE,&handle)!=ESP_OK)return false;
  bool ok=nvs_set_blob(handle,"bindings_v1",data,size)==ESP_OK&&nvs_commit(handle)==ESP_OK;
  nvs_close(handle);return ok;
 }
};
#else
// Host-only storage boundary. Native collection fault tests inject their own backend.
#include <vector>
struct RemoteBindingBackend {
 static inline std::vector<uint8_t> disk;
 static inline std::vector<uint8_t> snapshots[4];
 static inline unsigned writes=0,fail_write_at=0;
 static inline bool fail_after_write=false,read_poisoned=false;
 static int read(void *,uint8_t *out,size_t expected){
  if(read_poisoned)return -1;
  if(disk.empty())return 0;if(disk.size()!=expected)return -1;
  memcpy(out,disk.data(),expected);return 1;
 }
 static bool write(void *,const uint8_t *data,size_t size){
  bool fail=++writes==fail_write_at;if(fail&&!fail_after_write)return false;
  disk.assign(data,data+size);if(size>4&&data[4]&&data[4]<4)snapshots[data[4]]=disk;if(fail)read_poisoned=true;return true;
 }
};
#endif
