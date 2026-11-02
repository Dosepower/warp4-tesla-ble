// T1r7EXT narrow regression (HOST ONLY): links the REAL tesla_ble_storage.cpp (ESP/POSIX branch, i.e.
// TESLA_BLE_HOST_FILESYSTEM undefined) with -DTESLA_WORKER_PSRAM_LAB=1 against the T1r6 host stubs.
// Every POSIX file call made from tesla_ble_storage.o is redirected by -Wl,--wrap to an in-memory fake FS
// and counted, so "zero POSIX I/O" is measured at the real call sites, not inferred.
// Simulated SP: hs::sp in the labelled fake PSRAM / INTERNAL arenas (host stub, NOT ESP placement proof).
// Fake key material below is a lab dummy, never a real key.
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include "modules/tesla_vehicle/tesla_ble_storage.h"
#include "modules/tesla_vehicle/tesla_worker_psram_lab.h"

namespace fakefs {
std::map<std::string, std::string> files;
struct Fd { std::string path; size_t pos; };
std::map<int, Fd> fds; int next_fd = 100;
int open_calls = 0, stat_calls = 0, fstat_calls = 0, read_calls = 0, close_calls = 0, mutate_calls = 0;
int total() { return open_calls + stat_calls + fstat_calls + read_calls + close_calls + mutate_calls; }
void reset_counts() { open_calls = stat_calls = fstat_calls = read_calls = close_calls = mutate_calls = 0; }
}
using namespace fakefs;
extern "C" {
int __wrap_open(const char *p, int flags, ...) {
    open_calls++;
    if ((flags & O_ACCMODE) != O_RDONLY) { mutate_calls++; errno = EACCES; return -1; }
    if (!files.count(p)) { errno = ENOENT; return -1; }
    fds[next_fd] = {p, 0}; return next_fd++;
}
int __wrap_open64(const char *p, int flags, ...) { return __wrap_open(p, flags); }
int __wrap_fstat(int fd, struct stat *st) {
    fstat_calls++; auto it = fds.find(fd); if (it == fds.end()) { errno = EBADF; return -1; }
    std::memset(st, 0, sizeof(*st)); st->st_mode = S_IFREG | 0600; st->st_size = files[it->second.path].size(); return 0;
}
int __wrap_fstat64(int fd, struct stat *st) { return __wrap_fstat(fd, st); }
int __wrap_stat(const char *p, struct stat *st) {
    stat_calls++; if (!files.count(p)) { errno = ENOENT; return -1; }
    std::memset(st, 0, sizeof(*st)); st->st_mode = S_IFREG | 0600; st->st_size = files[p].size(); return 0;
}
int __wrap_stat64(const char *p, struct stat *st) { return __wrap_stat(p, st); }
ssize_t __wrap_read(int fd, void *b, size_t n) {
    read_calls++; auto it = fds.find(fd); if (it == fds.end()) { errno = EBADF; return -1; }
    const std::string &d = files[it->second.path]; size_t k = std::min(n, d.size() - it->second.pos);
    std::memcpy(b, d.data() + it->second.pos, k); it->second.pos += k; return (ssize_t)k;
}
int __wrap_close(int fd) { close_calls++; return fds.erase(fd) ? 0 : (errno = EBADF, -1); }
int __wrap_unlink(const char *) { mutate_calls++; errno = EACCES; return -1; }
int __wrap_rename(const char *, const char *) { mutate_calls++; errno = EACCES; return -1; }
ssize_t __wrap_write(int, const void *, size_t) { mutate_calls++; errno = EACCES; return -1; }
int __wrap_fsync(int) { mutate_calls++; errno = EACCES; return -1; }
}

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
using LR = TeslaBleStorageAdapter::LoadResult; using KP = TeslaBleStorageAdapter::KeyPresence;
namespace lab = tesla_worker_psram_lab;
static void on_psram() { hs::sp = hs::psram + hs::ARENA / 2; }
static void on_internal() { hs::sp = hs::internal + hs::ARENA / 2; }
static const char *KEY_PATH = "/data/config/tesla_ble_private_key";
static const std::string DUMMY = "-----BEGIN EC PRIVATE KEY-----\nTEFCLURVTU1ZLU5PVC1BLUtFWQ==\n-----END EC PRIVATE KEY-----\n";

