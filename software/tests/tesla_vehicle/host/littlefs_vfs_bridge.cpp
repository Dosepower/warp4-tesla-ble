// Routes the production target POSIX storage calls for "/data/..." through the
// extracted esp_littlefs 1.22.3 vfs_littlefs_* hooks (build_littlefs_vfs_harness.py).
// Every other path/descriptor (test fixtures, config files) uses the host libc.
// Link with -Wl,--wrap=open,--wrap=close,--wrap=read,--wrap=write,--wrap=fsync,
// --wrap=fstat,--wrap=stat,--wrap=rename,--wrap=unlink.
#include <cstdarg>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <functional>
#include <string>
extern "C" {
void vfs_backend_setup(); void vfs_backend_teardown();
int vfs_backend_open(const char *,int); int vfs_backend_close(int); long vfs_backend_read(int,void *,size_t);
long vfs_backend_write(int,const void *,size_t); int vfs_backend_fsync(int); int vfs_backend_fstat(int,struct stat *);
int vfs_backend_stat(const char *,struct stat *); int vfs_backend_rename(const char *,const char *); int vfs_backend_unlink(const char *);
unsigned vfs_backend_open_fds();
int __real_open(const char *,int,...); int __real_close(int); ssize_t __real_read(int,void *,size_t);
ssize_t __real_write(int,const void *,size_t); int __real_fsync(int); int __real_fstat(int,struct stat *);
int __real_stat(const char *,struct stat *); int __real_rename(const char *,const char *); int __real_unlink(const char *);
}
// Target VFS public FDs are offset so they never collide with host descriptors.
static constexpr int VFS_FD_BASE = 1 << 20;
static std::recursive_mutex vfs_bridge_lock; // FreeRTOS efs->lock equivalent (sem_take is a shim).
static bool data_path(const char *p) { return p && !std::strncmp(p, "/data/", 6); }
static bool vfs_fd(int fd) { return fd >= VFS_FD_BASE; }
// Optional observation/fault hook: operation, path. Return errno to fail (0 = pass through).
std::function<int(const char *, const std::string &)> vfs_bridge_fault;
static int faulted(const char *op, const std::string &path) {
    if (!vfs_bridge_fault) { return 0; }
    const int e = vfs_bridge_fault(op, path);
    if (e) { errno = e; }
    return e;
}
static std::string vfs_paths[64];
extern "C" int __wrap_open(const char *p, int f, ...) {
    va_list args; va_start(args, f); const int mode = (f & O_CREAT) ? va_arg(args, int) : 0; va_end(args);
    if (!data_path(p)) { return __real_open(p, f, mode); }
    std::lock_guard<std::recursive_mutex> g(vfs_bridge_lock);
    if (faulted("open", p)) { return -1; }
    const int fd = vfs_backend_open(p, f);
    if (fd < 0) { return -1; }
    vfs_paths[fd] = p;
    return fd + VFS_FD_BASE;
}
extern "C" int __wrap_close(int fd) {
    if (!vfs_fd(fd)) { return __real_close(fd); }
    std::lock_guard<std::recursive_mutex> g(vfs_bridge_lock);
    const auto path = vfs_paths[fd - VFS_FD_BASE];
    const int rc = vfs_backend_close(fd - VFS_FD_BASE);
    return faulted("close", path) ? -1 : rc;
}
extern "C" ssize_t __wrap_read(int fd, void *p, size_t n) {
    if (!vfs_fd(fd)) { return __real_read(fd, p, n); }
    std::lock_guard<std::recursive_mutex> g(vfs_bridge_lock);
    if (faulted("read", vfs_paths[fd - VFS_FD_BASE])) { return -1; }
    return vfs_backend_read(fd - VFS_FD_BASE, p, n);
}
extern "C" ssize_t __wrap_write(int fd, const void *p, size_t n) {
    if (!vfs_fd(fd)) { return __real_write(fd, p, n); }
    std::lock_guard<std::recursive_mutex> g(vfs_bridge_lock);
    if (faulted("write", vfs_paths[fd - VFS_FD_BASE])) { return -1; }
    return vfs_backend_write(fd - VFS_FD_BASE, p, n);
}
extern "C" int __wrap_fsync(int fd) {
    if (!vfs_fd(fd)) { return __real_fsync(fd); }
    std::lock_guard<std::recursive_mutex> g(vfs_bridge_lock);
    if (faulted("fsync", vfs_paths[fd - VFS_FD_BASE])) { return -1; }
    return vfs_backend_fsync(fd - VFS_FD_BASE);
}
extern "C" int __wrap_fstat(int fd, struct stat *s) {
    if (!vfs_fd(fd)) { return __real_fstat(fd, s); }
    std::lock_guard<std::recursive_mutex> g(vfs_bridge_lock);
    if (faulted("fstat", vfs_paths[fd - VFS_FD_BASE])) { return -1; }
    return vfs_backend_fstat(fd - VFS_FD_BASE, s);
}
extern "C" int __wrap_stat(const char *p, struct stat *s) {
    if (!data_path(p)) { return __real_stat(p, s); }
    std::lock_guard<std::recursive_mutex> g(vfs_bridge_lock);
    if (faulted("stat", p)) { return -1; }
    return vfs_backend_stat(p, s);
}
extern "C" int __wrap_rename(const char *a, const char *b) {
    if (!data_path(a) && !data_path(b)) { return __real_rename(a, b); }
    std::lock_guard<std::recursive_mutex> g(vfs_bridge_lock);
    if (!data_path(a) || !data_path(b)) { errno = EXDEV; return -1; }
    if (faulted("rename", a)) { return -1; }
    return vfs_backend_rename(a, b);
}
extern "C" int __wrap_unlink(const char *p) {
    if (!data_path(p)) { return __real_unlink(p); }
    std::lock_guard<std::recursive_mutex> g(vfs_bridge_lock);
    if (faulted("unlink", p)) { return -1; }
    return vfs_backend_unlink(p);
}
