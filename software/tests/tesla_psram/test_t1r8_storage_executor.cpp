// T1r8EXT executor regression (HOST ONLY). Links the REAL tesla_ble_storage.cpp + tesla_storage_executor.h with
// real POSIX files in a scratch directory. Every POSIX call made from storage/executor code is --wrap'ed and
// attributed to the calling thread label: a call on a "PSRAM-stack" (worker) thread is a failure.
// Dummy bytes below are a lab pattern, never a real key.
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include "modules/tesla_vehicle/tesla_ble_storage.h"
#include "modules/tesla_vehicle/tesla_ble_storage_io.h"

namespace ex = tesla_storage_executor; namespace pl = tesla_storage_executor::plat;
static std::atomic<int> posix_on_psram{0}, posix_on_internal{0};
static std::atomic<bool> hold{false}; static std::atomic<int> held{0};
static void account() {
    (pl::tl_psram ? posix_on_psram : posix_on_internal)++;
    if (!pl::tl_psram && pl::tl_task && hold.load()) { held++; while (hold.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); } }
}
extern "C" {
int __real_open(const char *, int, ...); int __real_stat(const char *, struct stat *); int __real_fstat(int, struct stat *);
ssize_t __real_read(int, void *, size_t); ssize_t __real_write(int, const void *, size_t); int __real_close(int);
int __real_unlink(const char *); int __real_rename(const char *, const char *); int __real_fsync(int);
int __wrap_open(const char *p, int f, ...) { va_list a; va_start(a, f); mode_t m = va_arg(a, int); va_end(a); account(); return __real_open(p, f, m); }
int __wrap_stat(const char *p, struct stat *s) { account(); return __real_stat(p, s); }
int __wrap_fstat(int d, struct stat *s) { account(); return __real_fstat(d, s); }
ssize_t __wrap_read(int d, void *b, size_t n) { account(); return __real_read(d, b, n); }
ssize_t __wrap_write(int d, const void *b, size_t n) { account(); return __real_write(d, b, n); }
int __wrap_close(int d) { account(); return __real_close(d); }
int __wrap_unlink(const char *p) { account(); return __real_unlink(p); }
int __wrap_rename(const char *a, const char *b) { account(); return __real_rename(a, b); }
int __wrap_fsync(int d) { account(); return __real_fsync(d); }
}
static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
using LR = TeslaBleStorageAdapter::LoadResult; using SR = TeslaBleStorageAdapter::SaveResult;
template <typename F> static void on_worker(F f) { std::thread t([&] { pl::tl_psram = true; f(); }); t.join(); }
static int open_fds() { int n = 0; DIR *d = opendir("/proc/self/fd"); while (readdir(d)) { n++; } closedir(d); return n; }
static bool wait_until(std::function<bool()> c, int ms = 3000) { for (int i = 0; i < ms; ++i) { if (c()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(1)); } return c(); }
static void settle() { CHECK(wait_until([] { return ex::stats.live_jobs.load() == 0; })); }

