#pragma once
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <pthread.h>

// Model IDF pthread initialization's two fallible resources: the pthread
// object allocation, then xSemaphoreCreateMutex's allocation. Compile the
// actual production callers with target libstdc++ constructor semantics.
namespace SyncFaultProbe {
inline unsigned attempts = 0, fail_at = 0, failures = 0, invalid_uses = 0;
inline bool watching = false;
struct Entry { pthread_mutex_t *mutex = nullptr; void *object = nullptr; void *semaphore = nullptr; };
inline Entry live[32];
inline pthread_mutex_t *invalid[32]{};
inline unsigned count() { unsigned n = 0; for (auto &e : live) { n += e.mutex != nullptr; } return n; }
inline void begin(unsigned fault) { attempts = failures = invalid_uses = 0; fail_at = fault; watching = true; }
inline void end() { watching = false; }
inline void *allocate() {
    if (watching && ++attempts == fail_at) { ++failures; return nullptr; }
    return std::malloc(1);
}
inline bool failed(pthread_mutex_t *mutex) {
    for (auto p : invalid) { if (p == mutex) { return true; } }
    return false;
}
inline void mark(pthread_mutex_t *mutex) {
    for (auto &p : invalid) { if (!p || p == mutex) { p = mutex; return; } }
    assert(false);
}
inline void forget(pthread_mutex_t *mutex) { for (auto &p : invalid) { if (p == mutex) { p = nullptr; } } }
}
extern "C" int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
extern "C" int __real_pthread_mutex_destroy(pthread_mutex_t *);
extern "C" int __real_pthread_mutex_lock(pthread_mutex_t *);
extern "C" int __real_pthread_mutex_trylock(pthread_mutex_t *);
extern "C" int __real_pthread_mutex_unlock(pthread_mutex_t *);
extern "C" int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    using namespace SyncFaultProbe;
    forget(mutex);
    void *object = allocate();
    if (!object) { mark(mutex); return ENOMEM; }
    void *semaphore = allocate();
    if (!semaphore) { std::free(object); mark(mutex); return ENOMEM; }
    const int result = __real_pthread_mutex_init(mutex, attr);
    if (result) { std::free(object); std::free(semaphore); mark(mutex); return result; }
    for (auto &e : live) { if (!e.mutex) { e = {mutex, object, semaphore}; return 0; } }
    assert(false); return ENOMEM;
}
extern "C" int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex) {
    using namespace SyncFaultProbe;
    if (failed(mutex)) { ++invalid_uses; forget(mutex); return EINVAL; }
    for (auto &e : live) {
        if (e.mutex == mutex) {
            const int result = __real_pthread_mutex_destroy(mutex);
            assert(result == 0); std::free(e.object); std::free(e.semaphore); e = {}; return result;
        }
    }
    return __real_pthread_mutex_destroy(mutex);
}
extern "C" int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (SyncFaultProbe::failed(mutex)) { ++SyncFaultProbe::invalid_uses; return EINVAL; }
    return __real_pthread_mutex_lock(mutex);
}
extern "C" int __wrap_pthread_mutex_trylock(pthread_mutex_t *mutex) {
    if (SyncFaultProbe::failed(mutex)) { ++SyncFaultProbe::invalid_uses; return EINVAL; }
    return __real_pthread_mutex_trylock(mutex);
}
extern "C" int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (SyncFaultProbe::failed(mutex)) { ++SyncFaultProbe::invalid_uses; return EINVAL; }
    return __real_pthread_mutex_unlock(mutex);
}
