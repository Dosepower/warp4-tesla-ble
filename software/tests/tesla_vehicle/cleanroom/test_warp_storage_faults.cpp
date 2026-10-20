// SPDX-License-Identifier: Apache-2.0
// Real POSIX production path with syscall fault injection. No filesystem secret read.
#include "../../../src/modules/tesla_vehicle/tesla_ble_storage.h"
#include "../../../src/modules/tesla_vehicle/tesla_ble_storage_io.h"
#include <filesystem>
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <sys/wait.h>
static unsigned calls=0,fail_at=0;
static bool active=false;
enum class IoMode { Ordinary, ShortWrite, ShortRead, CorruptRead, EofRead, InterruptedRead };
static bool interrupted=false;
static IoMode mode=IoMode::Ordinary;
static bool fault() { return active && ++calls==fail_at; }
extern "C" int __real_open(const char *,int,...);
extern "C" int __real_fstat(int,struct stat *);
extern "C" ssize_t __real_read(int,void *,size_t);
extern "C" ssize_t __real_write(int,const void *,size_t);
extern "C" int __real_fsync(int);
extern "C" int __real_close(int);
extern "C" int __real_stat(const char *,struct stat *);
extern "C" int __real_rename(const char *,const char *);
extern "C" int __real_unlink(const char *);
extern "C" int __wrap_open(const char *p,int f,...) {
    if(fault()) { errno=ENOMEM; return -1; }
    return __real_open(p,f,0600);
}
extern "C" int __wrap_fstat(int f,struct stat *s) { if(fault()) { errno=EIO; return -1; } return __real_fstat(f,s); }
extern "C" ssize_t __wrap_read(int f,void *p,size_t n) {
    if(fault()) { errno=EIO; return -1; }
    if(active && mode==IoMode::EofRead) { return 0; }
    if(active && mode==IoMode::InterruptedRead && !interrupted) { interrupted=true; errno=EINTR; return -1; }
    const auto result=__real_read(f,p,active && mode==IoMode::ShortRead && n>1?1:n);
    if(active && mode==IoMode::CorruptRead && result>0) { static_cast<uint8_t *>(p)[0]^=1; }
    return result;
}
extern "C" ssize_t __wrap_write(int f,const void *p,size_t n) {
    if(fault()) { errno=EIO; return -1; }
    return __real_write(f,p,active && mode==IoMode::ShortWrite && n>1?1:n);
}
extern "C" int __wrap_fsync(int f) { if(fault()) { errno=EIO; return -1; } return __real_fsync(f); }
extern "C" int __wrap_close(int f) {
    // Public descriptor is consumed even on a reported error, as on the target.
    // This is not a model of esp_littlefs private-cache cleanup/power failure.
    const bool failed=fault(); int rc=__real_close(f); if(failed) { errno=EIO; return -1; } return rc;
}
extern "C" int __wrap_stat(const char *p,struct stat *s) { if(fault()) { errno=EIO; return -1; } return __real_stat(p,s); }
extern "C" int __wrap_rename(const char *a,const char *b) { if(fault()) { errno=EIO; return -1; } return __real_rename(a,b); }
extern "C" int __wrap_unlink(const char *p) { if(fault()) { errno=EIO; return -1; } return __real_unlink(p); }
static std::array<uint8_t,4096> old{},next{};
static size_t material_size=6;
static const char *key="warp_ev_mac",*path="/config/tesla_ble_warp_ev_mac";
static void seed(TeslaBleStorageAdapter &s,bool backup) {
    assert(s.forget_material());
    assert(tesla_clean::KeyStorage::committed(s.save_transaction(key,old.data(),material_size)));
    if(backup) {
        TeslaBleStorageIO::Paths p; assert(p.initialize(path));
        assert(__real_rename(p.canonical,p.backup)==0);
    }
}
static void verify_old_or_committed(TeslaBleStorageAdapter &s,tesla_clean::SaveResult result) {
    if(s.restart_required()) { assert(result==tesla_clean::SaveResult::RestartRequired); return; }
    uint8_t out[4096]{}; size_t size=0;
    assert(s.load_into(key,out,sizeof(out),size)==tesla_clean::LoadResult::Loaded && size==material_size);
    assert(!std::memcmp(out,tesla_clean::KeyStorage::committed(result)?next.data():old.data(),material_size));
}
int main() {
    std::filesystem::create_directories(TESLA_CLEAN_STORAGE_PREFIX "/config");
    old.fill(42); next.fill(73);
    TeslaBleStorageAdapter storage; unsigned cases=0,cancellations=0;
    const char *keys[]={"private_key","session_vcsec","session_infotainment","warp_ev_mac"};
    const char *paths[]={"/config/tesla_ble_private_key","/config/tesla_ble_session_vcsec","/config/tesla_ble_session_info","/config/tesla_ble_warp_ev_mac"};
    for(unsigned k=0;k<4;++k) for(size_t bytes:{size_t{1},size_t{6},size_t{65},size_t{129},size_t{4096}}) for(bool backup:{false,true}) {
        key=keys[k]; path=paths[k]; material_size=bytes;
        seed(storage,backup); calls=0; fail_at=0; active=true;
        const auto result=storage.save_transaction(key,next.data(),material_size); active=false;
        assert(result==tesla_clean::SaveResult::Committed); const unsigned boundaries=calls;
        for(unsigned n=1;n<=boundaries;++n) {
            seed(storage,backup); const pid_t pid=fork(); assert(pid>=0);
            if(!pid) {
                calls=0; fail_at=n; active=true; auto r=storage.save_transaction(key,next.data(),material_size); active=false;
                verify_old_or_committed(storage,r);
                if(storage.restart_required()) {
                    assert(storage.save_transaction(key,next.data(),material_size)==tesla_clean::SaveResult::RestartRequired);
                    assert(!storage.forget_material());
                } else {
                    assert(tesla_clean::KeyStorage::committed(storage.save_transaction(key,next.data(),material_size)));
                }
                _exit(0);
            }
            int status=0; assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0); ++cases;
        }
        seed(storage,backup); unsigned guards=0;
        assert(storage.set_mutation_guard([&]() { ++guards; return true; }));
        assert(storage.save_transaction(key,next.data(),material_size)==tesla_clean::SaveResult::Committed);
        const auto checks=guards;
        assert(storage.set_mutation_guard([]() { return true; }));
        for(unsigned n=0;n<checks;++n) {
            seed(storage,backup); guards=0;
            assert(storage.set_mutation_guard([&]() { return guards++<n; }));
            const auto r=storage.save_transaction(key,next.data(),material_size);
            assert(r!=tesla_clean::SaveResult::Committed);
            assert(storage.set_mutation_guard([]() { return true; })); verify_old_or_committed(storage,r);
            assert(storage.save_transaction(key,next.data(),material_size)==tesla_clean::SaveResult::Committed); ++cancellations;
        }
    }
    unsigned io_cases=0,delete_cases=0;
    material_size=129;
    for(unsigned k=0;k<4;++k) for(bool backup:{false,true}) {
        key=keys[k]; path=paths[k];
        for(auto selected:{IoMode::ShortWrite,IoMode::ShortRead,IoMode::CorruptRead}) {
            seed(storage,backup); mode=selected; active=true; fail_at=0;
            const auto r=storage.save_transaction(key,next.data(),material_size); active=false; mode=IoMode::Ordinary;
            assert(tesla_clean::KeyStorage::committed(r)==(selected==IoMode::ShortWrite)); verify_old_or_committed(storage,r);
            assert(storage.save_transaction(key,next.data(),material_size)==tesla_clean::SaveResult::Committed); ++io_cases;
        }
        seed(storage,false); TeslaBleStorageIO::Paths p; assert(p.initialize(path));
        std::filesystem::copy_file(p.canonical,p.backup,std::filesystem::copy_options::overwrite_existing);
        assert(storage.save_transaction(key,next.data(),material_size)==tesla_clean::SaveResult::Committed);
        verify_old_or_committed(storage,tesla_clean::SaveResult::Committed); ++io_cases;
    }
    auto deletion_seed=[&]() {
        assert(storage.forget_material());
        for(unsigned k=0;k<4;++k) {
            assert(storage.save_transaction(keys[k],old.data(),1)==tesla_clean::SaveResult::Committed);
            TeslaBleStorageIO::Paths p; assert(p.initialize(paths[k]));
            std::filesystem::copy_file(p.canonical,p.backup,std::filesystem::copy_options::overwrite_existing);
            std::filesystem::copy_file(p.canonical,p.temporary,std::filesystem::copy_options::overwrite_existing);
        }
    };
    deletion_seed(); active=true; calls=fail_at=0; assert(storage.forget_material()); active=false; const auto deletes=calls;
    for(unsigned n=1;n<=deletes;++n) {
        deletion_seed(); active=true; calls=0; fail_at=n; assert(!storage.forget_material()); active=false;
        assert(storage.forget_material() && std::filesystem::is_empty(TESLA_CLEAN_STORAGE_PREFIX "/config")); ++delete_cases;
    }
    unsigned load_faults=0,load_successes=0;
    for(unsigned k=0;k<4;++k) for(size_t bytes:{size_t{1},size_t{6},size_t{65},size_t{129},size_t{4096}}) for(bool backup:{false,true}) {
        key=keys[k]; path=paths[k]; material_size=bytes; seed(storage,backup);
        uint8_t out[4096]; size_t size=99;
        mode=bytes<4096?IoMode::ShortRead:IoMode::Ordinary; calls=fail_at=0; active=true;
        assert(storage.load_into(key,out,sizeof(out),size)==tesla_clean::LoadResult::Loaded);
        active=false; const auto boundaries=calls;
        assert(size==bytes && !std::memcmp(out,old.data(),bytes)); ++load_successes;
        for(unsigned n=1;n<=boundaries;++n) {
            const pid_t pid=fork(); assert(pid>=0);
            if(!pid) {
                std::memset(out,255,sizeof(out)); size=99; calls=0; fail_at=n; active=true;
                assert(storage.load_into(key,out,sizeof(out),size)==tesla_clean::LoadResult::Error); active=false;
                assert(size==0); for(auto b:out) { assert(b==0); }
                if(storage.restart_required()) {
                    TeslaBleStorageAdapter successor;
                    assert(successor.load_into(key,out,sizeof(out),size)==tesla_clean::LoadResult::RestartRequired);
                } else {
                    assert(storage.load_into(key,out,sizeof(out),size)==tesla_clean::LoadResult::Loaded && size==bytes);
                    assert(!std::memcmp(out,old.data(),bytes));
                }
                _exit(0);
            }
            int status=0; assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0); ++load_faults;
        }
        fail_at=0;
        for(auto selected:{IoMode::EofRead,IoMode::InterruptedRead}) {
            mode=selected; interrupted=false; active=true; size=99; std::memset(out,255,sizeof(out));
            const auto result=storage.load_into(key,out,sizeof(out),size); active=false;
            if(selected==IoMode::EofRead) {
                assert(result==tesla_clean::LoadResult::Error && size==0); for(auto b:out) { assert(b==0); }
            } else { assert(interrupted && result==tesla_clean::LoadResult::Loaded && size==bytes && !std::memcmp(out,old.data(),bytes)); }
            mode=IoMode::Ordinary;
            assert(storage.load_into(key,out,sizeof(out),size)==tesla_clean::LoadResult::Loaded && !std::memcmp(out,old.data(),bytes));
            ++load_successes;
        }
    }
    assert(storage.forget_material());
    std::printf("Bound WARP descriptor storage: %u injected syscall failures + %u monotonic cancellation cases across 4 keys, 1/6/65/129/4096 bytes and canonical/backup commits; %u short-write/read/corruption/canonical+backup cases and %u complete-delete faults; explicit outcomes, old-or-new commit, cleanup, close quarantine and retry passed\n",cases,cancellations,io_cases,delete_cases);
    std::printf("Independent production load matrix: %u canonical/backup open/fstat/every-partial-read/close faults and %u partial/EINTR/EOF schedules; error erasure, nonmutating recovery, quarantine across adapter recreation passed\n",load_faults,load_successes);
}
