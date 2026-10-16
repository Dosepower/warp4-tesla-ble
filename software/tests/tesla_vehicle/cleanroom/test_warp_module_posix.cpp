// SPDX-License-Identifier: Apache-2.0
// Actual production descriptor storage + module/worker/API/main; only syscall
// fault boundaries, EV/SDK/OS/Config are hosted. No key content printed/copied.
#define main unused_in_memory_module_main
#include "test_warp_module.cpp"
#undef main
#include <filesystem>
static bool fail_promotion=false,fail_remove=false,fail_close=false,fail_session_remove=false;
static std::function<void()> before_verified_close;
extern "C" int __real_rename(const char *,const char *);
extern "C" int __real_unlink(const char *);
extern "C" int __real_close(int);
extern "C" int __wrap_rename(const char *a,const char *b) {
    if(fail_promotion && std::string(a).find("private_key.tmp")!=std::string::npos) { errno=EIO; return -1; }
    return __real_rename(a,b);
}
extern "C" int __wrap_unlink(const char *p) {
    if(r6_remove_fault && r6_remove_fault("unlink:"+std::string(p))){errno=EIO;return -1;}
    if(fail_remove || (fail_session_remove && std::string(p).find("session_vcsec")!=std::string::npos)) { errno=EIO; return -1; }
    return __real_unlink(p);
}
extern "C" int __wrap_close(int fd) {
    char path[256]{},descriptor[64]{};
    std::snprintf(descriptor,sizeof(descriptor),"/proc/self/fd/%d",fd);
    const auto n=::readlink(descriptor,path,sizeof(path)-1);
    const bool verified=n>0 && std::string(path,size_t(n)).find("private_key.tmp")!=std::string::npos &&
        (::fcntl(fd,F_GETFL)&O_ACCMODE)==O_RDONLY;
    const int rc=__real_close(fd);
    if(verified && before_verified_close) { auto hook=std::move(before_verified_close); before_verified_close={}; hook(); }
    if(fail_close) { fail_close=false; errno=EIO; return -1; }
    return rc;
}
void TeslaBleModuleTest::ui_r3_posix(bool guard_refusal, bool deletion_failure, bool close_failure, bool session_failure) {
        auto *r=module.runtime_owner.get();
        assert(r->storage->forget_material()); r->bound_vehicle_mac.store(0); r->session.bind(0);
        ui_with_worker_guard([&]() {
            module.process_module_events(); assert(module.state.get("key_presence")->asString()=="absent");
            TeslaBleStorageIO::Paths key; assert(key.initialize("/config/tesla_ble_private_key"));
            bool refused=false;

            // Fail promotion syscall after real verified write, or identity-change
            // at verified read-close via the host close boundary below.
            if(guard_refusal) { before_verified_close=[&]() { ++ev.generation; module.loop(); refused=true; }; }
            fail_promotion=!guard_refusal;
            queue_pair(); module.process_commands(); fail_promotion=false; before_verified_close={};
            assert(!guard_refusal || refused);
            assert(std::filesystem::exists(key.temporary) && !std::filesystem::exists(key.canonical) && !std::filesystem::exists(key.backup));
            module.process_module_events();
            assert(!module.state.get("key_present")->asBool());
            assert(module.state.get("key_presence")->asString()=="cleanup");
            std::printf("R3_STORAGE_STATE {\"running\":true,\"key_present\":false,\"key_presence\":\"%s\",\"restart_required\":false}\n",module.state.get("key_presence")->asString().c_str());
            module.update_active_vehicle();
            const uint8_t fixture[]={1};
            for(const char *name : {"session_vcsec","session_infotainment","warp_ev_mac"}) {
                assert(tesla_clean::KeyStorage::committed(r->storage->save_transaction(name,fixture,1)));
                const char *path=std::strcmp(name,"session_vcsec")==0 ? "/config/tesla_ble_session_vcsec" :
                    std::strcmp(name,"session_infotainment")==0 ? "/config/tesla_ble_session_info" : "/config/tesla_ble_warp_ev_mac";
                TeslaBleStorageIO::Paths p; assert(p.initialize(path));
                std::filesystem::copy_file(p.canonical,p.backup);
                std::filesystem::copy_file(p.canonical,p.temporary);
            }
            if(close_failure) {
                fail_close=true; uint8_t bytes[6]{}; size_t n=0;
                assert(r->storage->load_into("warp_ev_mac",bytes,sizeof(bytes),n)==tesla_clean::LoadResult::Error);
                assert(r->storage->restart_required());
            }
            fail_remove=deletion_failure && !session_failure; fail_session_remove=session_failure;
            String error; api.commands.at("tesla_vehicle/forget_key")(0,error); assert(error.length()==0);
            module.process_commands(); fail_remove=false; fail_session_remove=false; module.process_module_events();
            if(deletion_failure || close_failure) {
                assert(r->stop_requested.load());
                if(session_failure) {
                    assert(!std::filesystem::exists(key.temporary));
                    TeslaBleStorageAdapter restarted;
                    assert(restarted.private_key_presence()==TeslaBleStorageAdapter::KeyPresence::CleanupRequired);
                    assert(restarted.forget_material());
                    assert(restarted.private_key_presence()==TeslaBleStorageAdapter::KeyPresence::Absent);
                } else { assert(std::filesystem::exists(key.temporary)); }
                module.loop(); assert(module.state.get("restart_required")->asBool());
                assert(module.state.get("key_presence")->asString()!="absent");
            } else {
                assert(module.state.get("key_presence")->asString()=="absent");
                assert(std::filesystem::is_empty(TESLA_CLEAN_STORAGE_PREFIX "/config"));
            }
            assert(sent.empty());
            std::puts("R3 real descriptors: staging-only material is deletable; deletion truth/fail-closed restart preserved");
        });
}
#ifndef TESLA_BLE_R6_POSIX_INCLUDE
int main(int argc,char **argv) {
    std::filesystem::create_directories(TESLA_CLEAN_STORAGE_PREFIX "/config");
    TeslaBleModuleTest t;
    const char *mode=argc==2 ? argv[1] : "promotion";
    if(std::strcmp(mode,"r5-delete-close-disconnect")==0)t.ui_r5_delete_delayed(false);
    else if(std::strcmp(mode,"r5-delete-disconnect-close")==0)t.ui_r5_delete_delayed(true);
    else if(std::strcmp(mode,"r5-final-absent")==0)t.ui_r5_final_presence(0,false);
    else if(std::strcmp(mode,"r5-final-consumed")==0)t.ui_r5_final_presence(0,true);
    else if(std::strcmp(mode,"r5-final-cleanup")==0)t.ui_r5_final_presence(1,false);
    else if(std::strcmp(mode,"r5-final-unknown")==0)t.ui_r5_final_presence(2,false);
    else if(std::strcmp(mode,"r5-queue-recovery")==0)t.ui_r5_queue_recovery();
    else t.ui_r3_posix(std::strstr(mode,"guard")!=nullptr,std::strstr(mode,"remove-fail")!=nullptr,std::strstr(mode,"close-fail")!=nullptr,std::strstr(mode,"session-remove-fail")!=nullptr);
    std::filesystem::remove_all(TESLA_CLEAN_STORAGE_PREFIX);
}
#endif
