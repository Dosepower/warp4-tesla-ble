// Real BluetoothConfigStore; fault injection only at POSIX syscalls.
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "../../src/modules/tesla_vehicle/bluetooth_config_store.h"
namespace fs = std::filesystem;
static std::string fault;
static int fail_close=0, close_count=0, rename_count=0, mutations=0;
static int fail_sync=0, sync_count=0, fail_read=0, read_count=0;
static bool injecting=false;
extern "C" int __real_open(const char *, int, ...);
extern "C" ssize_t __real_read(int,void *,size_t);
extern "C" ssize_t __real_write(int,const void *,size_t);
extern "C" int __real_fsync(int);
extern "C" int __real_close(int);
extern "C" int __real_unlink(const char *);
extern "C" int __real_rename(const char *,const char *);
extern "C" int __real_ftruncate(int,off_t);
extern "C" int __wrap_open(const char *p,int f,...) {
    if(injecting && (f&(O_CREAT|O_TRUNC))) { ++mutations; if(fault=="open") {errno=EIO;return -1;} }
    return __real_open(p,f,0600);
}
extern "C" ssize_t __wrap_read(int fd,void *b,size_t n) {
    if(injecting && ++read_count==fail_read) {errno=EIO;return -1;}
    if(injecting && fault=="read") {errno=EIO;return -1;}
    if(injecting && fault=="eof") return 0;
    if(injecting && fault=="short-read") n=std::min(n,size_t{1});
    if(injecting && fault=="prefix-read") n=std::min(n,size_t{15});
    return __real_read(fd,b,n);
}
extern "C" ssize_t __wrap_write(int fd,const void *b,size_t n) {
    if(injecting) { ++mutations; if(fault=="write") {errno=EIO;return -1;} if(fault=="short-write") --n; }
    return __real_write(fd,b,n);
}
extern "C" int __wrap_ftruncate(int fd,off_t n) {if(injecting) ++mutations; return __real_ftruncate(fd,n);}
extern "C" int __wrap_fsync(int fd) {if(injecting && (++sync_count==fail_sync || fault=="fsync")) {errno=EIO;return -1;} return __real_fsync(fd);}
extern "C" int __wrap_close(int fd) {
    const int result=__real_close(fd);
    if(injecting && ++close_count==fail_close) {errno=EIO;return -1;}
    return result;
}
extern "C" int __wrap_unlink(const char *p) {
    if(injecting) {++mutations;if(fault=="remove") {errno=EIO;return -1;}}
    return __real_unlink(p);
}
extern "C" int __wrap_rename(const char *a,const char *b) {
    if(injecting) {
        ++mutations; ++rename_count;
        if((fault=="rename" && rename_count==2) || (fault=="rollback" && rename_count>=2)) {errno=EIO;return -1;}
    }
    return __real_rename(a,b);
}
static void put(const fs::path &p,const std::string &s) {std::ofstream out(p);out<<s;assert(out.good());}
static std::string bytes(const fs::path &p) {std::ifstream in(p);return {std::istreambuf_iterator<char>(in),{}};}
static void clean(const fs::path &p) {for(const char *s:{"",".tmp",".bak"}) fs::remove(p.string()+s);}
int main(int argc,char **argv) {
    assert(argc==3);const fs::path dir=argv[1];const std::string mode=argv[2];
    fs::create_directories(dir/"real");auto path=dir/"real/config";
    const auto victim=dir/"victim";const std::string old="{\"enable\":false}";
    put(victim,old);
    if(mode=="vfs-unavailable") {
        put(path,old);BluetoothConfigStore store(path.c_str());bool value=true;
        injecting=true;assert(!store.load(value));assert(!store.save(true));injecting=false;
        assert(mutations==0 && bytes(path)==old && bytes(victim)==old);return 0;
    }
    if(mode=="prefix") {
        put(path,"{\"enable\":true}TRAILING");BluetoothConfigStore store(path.c_str());
        fault="prefix-read";injecting=true;bool value=false;assert(!store.load(value));assert(!store.save(true));
        injecting=false;assert(bytes(path)=="{\"enable\":true}TRAILING");return 0;
    }
    if(mode=="faults") {
        unsigned schedules=0;
        for(bool initial:{true,false}) {
            for(const std::string failure:{"open","write","short-write","read","eof","fsync","rename","rollback","remove"}) {
                clean(path);BluetoothConfigStore store(path.c_str());
                if(!initial) assert(store.save(false));
                // Force a stale temporary file, including first activation cleanup.
                put(path.string()+".tmp",old);
                fault=failure;rename_count=close_count=mutations=0;injecting=true;
                assert(!store.save(true));injecting=false;fault.clear();
                BluetoothConfigStore reboot(path.c_str());bool value=true;
                assert(reboot.load(value) && !value);
                assert(reboot.save(true));assert(reboot.load(value) && value);++schedules;
            }
            // Count and fail EACH close, including temporary verification and
            // promoted-canonical read-close; IDF close refusal quarantines this boot.
            clean(path);BluetoothConfigStore count(path.c_str());if(!initial) assert(count.save(false));
            close_count=sync_count=read_count=0;fault.clear();injecting=true;assert(count.save(true));injecting=false;
            const int positions=close_count;assert(positions>=4);
            const int sync_positions=sync_count, read_positions=read_count;
            for(int n=1;n<=positions;++n) {
                clean(path);BluetoothConfigStore store(path.c_str());if(!initial) assert(store.save(false));
                fail_close=n;close_count=rename_count=0;injecting=true;assert(!store.save(true));injecting=false;fail_close=0;
                assert(!store.save(true)); // Quarantine cannot be cleared by retry.
                BluetoothConfigStore reboot(path.c_str());bool value=true;assert(reboot.load(value) && !value);
                assert(reboot.save(true));++schedules;
            }
            for(bool sync:{true,false}) {
                for(int n=1;n<=(sync?sync_positions:read_positions);++n) {
                    clean(path);BluetoothConfigStore store(path.c_str());if(!initial) assert(store.save(false));
                    fail_sync=sync?n:0;fail_read=sync?0:n;sync_count=read_count=rename_count=0;
                    injecting=true;assert(!store.save(true));injecting=false;fail_sync=fail_read=0;
                    BluetoothConfigStore reboot(path.c_str());bool value=true;assert(reboot.load(value) && !value);
                    assert(reboot.save(true));++schedules;
                }
            }
            // Fail promotion AND rollback after the initial OFF authority exists.
            clean(path);BluetoothConfigStore rollback(path.c_str());if(!initial) assert(rollback.save(false));
            fault="rollback";rename_count=initial?-1:0;injecting=true;assert(!rollback.save(true));injecting=false;fault.clear();
            assert(fs::exists(path.string()+".bak"));bool recovered=true;
            BluetoothConfigStore reboot(path.c_str());assert(reboot.load(recovered) && !recovered);
            assert(reboot.save(true));++schedules;
        }
        clean(path);BluetoothConfigStore short_reads(path.c_str());assert(short_reads.save(true));
        fault="short-read";injecting=true;bool value=false;assert(short_reads.load(value) && value);injecting=false;
        assert(bytes(victim)==old);printf("fault schedules=%u\n",schedules);return 0;
    }
    if(mode=="ancestor") {fs::create_directory_symlink(dir/"real",dir/"alias");path=dir/"alias/config";}
    else if(mode.rfind("alias:",0)==0) {
        const std::string a=mode=="alias:tb"?".tmp":"";
        const std::string b=mode=="alias:ct"?".tmp":".bak";
        put(path.string()+a,old);fs::create_hard_link(path.string()+a,path.string()+b);
    }
    else {
        const auto colon=mode.find(':');assert(colon!=std::string::npos);
        const std::string leaf=mode.substr(0,colon),kind=mode.substr(colon+1);
        const fs::path target=path.string()+(leaf=="canonical"?"":leaf=="tmp"?".tmp":".bak");
        if(kind=="symlink") fs::create_symlink(victim,target);
        else if(kind=="dangling") fs::create_symlink(dir/"absent",target);
        else if(kind=="hardlink") fs::create_hard_link(victim,target);
        else if(kind=="directory") fs::create_directory(target);
        else if(kind=="fifo") assert(::mkfifo(target.c_str(),0600)==0);
        else assert(false);
    }
    const auto before=bytes(victim);BluetoothConfigStore store(path.c_str());
    injecting=true;mutations=0;bool value=true;assert(!store.load(value));assert(!store.save(true));
    injecting=false;assert(mutations==0);assert(bytes(victim)==before);
    assert(!fs::exists(dir/"absent"));
    // Remove only this fixture's bad entry, then retry the same store.
    if(mode=="ancestor") {fs::remove(dir/"alias");fs::create_directories(dir/"alias");}
    else clean(path);
    assert(store.save(true));assert(store.load(value) && value);
    assert(bytes(victim)==before);puts("unsafe paths rejected before mutation, retry passed");
}