// Worker stop path on a PSRAM stack: apply_profile/private_key_presence -> load_into.
static void psram_stop_step(TeslaBleStorageAdapter &s, const char *tag) {
    on_psram(); reset_counts();
    const uint32_t d0 = lab::proof.flash_denials.load();
    uint8_t buf[512]; size_t size = 77;
    CHECK(s.load_into("private_key", buf, sizeof(buf), size) == LR::Error);
    CHECK(size == 0);
    CHECK(lab::proof.flash_denials.load() == d0 + 1);
    const KP p = s.private_key_presence();
    CHECK(p == KP::Unknown);
    CHECK(p != KP::Absent);
    CHECK(!s.has_private_key());
    std::printf("[%s] psram: posix_total=%d open=%d stat=%d read=%d close=%d mutate=%d presence=%d denials+%u\n", tag,
                total(), open_calls, stat_calls, read_calls, close_calls, mutate_calls, (int)p, lab::proof.flash_denials.load() - d0);
    CHECK(total() == 0);
    CHECK(open_calls == 0);
    CHECK(fds.empty());
}

int main() {
    TeslaBleStorageAdapter s;
    // 1) PSRAM stack, key present (the T1r6EXT stop-panic case): zero POSIX I/O, Error -> Unknown.
    files.clear(); files[KEY_PATH] = DUMMY;
    psram_stop_step(s, "key-present");
    // 2) PSRAM stack, key absent: still Unknown, never Absent (no absent-cleanup decision without I/O).
    files.clear();
    psram_stop_step(s, "key-absent");
    // 3) Internal stack, key present: real open/fstat/read/close path runs (positive control).
    on_internal(); files.clear(); files[KEY_PATH] = DUMMY; reset_counts();
    const uint32_t d0 = lab::proof.flash_denials.load();
    uint8_t buf[512]; size_t size = 0;
    CHECK(s.load_into("private_key", buf, sizeof(buf), size) == LR::Loaded);
    CHECK(size == DUMMY.size() && std::memcmp(buf, DUMMY.data(), size) == 0);
    CHECK(open_calls >= 1 && fstat_calls >= 1 && read_calls >= 1 && close_calls >= 1);
    CHECK(s.private_key_presence() == KP::Present);
    CHECK(lab::proof.flash_denials.load() == d0);
    std::printf("[internal key-present] open=%d fstat=%d read=%d close=%d\n", open_calls, fstat_calls, read_calls, close_calls);
    // 4) Internal stack, key absent: production Absent semantics preserved (open -> ENOENT, stat probes).
    files.clear(); reset_counts();
    CHECK(s.load_into("private_key", buf, sizeof(buf), size) == LR::Absent);
    CHECK(s.private_key_presence() == KP::Absent);
    CHECK(open_calls >= 2 && stat_calls >= 1 && mutate_calls == 0);
    std::printf("[internal key-absent] open=%d stat=%d\n", open_calls, stat_calls);
    // 5) Stop/restart cycles: stop on PSRAM stack -> Unknown/0 I/O; restart (internal) -> real I/O again.
    for (int cycle = 0; cycle < 3; ++cycle) {
        files.clear(); files[KEY_PATH] = DUMMY;
        char tag[32]; std::snprintf(tag, sizeof(tag), "cycle%d-stop", cycle);
        psram_stop_step(s, tag);
        on_internal(); reset_counts();
        CHECK(s.private_key_presence() == KP::Present);
        CHECK(open_calls >= 1);
    }
    CHECK(fds.empty());
    std::printf("HOST-STUB + REAL tesla_ble_storage.cpp (fake PSRAM/INTERNAL SP; not ESP placement proof): %d/%d checks passed\n",
                checks - failures, checks);
    return failures ? 1 : 0;
}
