// Real production POSIX storage boundary: no Arduino/LittleFS mock.
#include "../../src/modules/tesla_vehicle/tesla_ble_storage.h"
#include "../../src/modules/tesla_vehicle/tesla_ble_storage_io.h"
#include <cassert>
#include <string>
#include <cstdarg>
#include <cstdio>

static std::string fault, target;
static bool checking = false, injected = false;
static int config_fd = -1;
static unsigned mutations = 0;
static bool hit(const char *operation, const char *path = nullptr) {
    if (!checking || fault != operation || (path && target != path)) { return false; }
    injected = true; errno = EIO; return true;
}
extern "C" {
int __real_stat(const char *, struct stat *);
int __wrap_stat(const char *p, struct stat *s) {
    if (hit("stat", p)) { return -1; }
    return __real_stat(p, s);
}
int __real_open(const char *, int, ...);
int __wrap_open(const char *p, int flags, ...) {
    if (checking && (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC))) { ++mutations; }
    if (hit("open", p)) { return -1; }
    int fd;
    if (flags & O_CREAT) { va_list a; va_start(a, flags); int mode = va_arg(a, int); va_end(a); fd = __real_open(p, flags, mode); }
    else { fd = __real_open(p, flags); }
    if (checking) { config_fd = fd; }
    return fd;
}
int __real_fstat(int, struct stat *);
int __wrap_fstat(int fd, struct stat *s) {
    if (fd == config_fd && hit("fstat")) { return -1; }
    const int rc = __real_fstat(fd, s);
    if (fd == config_fd && hit("size-shrunk")) { --s->st_size; }
    if (fd == config_fd && hit("nonregular")) { s->st_mode = S_IFDIR | 0700; }
    return rc;
}
ssize_t __real_read(int, void *, size_t);
ssize_t __wrap_read(int fd, void *p, size_t n) {
    if (fd == config_fd && hit("read")) { return -1; }
    if (fd == config_fd && hit("eof")) { return 0; }
    if (fd == config_fd && n == 1 && hit("grown")) { *static_cast<char *>(p) = 'x'; return 1; }
    if (fd == config_fd && fault == "truncated" && checking) {
        injected = true;
        if (n > 1) { return __real_read(fd, p, n - 1); }
        return 0;
    }
    return __real_read(fd, p, n);
}
int __real_close(int);
int __wrap_close(int fd) {
    const bool fail = fd == config_fd && hit("close");
    const int rc = __real_close(fd);
    return fail ? -1 : rc;
}
ssize_t __real_write(int, const void *, size_t);
ssize_t __wrap_write(int fd, const void *p, size_t n) { if (checking) { ++mutations; } return __real_write(fd, p, n); }
int __real_unlink(const char *);
int __wrap_unlink(const char *p) { if (checking) { ++mutations; } return __real_unlink(p); }
int __real_rename(const char *, const char *);
int __wrap_rename(const char *a, const char *b) { if (checking) { ++mutations; } return __real_rename(a, b); }
}
int main(int argc, char **argv) {
    assert(argc == 4);
    const bool expected = std::string(argv[1]) == "blocked";
    fault = argv[2]; target = argv[3];
    // Repeated cold classification must not need a durable marker or cleanup.
    for (unsigned boot = 0; boot < 3; ++boot) {
        TeslaBleStorageIO::close_quarantine.store(false); config_fd = -1;
        checking = true;
        const bool blocked = TeslaBleStorageAdapter::legacy_material_present();
        checking = false;
        assert(blocked == expected);
        assert(mutations == 0);
    }
    assert(fault == "none" || injected);
    std::puts("EMPTY_LEGACY_POSIX PASS 3 reboots; zero mutation syscalls");
}
