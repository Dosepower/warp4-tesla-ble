// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/support.hpp>
#include <array>
#include <cstring>
#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#if defined(TESLA_PSRAM_PLACEMENT) && TESLA_PSRAM_PLACEMENT
#include <atomic>
#include <freertos/idf_additions.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#define TESLA_CHECKED_MUTEX_PSRAM 1
#endif
#else
#include <mutex>
#endif

// Independently authored WARP boundary utilities. The core remains single-worker;
// module publications use a native mutex, not the core's single-owner lock state.
namespace tesla_warp {
#if defined(TESLA_CHECKED_MUTEX_PSRAM)
// T1r11 LAB: placement proof for CheckedMutex FreeRTOS objects (read from the real handle address).
struct MutexProof { std::atomic<uint32_t> created{0}, external{0}, non_external{0}, failures{0}, deleted{0}; };
inline MutexProof mutex_proof;
#endif
template<typename Signature> class FunctionRef;
template<typename R,typename... A> class FunctionRef<R(A...)> {
    void *context_;
    R (*call_)(void *,A...);
public:
    template<typename F> FunctionRef(F &&f) : context_(const_cast<void *>(static_cast<const void *>(&f))),
        call_([](void *p,A... a)->R { return (*static_cast<std::remove_reference_t<F> *>(p))(std::forward<A>(a)...); }) {}
    R operator()(A... a) const { return call_(context_,std::forward<A>(a)...); }
};
class CheckedMutex {
#ifdef ESP_PLATFORM
#if defined(TESLA_CHECKED_MUTEX_PSRAM)
    // T1r11 LAB: StaticQueue_t of the mutex in PSRAM (xSemaphoreCreateMutexWithCaps). Task context only (Guard uses 0 or
    // portMAX_DELAY, no ISR give/take); its spinlock uses the esp_cpu_compare_and_set external-RAM path. No fallback:
    // failure leaves handle_ null, so lock() fails exactly like the previous internal allocation failure.
    SemaphoreHandle_t handle_=create_psram();
    static SemaphoreHandle_t create_psram() {
        SemaphoreHandle_t h=xSemaphoreCreateMutexWithCaps(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!h) { mutex_proof.failures.fetch_add(1); return nullptr; }
        mutex_proof.created.fetch_add(1);
        (esp_ptr_external_ram(h)?mutex_proof.external:mutex_proof.non_external).fetch_add(1);
        return h;
    }
#else
    SemaphoreHandle_t handle_=xSemaphoreCreateMutex();
#endif
    bool lock(bool try_only) { return handle_ && xSemaphoreTake(handle_,try_only?0:portMAX_DELAY)==pdTRUE; }
    void unlock() { xSemaphoreGive(handle_); }
#else
    std::mutex *handle_=new(std::nothrow) std::mutex;
    bool lock(bool try_only) { if(!handle_) { return false; } if(try_only) { return handle_->try_lock(); } handle_->lock(); return true; }
    void unlock() { handle_->unlock(); }
#endif
public:
    CheckedMutex()=default;
    CheckedMutex(const CheckedMutex &)=delete;
    CheckedMutex &operator=(const CheckedMutex &)=delete;
    bool initialized() const { return handle_!=nullptr; }
    ~CheckedMutex() {
#if defined(TESLA_CHECKED_MUTEX_PSRAM)
        if(handle_) { vSemaphoreDeleteWithCaps(handle_); mutex_proof.deleted.fetch_add(1); }
#elif defined(ESP_PLATFORM)
        if(handle_) { vSemaphoreDelete(handle_); }
#else
        delete handle_;
#endif
    }
    class Guard {
        CheckedMutex *owner_;
    public:
        explicit Guard(CheckedMutex &mutex,bool try_only=false) : owner_(mutex.lock(try_only)?&mutex:nullptr) {}
        Guard(const Guard &)=delete;
        Guard &operator=(const Guard &)=delete;
        ~Guard() { if(owner_) { owner_->unlock(); } }
        explicit operator bool() const { return owner_!=nullptr; }
    };
};
// Only for bounded scalar publication by task-context writers. ESP masks local
// interrupts and excludes the other core for the fixed store sequence: no task
// semaphore wait, allocation, callback, IO, or nested lock is permitted inside.
// Native mutex models the same writer exclusion for host race detection.
class ScalarPublicationMutex {
#ifdef ESP_PLATFORM
    // Static writable storage is internal DRAM, unlike Runtime which may be
    // allocated in PSRAM. Share this tiny lock across snapshot instances; never
    // nest publications. ESP32 CAS must not target an external-RAM portMUX.
    inline static portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
#else
    CheckedMutex mutex_;
#endif
public:
    bool initialized() const {
#ifdef ESP_PLATFORM
        return true; // Static portMUX, no allocation can fail.
#else
        return mutex_.initialized();
#endif
    }
    class Guard {
#ifdef ESP_PLATFORM
        ScalarPublicationMutex &owner_;
#else
        CheckedMutex::Guard guard_;
#endif
    public:
        explicit Guard(ScalarPublicationMutex &owner)
#ifdef ESP_PLATFORM
            : owner_(owner) { portENTER_CRITICAL(&owner_.mutex_); }
        ~Guard() { portEXIT_CRITICAL(&owner_.mutex_); }
        explicit operator bool() const { return true; }
#else
            : guard_(owner.mutex_) {}
        explicit operator bool() const { return bool(guard_); }
#endif
        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;
    };
};
class FixedVin {
    std::array<char,18> text_{};
public:
    bool assign(const char *s) { if(!s || std::strlen(s)>17) { return false; } std::strncpy(text_.data(),s,17); text_[17]=0; return true; }
    const char *c_str() const { return text_.data(); }
};
}