int main() {
    const int fds0 = open_fds();
    CHECK(ex::start());
    TeslaBleStorageAdapter store;
    CHECK(store.select_profile(15, 0x025431000001ULL));
    uint8_t key[300]; for (size_t i = 0; i < sizeof(key); ++i) { key[i] = static_cast<uint8_t>('A' + i % 26); }
    // 1 positive roundtrip on the simulated PSRAM worker: save (tmp write/fsync/verify/rename), load, remove, absent.
    on_worker([&] {
        CHECK(store.save_transaction("private_key", key, sizeof(key)) == SR::Committed);
        uint8_t out[512]; size_t n = 0;
        CHECK(store.load_into("private_key", out, sizeof(out), n) == LR::Loaded && n == sizeof(key) && !memcmp(out, key, n));
        CHECK(store.save_transaction("private_key", key, 200) == SR::Committed); // second commit: backup rotation path
        CHECK(store.load_into("private_key", out, sizeof(out), n) == LR::Loaded && n == 200);
        CHECK(store.remove_key("private_key"));
        CHECK(store.load_into("private_key", out, sizeof(out), n) == LR::Absent);
    });
    CHECK(posix_on_psram.load() == 0); CHECK(posix_on_internal.load() > 0);
    CHECK(ex::stats.submitted.load() > 0 && ex::stats.submitted.load() == ex::stats.completed.load());
    CHECK(ex::stats.timeouts.load() == 0 && !ex::quarantine.load()); settle();
    std::printf("roundtrip: submitted=%u posix_internal=%d posix_psram=%d\n", ex::stats.submitted.load(), posix_on_internal.load(), posix_on_psram.load());
    // 2 main/internal-stack callers execute directly, never queue: no deadlock with a stalled executor.
    hold = true;
    on_worker([] { struct stat st{}; ex::timeout_ms = 100; CHECK(tesla_storage_fs::fs_stat("/", &st) == -1 && errno == EIO); });
    CHECK(wait_until([] { return held.load() > 0; }));
    { const uint32_t sub = ex::stats.submitted.load(); const auto t0 = pl::now_ms(); uint8_t out[16]; size_t n = 0;
      TeslaBleStorageIO::close_quarantine = false; const bool q = ex::quarantine.exchange(false);
      CHECK(store.load_into("authorized", out, sizeof(out), n) == LR::Absent); // main thread, executor still held
      CHECK(ex::stats.submitted.load() == sub && pl::now_ms() - t0 < 100); ex::quarantine = q; }
    hold = false; settle(); CHECK(ex::stats.timeouts.load() == 1 && ex::quarantine.load());
    CHECK(TeslaBleStorageIO::restart_required()); // a timeout quarantines storage until reboot
    ex::quarantine = false; held = 0;
    // 3 timeout of an open: caller gets Error (never Absent), executor closes the orphan FD and frees the Job.
    on_worker([&] { CHECK(store.save_transaction("vehicle_vin", key, 17) == SR::Committed); });
    hold = true; ex::timeout_ms = 100;
    on_worker([&] { uint8_t out[32]; size_t n = 0; CHECK(store.load_into("vehicle_vin", out, sizeof(out), n) == LR::Error && n == 0); });
    CHECK(ex::stats.live_jobs.load() >= 1); hold = false; settle();
    CHECK(ex::stats.orphan_fds_closed.load() == 1 && ex::quarantine.load());
    on_worker([&] { uint8_t out[32]; size_t n = 0; CHECK(store.load_into("vehicle_vin", out, sizeof(out), n) == LR::RestartRequired); });
    ex::quarantine = false; ex::timeout_ms = 5000;
    // 4 allocation failure: EIO -> Error/Unknown, never Absent; nothing queued.
    on_worker([&] { const uint32_t sub = ex::stats.submitted.load(); ex::inject_alloc_failures = 1; uint8_t out[32]; size_t n = 0;
        CHECK(store.load_into("warp_ev_mac", out, sizeof(out), n) == LR::Error); CHECK(ex::stats.submitted.load() == sub);
        ex::inject_alloc_failures = 1; CHECK(store.private_key_presence() == TeslaBleStorageAdapter::KeyPresence::Unknown); });
    CHECK(ex::stats.alloc_failures.load() == 2); settle();
    // 5 queue full: abandoned calls pile up behind a stalled executor; the overflow is refused (EIO) without a leak.
    hold = true; ex::timeout_ms = 30;
    on_worker([] { struct stat st{}; for (int i = 0; i < 7; ++i) { CHECK(tesla_storage_fs::fs_stat("/", &st) == -1 && errno == EIO); } });
    CHECK(ex::stats.queue_full.load() >= 1); hold = false; settle(); ex::quarantine = false; ex::timeout_ms = 5000;
    // 6 stop while a call is stalled: no reap until the FIFO drained; then delete; submit after stop is refused.
    hold = true; ex::timeout_ms = 50;
    on_worker([] { uint8_t b[8]; CHECK(tesla_storage_fs::fs_write(-1, b, sizeof(b)) == -1); });
    ex::request_stop(); CHECK(!ex::reap());
    on_worker([] { struct stat st{}; const uint32_t r = ex::stats.refused.load(); CHECK(tesla_storage_fs::fs_stat("/", &st) == -1 && errno == EIO && ex::stats.refused.load() == r + 1); });
    hold = false; CHECK(wait_until([] { return ex::reap(); })); CHECK(ex::stats.phase.load() == ex::Absent); settle();
    ex::quarantine = false; ex::timeout_ms = 5000;
    // 7 on-demand restart after reap; failed executor creation leaves nothing behind; idle stop reaps cleanly.
    pl::fail_task_create = true; CHECK(!ex::start()); CHECK(ex::stats.phase.load() == ex::Absent);
    CHECK(ex::start()); on_worker([&] { uint8_t out[32]; size_t n = 0; CHECK(store.load_into("vehicle_vin", out, sizeof(out), n) == LR::Loaded && n == 17); });
    CHECK(store.remove_key("vehicle_vin"));  // main thread, direct
    ex::request_stop(); CHECK(wait_until([] { return ex::reap(); }));
    CHECK(ex::stats.creates.load() == 2 && ex::stats.deletes.load() == 2);
    // ownership/leak/zeroize/backstop totals
    CHECK(ex::stats.live_jobs.load() == 0 && pl::live_allocs.load() == 0);
    CHECK(pl::nonzero_frees.load() == 0);          // every freed Job was zeroized
    CHECK(pl::queues_deleted_nonempty.load() == 0);
    CHECK(pl::backstop_denials.load() == 0);       // routing never fell through to the PSRAM direct path
    CHECK(posix_on_psram.load() == 0);
    CHECK(open_fds() == fds0);                      // no FD leaked (orphan closed)
    std::printf("stats: submitted=%u completed=%u abandoned=%u timeouts=%u queue_full=%u alloc_fail=%u refused=%u orphan_fds_closed=%u freed=%u routed=%u direct=%u job_bytes=%zu\n",
        ex::stats.submitted.load(), ex::stats.completed.load(), ex::stats.abandoned.load(), ex::stats.timeouts.load(), ex::stats.queue_full.load(),
        ex::stats.alloc_failures.load(), ex::stats.refused.load(), ex::stats.orphan_fds_closed.load(), ex::stats.freed.load(),
        ex::stats.routed_calls.load(), ex::stats.direct_calls.load(), sizeof(ex::Job));
    std::printf("%s checks=%d failures=%d\n", failures ? "FAILED" : "OK", checks, failures);
    return failures ? 1 : 0;
}
