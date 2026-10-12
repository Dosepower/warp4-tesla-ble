// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/storage.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tesla_clean;
struct Backend {
    LoadResult load_result=LoadResult::Loaded;
    SaveResult save_result=SaveResult::Committed;
    size_t reported=7; unsigned loads=0,saves=0,removes=0;
    bool removal=true;
    static LoadResult load(void *p,uint8_t *data,size_t capacity,size_t &n) {
        auto &b=*static_cast<Backend *>(p); ++b.loads; std::memset(data,23,capacity); n=b.reported; return b.load_result;
    }
    static SaveResult save(void *p,Bytes data) { auto &b=*static_cast<Backend *>(p); ++b.saves; assert(data.valid()); return b.save_result; }
    static bool remove(void *p) { auto &b=*static_cast<Backend *>(p); ++b.removes; return b.removal; }
    StorageOps ops() { return {load,save,remove,this}; }
};
int main()
{
    Backend b; CheckedKeyStorage storage;
    std::array<uint8_t,512> bytes{}; size_t n=123;
    assert(storage.load(bytes.data(),bytes.size(),n)==LoadResult::Error && n==0);
    auto invalid=b.ops(); invalid.save=nullptr; assert(!storage.initialize(invalid));
    assert(storage.initialize(b.ops()) && !storage.initialize(b.ops()));
    assert(storage.load(bytes.data(),bytes.size(),n)==LoadResult::Loaded && n==7 && bytes[0]==23);
    for(auto status:{LoadResult::Absent,LoadResult::Error}) {
        b.load_result=status; bytes.fill(99); n=123;
        assert(storage.load(bytes.data(),bytes.size(),n)==status && n==0);
        for(auto v:bytes) { assert(v==0); }
    }
    b.load_result=LoadResult::Loaded; b.reported=513;
    assert(storage.load(bytes.data(),bytes.size(),n)==LoadResult::Error && n==0 && bytes[0]==0);
    b.reported=0; assert(storage.load(bytes.data(),bytes.size(),n)==LoadResult::Error);
    b.reported=7;
    const auto loads=b.loads;
    assert(storage.load(nullptr,512,n)==LoadResult::Error && b.loads==loads);
    assert(storage.load(bytes.data(),513,n)==LoadResult::Error && b.loads==loads);
    assert(storage.save({nullptr,1})==SaveResult::NotCommitted && b.saves==0);
    assert(storage.save({bytes.data(),0})==SaveResult::NotCommitted && b.saves==0);
    assert(storage.save({bytes.data(),513})==SaveResult::NotCommitted && b.saves==0);
    for(auto status:{SaveResult::NotCommitted,SaveResult::NotCommittedCleanupPending,SaveResult::Committed,SaveResult::CommittedCleanupPending}) {
        b.save_result=status; assert(storage.save({bytes.data(),7})==status && !storage.quarantined());
    }
    b.removal=false; assert(!storage.remove()); b.removal=true; assert(storage.remove());
    b.save_result=SaveResult::RestartRequired;
    assert(storage.save({bytes.data(),7})==SaveResult::RestartRequired && storage.quarantined());
    const auto saves=b.saves,removes=b.removes;
    assert(storage.save({bytes.data(),7})==SaveResult::RestartRequired && b.saves==saves);
    assert(storage.load(bytes.data(),512,n)==LoadResult::RestartRequired && b.loads==loads);
    assert(!storage.remove() && b.removes==removes);
    // Invalid backend enums are not success; latch quarantine, erase loaded output.
    CheckedKeyStorage bad; assert(bad.initialize(b.ops())); b.load_result=static_cast<LoadResult>(255);
    assert(bad.load(bytes.data(),512,n)==LoadResult::RestartRequired && bad.quarantined() && n==0 && bytes[0]==0);
    CheckedKeyStorage bad_save; assert(bad_save.initialize(b.ops())); b.save_result=static_cast<SaveResult>(255);
    assert(bad_save.save({bytes.data(),7})==SaveResult::RestartRequired && bad_save.quarantined());
    std::puts("Clean checked storage: explicit commit/refusal/cleanup results, invalid bounds/enums, cleared failed load and quarantine passed");
}
