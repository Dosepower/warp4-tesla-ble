// R13: real POSIX production transaction, syscall fault boundaries only.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdlib>
#include "../../src/modules/tesla_vehicle/bluetooth_config_store.h"
static std::string fault;
static int closes=0, renames=0;
extern "C" int __real_open(const char *, int, ...);
extern "C" ssize_t __real_write(int,const void *,size_t);
extern "C" ssize_t __real_read(int,void *,size_t);
extern "C" int __real_fsync(int);
extern "C" int __real_close(int);
extern "C" int __real_rename(const char *,const char *);
extern "C" int __real_unlink(const char *);
extern "C" int __wrap_open(const char *p,int f,...) {
    if(fault=="open" && (f&(O_CREAT|O_WRONLY|O_RDWR))) {errno=EIO;return -1;}
    return __real_open(p,f,0600);
}
extern "C" ssize_t __wrap_write(int fd,const void *b,size_t n) {
    if(fault=="write") {errno=EIO;return -1;}
    if(fault=="short") return __real_write(fd,b,n-1);
    return __real_write(fd,b,n);
}
extern "C" ssize_t __wrap_read(int fd,void *b,size_t n) {
    if(fault=="read") {errno=EIO;return -1;}
    return __real_read(fd,b,n);
}
extern "C" int __wrap_fsync(int fd) {
    if(fault=="fsync") {errno=EIO;return -1;}
    return __real_fsync(fd);
}
extern "C" int __wrap_close(int fd) {
    int rc=__real_close(fd);
    if(fault=="close" && ++closes==1) {errno=EIO;return -1;}
    return rc;
}
extern "C" int __wrap_rename(const char *a,const char *b) {
    ++renames;
    if((fault=="rename" && renames==2) || (fault=="rollback" && renames>=2)) {errno=EIO;return -1;}
    return __real_rename(a,b);
}
extern "C" int __wrap_unlink(const char *p) {
    if(fault=="remove" && std::string(p).find(".bak")!=std::string::npos) {errno=EIO;return -1;}
    return __real_unlink(p);
}
#ifndef R13_NO_PERSISTENCE_MAIN
int main(int argc,char **argv) {
    assert(argc==2); std::string dir=argv[1];
    assert(::mkdir(dir.c_str(),0700)==0 || errno==EEXIST);
    const std::string path=dir+"/bluetooth_config";
    for(const auto *mode:{"open","write","short","read","fsync","close","rename","remove","rollback","ok"}) {
        fault.clear(); closes=renames=0;
        for(const char *s:{"",".tmp",".bak"}) ::unlink((path+s).c_str());
        BluetoothConfigStore store(path.c_str());
        bool value=true; assert(store.load(value) && !value);
        assert(store.save(false));
        fault=mode; closes=renames=0;
        const bool saved=store.save(true);
        assert(saved==(fault=="ok"));
        fault.clear(); BluetoothConfigStore reboot(path.c_str());
        value=true; assert(reboot.load(value));
        assert(value==saved); // Any failure preserves/restores last confirmed OFF.
        if(std::string(mode)=="close") assert(!store.save(true)); // close quarantine
    }
    puts("R13 POSIX persistence failures and reboot recovery passed");
    return 0;
}
#endif
