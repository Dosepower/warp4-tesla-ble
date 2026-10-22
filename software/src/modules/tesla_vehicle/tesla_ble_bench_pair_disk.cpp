#include "tesla_ble_bench_pair_disk.h"
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS && (!defined(TESLA_BLE_R13_MASTER_BENCH) || !TESLA_BLE_R13_MASTER_BENCH)
#include "tesla_ble_storage_io.h"
#include "tesla_ble_bench_record.h"
#include <mbedtls/platform_util.h>
using namespace tesla_clean;
namespace {
// Outside the generic config API namespace; only bench actions own these files.
#ifdef TESLA_CLEAN_STORAGE_PREFIX
#define BENCH_PREFIX TESLA_CLEAN_STORAGE_PREFIX
#elif defined(TESLA_BLE_HOST_FILESYSTEM)
#define BENCH_PREFIX ""
#else
#define BENCH_PREFIX "/data"
#endif
struct BenchPaths { const char *canonical; const char *temporary; const char *backup; };
constexpr BenchPaths key_paths={BENCH_PREFIX "/tesla_ble_bench_pair_record",BENCH_PREFIX "/tesla_ble_bench_pair_record.tmp",BENCH_PREFIX "/tesla_ble_bench_pair_record.bak"};
constexpr BenchPaths marker_paths={BENCH_PREFIX "/tesla_ble_bench_pair_exposed",BENCH_PREFIX "/tesla_ble_bench_pair_exposed.tmp",BENCH_PREFIX "/tesla_ble_bench_pair_exposed.bak"};
#undef BENCH_PREFIX
using namespace TeslaBleStorageIO;
LoadResult read_file(const char *path,uint8_t *buffer,size_t capacity,size_t &length)
{
    length=0; memset(buffer,0,capacity);
    const auto exists=presence(path);
    if (exists==Presence::Absent) { return LoadResult::Absent; }
    if (exists!=Presence::Present) { return LoadResult::Error; }
    File file; size_t size=0;
    bool ok=file.open(path,false) && file.size(size) && size>0 && size<=capacity;
    while (ok && length<size) { const auto n=file.read(buffer+length,size-length); if(n<=0) { ok=false; } else { length+=n; } }
    ok=file.close() && ok;
    if(!ok) { mbedtls_platform_zeroize(buffer,capacity); length=0; }
    return ok?LoadResult::Loaded:LoadResult::Error;
}
bool verify(const char *path,Bytes bytes)
{
    std::unique_ptr<BenchPairRecord> workspace(new(std::nothrow) BenchPairRecord);
    if (!workspace) { return false; }
    size_t length=0;
    const auto &readback=workspace->bytes;
    return read_file(path,workspace->bytes.data(),readback.size(),length)==LoadResult::Loaded && length==bytes.size && !memcmp(readback.data(),bytes.data,length);
}
SaveResult create_record(const BenchPaths &p,Bytes bytes)
{
    if (!bytes.valid() || !bytes.size || bytes.size>544 || restart_required()) { return SaveResult::NotCommitted; }
    // Never overwrite old/canonical or crash-staged material. Explicit cleanup
    // is required if any variant exists. Single worker owns this namespace.
    for(const char *entry:{p.canonical,p.temporary,p.backup}) { if(presence(entry)!=Presence::Absent) { return SaveResult::NotCommittedCleanupPending; } }
    File file; bool ok=file.open(p.temporary,true); size_t written=0;
    while(ok && written<bytes.size) { const auto n=file.write(bytes.data+written,bytes.size-written); if(n<=0) { ok=false; } else { written+=n; } }
    ok=ok && file.sync(); ok=file.close() && ok;
    if (!ok || !verify(p.temporary,bytes) || !TeslaBleStorageIO::rename(p.temporary,p.canonical) || !verify(p.canonical,bytes)) {
        return restart_required()?SaveResult::RestartRequired:SaveResult::NotCommittedCleanupPending;
    }
    return SaveResult::Committed;
}
}
LoadResult BenchPairDisk::load(uint8_t *buffer,size_t capacity,size_t &length)
{
    const auto &p=key_paths;
    if (!buffer || capacity>544) { return LoadResult::Error; }
    // Any incomplete transaction requires human cleanup, never regenerate.
    if (TeslaBleStorageIO::presence(p.temporary)!=TeslaBleStorageIO::Presence::Absent || TeslaBleStorageIO::presence(p.backup)!=TeslaBleStorageIO::Presence::Absent) { return LoadResult::Error; }
    return read_file(p.canonical,buffer,capacity,length);
}
SaveResult BenchPairDisk::save(Bytes bytes) { return create_record(key_paths,bytes); }
bool BenchPairDisk::exposure(bool mark,bool &value)
{
    value=false; const auto &p=marker_paths;
    if(TeslaBleStorageIO::presence(p.temporary)!=TeslaBleStorageIO::Presence::Absent ||
       TeslaBleStorageIO::presence(p.backup)!=TeslaBleStorageIO::Presence::Absent) { return false; }
    uint8_t data=0; size_t length=0; auto result=read_file(p.canonical,&data,1,length);
    if(result==LoadResult::Loaded) { value=data==1 && length==1; return value; }
    if(result!=LoadResult::Absent) { return false; }
    if(!mark) { return true; }
    const uint8_t exposed=1;
    value=create_record(marker_paths,{&exposed,1})==SaveResult::Committed; return value;
}
bool BenchPairDisk::remove()
{
    const auto &key=key_paths; const auto &marker=marker_paths;
    // Durable fail-closed tombstone BEFORE touching any key variant.
    uint8_t byte=0; size_t length=0;
    const auto loaded=read_file(marker.canonical,&byte,1,length);
    if(loaded==LoadResult::Absent) {
        const uint8_t exposed=1;
        if(create_record(marker_paths,{&exposed,1})!=SaveResult::Committed) { return false; }
    } else if(loaded!=LoadResult::Loaded || length!=1 || byte!=1) { return false; }
    auto remove_checked=[](const char *entry) {
            const auto presence=TeslaBleStorageIO::presence(entry);
            if(presence==TeslaBleStorageIO::Presence::Error || (presence==TeslaBleStorageIO::Presence::Present && !TeslaBleStorageIO::remove(entry))) { return false; }
            return TeslaBleStorageIO::presence(entry)==TeslaBleStorageIO::Presence::Absent;
    };
    for(const char *entry:{key.canonical,key.temporary,key.backup}) {
        if(!remove_checked(entry)) { return false; }
    }
    // Full readback precedes any marker deletion.
    for(const char *entry:{key.canonical,key.temporary,key.backup}) {
        if(TeslaBleStorageIO::presence(entry)!=TeslaBleStorageIO::Presence::Absent) { return false; }
    }
    for(const char *entry:{marker.temporary,marker.backup}) {
        if(!remove_checked(entry)) { return false; }
    }
    if(!remove_checked(marker.canonical)) {
        // Ambiguous final readback: restore tombstone, never infer absence.
        // Partial/failed restoration also reconstructs error, not key_ready.
        TeslaBleStorageIO::File file; const uint8_t exposed=1;
        if(file.open(marker.canonical,true)) { file.write(&exposed,1); file.sync(); }
        file.close();
        return false;
    }
    return true;
}
#endif
