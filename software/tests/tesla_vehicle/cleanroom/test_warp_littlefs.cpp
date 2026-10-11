// SPDX-License-Identifier: Apache-2.0
// Clean WARP storage tested against the retained external real LittleFS fixture.
#include "../../../src/modules/tesla_vehicle/tesla_ble_storage.h"
#include "../../../src/modules/tesla_vehicle/tesla_ble_storage_io.h"
#include <cassert>
#include <cstdio>
extern "C" {
void backend_setup(); void backend_teardown(); void backend_fail_close(bool);
int backend_open(const char *,int); int backend_close(int); long backend_read(int,void *,size_t);
long backend_write(int,const void *,size_t); int backend_fsync(int); int backend_fstat(int,struct stat *);
int backend_stat(const char *,struct stat *); int backend_rename(const char *,const char *); int backend_unlink(const char *);
unsigned backend_private_fds(); unsigned backend_public_fds(); unsigned backend_open_files();
}
static unsigned io=0;
extern "C" int __wrap_open(const char *p,int f,...) { ++io; return backend_open(p,f); }
extern "C" int __wrap_close(int f) { ++io; return backend_close(f); }
extern "C" ssize_t __wrap_read(int f,void *p,size_t n) { ++io; return backend_read(f,p,n); }
extern "C" ssize_t __wrap_write(int f,const void *p,size_t n) { ++io; return backend_write(f,p,n); }
extern "C" int __wrap_fsync(int f) { ++io; return backend_fsync(f); }
extern "C" int __wrap_fstat(int f,struct stat *s) { ++io; return backend_fstat(f,s); }
extern "C" int __wrap_stat(const char *p,struct stat *s) { ++io; return backend_stat(p,s); }
extern "C" int __wrap_rename(const char *p,const char *q) { ++io; return backend_rename(p,q); }
extern "C" int __wrap_unlink(const char *p) { ++io; return backend_unlink(p); }
int main() {
    using namespace tesla_clean; backend_setup(); TeslaBleStorageAdapter storage;
    const uint8_t before[]={42},after[]={73};
    assert(storage.save_transaction("private_key",before,1)==SaveResult::Committed);
    backend_fail_close(true);
    assert(storage.save_transaction("private_key",after,1)==SaveResult::RestartRequired);
    assert(storage.restart_required() && backend_private_fds()==1 && backend_public_fds()==0 && backend_open_files()==0);
    const auto boundary=io; backend_fail_close(false); TeslaBleStorageAdapter recreated;
    uint8_t erased[16]; std::memset(erased,0xA5,sizeof(erased)); size_t size=99;
    assert(recreated.load_into("private_key",erased,sizeof(erased),size)==LoadResult::RestartRequired && !size);
    for(auto byte:erased) { assert(!byte); }
    assert(recreated.save_transaction("private_key",after,1)==SaveResult::RestartRequired);
    assert(!recreated.remove_key("private_key") && !recreated.forget_material() && io==boundary);
    const int fd=backend_open("/data/config/tesla_ble_private_key",O_RDONLY); assert(fd>=0);
    uint8_t observed=0; assert(backend_read(fd,&observed,1)==1 && observed==42 && backend_close(fd)==0);
    backend_teardown();
    std::puts("Clean WARP real LittleFS-close: original commit retained, one orphan/private wrapper, public FD consumed, quarantine across recreation, erased failed load and no subsequent application IO passed; no power-loss guarantee");
}
