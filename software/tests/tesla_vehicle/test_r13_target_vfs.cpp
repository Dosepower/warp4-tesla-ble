// Exercise the production ESP_PLATFORM store through its actual VFS boundary.
#include <cassert>
#include <cstdarg>
#include <dirent.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include "bluetooth_config_store.h"
static std::string root, fault, target;
static unsigned hits = 0, fail_at = 1, mutations = 0;
static int failure_errno = EIO;
static std::map<int, std::string> fds;
static std::map<DIR *, std::string> dirs;
static bool mounted = true;
static std::string mapped(const char *p) { return root + p; }
static bool fail(const char *op, const std::string &path) {
    if (fault != op || (!target.empty() && path != target)) { return false; }
    if (++hits != fail_at) { return false; }
    errno = failure_errno; return true;
}
extern "C" {
int __real_open(const char *, int, ...);
int __real_close(int);
int __real_fstat(int, struct stat *);
ssize_t __real_read(int, void *, size_t);
ssize_t __real_write(int, const void *, size_t);
int __real_fsync(int);
int __real_ftruncate(int, off_t);
int __real_rename(const char *, const char *);
int __real_unlink(const char *);
DIR *__real_opendir(const char *);
struct dirent *__real_readdir(DIR *);
int __real_closedir(DIR *);
int __wrap_open(const char *p, int flags, ...) {
    if (fail("open", p)) { return -1; }
    if (flags & (O_WRONLY|O_RDWR|O_CREAT|O_TRUNC)) { ++mutations; }
    int fd = __real_open(mapped(p).c_str(), flags, 0600);
    if (fd >= 0) { fds[fd] = p; }
    return fd;
}
int __wrap_close(int fd) {
    const bool error = fail("close", fds[fd]);
    fds.erase(fd); const int rc = __real_close(fd);
    return error ? -1 : rc;
}
int __wrap_fstat(int fd, struct stat *st) {
    if (fail("size", fds[fd])) { return -1; }
    const int rc = __real_fstat(fd, st);
    if (fault == "long-prefix" && fds[fd] == target) { st->st_size = 15; }
    return rc;
}
ssize_t __wrap_read(int fd, void *p, size_t n) {
    if (fail("read", fds[fd])) { return -1; }
    if (fault == "short-read" && n > 1) { n = 1; }
    return __real_read(fd, p, n);
}
ssize_t __wrap_write(int fd, const void *p, size_t n) {
    ++mutations; if (fail("write", fds[fd])) { return -1; }
    return __real_write(fd, p, n);
}
int __wrap_fsync(int fd) { return fail("fsync", fds[fd]) ? -1 : __real_fsync(fd); }
int __wrap_ftruncate(int fd, off_t n) { ++mutations; return __real_ftruncate(fd, n); }
int __wrap_rename(const char *a, const char *b) {
    ++mutations;
    if (fail("rename", a) || (fault == "rollback" && (std::string(a).find(".tmp") != std::string::npos || std::string(a).find(".bak") != std::string::npos))) { return -1; }
    return __real_rename(mapped(a).c_str(), mapped(b).c_str());
}
int __wrap_unlink(const char *p) { ++mutations; return fail("unlink", p) ? -1 : __real_unlink(mapped(p).c_str()); }
DIR *__wrap_opendir(const char *p) {
    if (fail("opendir", p)) { return nullptr; }
    DIR *d = __real_opendir(mapped(p).c_str()); if (d) { dirs[d] = p; } return d;
}
struct dirent *__wrap_readdir(DIR *d) {
    if (fail("readdir", dirs[d])) { return nullptr; } return __real_readdir(d);
}
int __wrap_closedir(DIR *d) {
    const bool error = fail("closedir", dirs[d]); dirs.erase(d);
    const int rc = __real_closedir(d); return error ? -1 : rc;
}
bool r13_backend_mounted() { return mounted; }
}
static void put(const char *p, const std::string &bytes) {
    int fd = __real_open(mapped(p).c_str(), O_CREAT|O_TRUNC|O_WRONLY, 0600); assert(fd >= 0);
    assert(__real_write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size())); assert(__real_close(fd) == 0);
}
static std::string get(const char *p) {
    int fd = __real_open(mapped(p).c_str(), O_RDONLY); if (fd < 0) { return {}; }
    char bytes[80]{}; ssize_t n = __real_read(fd, bytes, sizeof(bytes)); __real_close(fd); return std::string(bytes, n);
}
static constexpr const char *path = "/data/config/bluetooth_config";
static const std::string off = "{\"enable\":false}", on = "{\"enable\":true}";
int main(int argc, char **argv) {
    assert(argc == 3); root = argv[1]; const std::string scenario = argv[2];
    ::mkdir((root+"/data").c_str(), 0700); ::mkdir((root+"/data/config").c_str(), 0700);
    BluetoothConfigStore store; bool enabled = true;
    if (scenario == "missing") { assert(store.load(enabled) && !enabled); }
    else if (scenario == "unbound") { assert(!store.load(enabled) && !enabled); assert(!store.save(true)); }

    else if (scenario == "save") { assert(store.save(true)); assert(store.load(enabled) && enabled); }
    else if (scenario == "positions") {
        unsigned plans = 0;
        auto reset = []() {
            for (const char *p : {path, "/data/config/bluetooth_config.tmp", "/data/config/bluetooth_config.bak"}) {
                __real_unlink(mapped(p).c_str());
            }
            put(path, off); hits = 0; target.clear();
        };
        for (const char *operation : {"open", "read", "close", "closedir", "fsync", "rename", "unlink"}) {
            reset(); fault = operation; fail_at = 100000;
            BluetoothConfigStore baseline; assert(baseline.save(true)); const unsigned count = hits;
            assert(count > 0);
            for (unsigned position = 1; position <= count; ++position) {
                reset(); fault = operation; fail_at = position;
                BluetoothConfigStore current; assert(!current.save(true)); fault.clear();
                BluetoothConfigStore reboot; assert(reboot.load(enabled) && !enabled);
                assert(reboot.save(true)); ++plans;
            }
        }
        printf("target adapter fault positions=%u\n", plans);
    }
    else if (scenario == "journal" || scenario.find("bak-open") == 0) {
        put(path, on); put("/data/config/bluetooth_config.bak", off);
        if (scenario != "journal") { fault = "open"; target = "/data/config/bluetooth_config.bak"; }
        if (scenario == "bak-open-nomem") { failure_errno = ENOMEM; }
        if (scenario == "bak-open-emfile") { failure_errno = EMFILE; }
        if (scenario == "bak-open-noent") { failure_errno = ENOENT; }
        const bool ok = store.load(enabled); assert(!enabled); assert(ok == (scenario == "journal")); assert(mutations == 0);
    } else if (scenario == "rollback") {
        put(path, off); fault = "rollback"; assert(!store.save(true)); fault.clear();
        BluetoothConfigStore reboot; assert(reboot.load(enabled) && !enabled);
    } else if (scenario == "write" || scenario == "fsync" || scenario == "rename" || scenario == "unlink") {
        put(path, off); fault = scenario; assert(!store.save(true)); fault.clear();
        BluetoothConfigStore reboot; assert(reboot.load(enabled) && !enabled); assert(reboot.save(true));
    } else if (scenario == "mount" || scenario == "ancestor" || scenario == "ancestor-missing" || scenario == "directory") {
        put(path, on);
        if (scenario == "mount") { mounted = false; }
        if (scenario == "ancestor") { fault = "opendir"; target = "/data/config"; }
        if (scenario == "ancestor-missing") { __real_unlink(mapped(path).c_str()); ::rmdir((root+"/data/config").c_str()); }
        if (scenario == "directory") { __real_unlink(mapped(path).c_str()); ::mkdir(mapped(path).c_str(), 0700); }
        assert(!store.load(enabled) && !enabled); hits = 0; assert(!store.save(true)); assert(mutations == 0);
    } else {
        put(path, scenario == "long-prefix" ? on+"x" : scenario == "bad-size" ? "bad" : on);
        target = path; fault = scenario;
        if (scenario == "canonical-open") { fault = "open"; }
        if (scenario == "read-eof") { fault = "read"; fail_at = 2; }
        if (scenario == "readdir" || scenario == "closedir") { target = "/data/config"; }
        const bool ok = store.load(enabled);
        if (scenario == "short-read") { assert(ok && enabled); }
        else { assert(!ok && !enabled); }
        assert(mutations == 0); assert(get(path) == (scenario == "long-prefix" ? on+"x" : scenario == "bad-size" ? "bad" : on));
    }
}
