// SPDX-License-Identifier: Apache-2.0
#include "../../../src/modules/tesla_vehicle/tesla_ble_clean_platform.h"
#include "../../../src/modules/tesla_vehicle/tesla_ble_storage.h"
#include <tesla_clean/storage.hpp>
#include <cassert>
#include <filesystem>
#include <cstring>
using namespace tesla_clean;
static int entropy(void *p,uint8_t *out,size_t size) {
    auto &n=*static_cast<uint32_t *>(p);
    for(size_t i=0;i<size;++i) { n=n*1664525u+1013904223u; out[i]=n>>24; } return 0;
}
int main() {
    tesla_warp::CheckedMutex mutex; assert(mutex.initialized());
    { tesla_warp::CheckedMutex::Guard g(mutex); assert(g);
      tesla_warp::CheckedMutex::Guard refused(mutex,true); assert(!refused); }
    { tesla_warp::CheckedMutex::Guard g(mutex,true); assert(g); }
    bool called=false; auto f=[&]() { called=true; return true; };
    tesla_warp::FunctionRef<bool()> ref(f); assert(ref() && called);
    std::filesystem::create_directories(TESLA_CLEAN_STORAGE_PREFIX "/config");
    TeslaBleStorageAdapter storage; assert(storage.forget_material());
    uint8_t buffer[512]; size_t size=123;
    assert(storage.load_into("private_key",buffer,sizeof(buffer),size)==LoadResult::Absent && size==0);
    IdentityStore keys; uint32_t seed=1; Random rng{entropy,&seed};
    assert(storage.initialize_identity(keys,rng));
    IdentityLease first; assert(keys.acquire(first)); std::array<uint8_t,65> point; assert(first.public_point(point));
    assert(storage.private_key_presence()==TeslaBleStorageAdapter::KeyPresence::Present);
    IdentityStore restart; assert(storage.initialize_identity(restart,rng)); IdentityLease second; assert(restart.acquire(second));
    std::array<uint8_t,65> after; assert(second.public_point(after) && after==point);
    assert(storage.set_mutation_guard([]() { return false; }));
    const uint8_t mac[6]={1,2,3,4,5,6};
    assert(!KeyStorage::committed(storage.save_transaction("warp_ev_mac",mac,sizeof(mac))));
    assert(!storage.forget_material());
    assert(storage.set_mutation_guard([]() { return true; }));
    assert(KeyStorage::committed(storage.save_transaction("warp_ev_mac",mac,sizeof(mac))));
    assert(storage.load_into("warp_ev_mac",buffer,sizeof(buffer),size)==LoadResult::Loaded && size==6 && !std::memcmp(buffer,mac,6));
    std::memset(buffer,0xA5,sizeof(buffer));
    assert(storage.load_into("private_key",buffer,1,size)==LoadResult::Error && size==0 && buffer[0]==0);
    assert(storage.forget_material());
    assert(storage.private_key_presence()==TeslaBleStorageAdapter::KeyPresence::Absent);
    assert(storage.set_mutation_guard([]() { return false; }));
    IdentityStore refused; assert(!storage.initialize_identity(refused,rng)); IdentityLease missing; assert(!refused.acquire(missing));
    assert(storage.set_mutation_guard([]() { return true; }));
    assert(storage.initialize_identity(refused,rng));
    assert(storage.forget_material());
}
