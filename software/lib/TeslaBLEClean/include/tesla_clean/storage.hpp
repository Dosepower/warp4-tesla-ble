// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/identity.hpp>
#include <cstring>
namespace tesla_clean {
enum class LoadResult { Loaded, Absent, Error, RestartRequired };
struct StorageOps {
    LoadResult (*load)(void *,uint8_t *,size_t,size_t &)=nullptr;
    SaveResult (*save)(void *,Bytes)=nullptr;
    bool (*remove)(void *)=nullptr;
    void *context=nullptr;
};
class CheckedKeyStorage final : public KeyStorage {
    StorageOps ops_{};
    bool ready_=false,quarantined_=false;
public:
    bool initialize(StorageOps ops)
    {
        if(ready_ || !ops.load || !ops.save || !ops.remove) { return false; }
        ops_=ops; ready_=true; return true;
    }
    LoadResult load(uint8_t *output,size_t capacity,size_t &length)
    {
        length=0;
        if(!output || capacity==0 || capacity>512) { return LoadResult::Error; }
        std::memset(output,0,capacity);
        if(quarantined_) { return LoadResult::RestartRequired; }
        if(!ready_) { return LoadResult::Error; }
        LoadResult result=ops_.load(ops_.context,output,capacity,length);
        switch(result) {
        case LoadResult::Loaded: case LoadResult::Absent: case LoadResult::Error: break;
        case LoadResult::RestartRequired: quarantined_=true; break;
        default: quarantined_=true; result=LoadResult::RestartRequired; break;
        }
        if(result==LoadResult::Loaded && (length==0 || length>capacity)) { result=LoadResult::Error; }
        if(result!=LoadResult::Loaded) { std::memset(output,0,capacity); length=0; }
        return result;
    }
    SaveResult save(Bytes input) override
    {
        if(quarantined_) { return SaveResult::RestartRequired; }
        if(!ready_ || !input.valid() || input.size==0 || input.size>512) { return SaveResult::NotCommitted; }
        SaveResult result=ops_.save(ops_.context,input);
        switch(result) {
        case SaveResult::NotCommitted: case SaveResult::NotCommittedCleanupPending:
        case SaveResult::Committed: case SaveResult::CommittedCleanupPending: break;
        case SaveResult::RestartRequired: quarantined_=true; break;
        default: quarantined_=true; result=SaveResult::RestartRequired; break;
        }
        return result;
    }
    bool remove() { return ready_ && !quarantined_ && ops_.remove(ops_.context); }
    bool quarantined() const { return quarantined_; }
};
}
