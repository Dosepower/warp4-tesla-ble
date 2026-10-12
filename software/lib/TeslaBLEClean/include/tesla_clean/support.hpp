// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>
namespace tesla_clean {
struct Allocation {
    static void *get(void *,size_t n) { return std::malloc(n); }
    static void put(void *,void *p) { std::free(p); }
    void *(*allocate)(void *,size_t)=get;
    void (*release)(void *,void *)=put;
    void *context=nullptr;
};
template<typename Signature> class CheckedCallback;
template<typename R,typename... Args> class CheckedCallback<R(Args...)> {
    struct Node {
        size_t references=1;
        Allocation allocation;
        R (*invoke)(Node *,Args...);
        void (*destroy)(Node *);
    };
    template<typename F> struct Owned : Node {
        F function;
        template<typename G> Owned(G &&f,Allocation a) : function(std::forward<G>(f))
        {
            this->allocation=a;
            this->invoke=[](Node *n,Args... args)->R {
                return static_cast<Owned *>(n)->function(std::forward<Args>(args)...);
            };
            this->destroy=[](Node *n) {
                const Allocation allocation=n->allocation;
                static_cast<Owned *>(n)->~Owned();
                allocation.release(allocation.context,n);
            };
        }
    };
    static void release(Node *n) { if(n && --n->references==0) { n->destroy(n); } }
    struct Pin { Node *node; ~Pin() { release(node); } };
    Node *node_=nullptr;
public:
    CheckedCallback()=default;
    ~CheckedCallback() { clear(); }
    CheckedCallback(const CheckedCallback &)=delete;
    CheckedCallback &operator=(const CheckedCallback &)=delete;
    CheckedCallback(CheckedCallback &&other) noexcept : node_(std::exchange(other.node_,nullptr)) {}
    CheckedCallback &operator=(CheckedCallback &&other) noexcept
    {
        if(this!=&other) { clear(); node_=std::exchange(other.node_,nullptr); }
        return *this;
    }
    template<typename F,typename=std::enable_if_t<std::is_nothrow_constructible_v<std::decay_t<F>,F &&> &&
                                                  std::is_nothrow_destructible_v<std::decay_t<F>>>>
    bool assign(F &&f,Allocation allocation={})
    {
        using Holder=Owned<std::decay_t<F>>;
        static_assert(alignof(Holder)<=alignof(std::max_align_t),"Unsupported over-aligned capture");
        if(!allocation.allocate || !allocation.release) { return false; }
        void *memory=allocation.allocate(allocation.context,sizeof(Holder));
        if(!memory) { return false; }
        // Placement construction uses a checked allocation and never throwing new.
        auto *candidate=new(memory) Holder(std::forward<F>(f),allocation);
        Node *old=std::exchange(node_,candidate); release(old);
        return true;
    }
    void clear() { release(std::exchange(node_,nullptr)); }
    explicit operator bool() const { return node_!=nullptr; }
    R operator()(Args... args) const
    {
        Node *current=node_;
        if(!current || current->references==std::numeric_limits<size_t>::max()) { return R(); }
        ++current->references; Pin pin{current};
        return current->invoke(current,std::forward<Args>(args)...);
    }
};
struct MutexOps {
    void *(*create)(void *)=nullptr;
    bool (*lock)(void *,void *,uint32_t)=nullptr;
    void (*unlock)(void *,void *)=nullptr;
    void (*destroy)(void *,void *)=nullptr;
    void *context=nullptr;
};
class CheckedMutex {
    MutexOps ops_{};
    void *handle_=nullptr;
    bool locked_=false;
public:
    CheckedMutex()=default;
    CheckedMutex(const CheckedMutex &)=delete;
    CheckedMutex &operator=(const CheckedMutex &)=delete;
    ~CheckedMutex() { unlock(); if(handle_) { ops_.destroy(ops_.context,handle_); } }
    bool initialize(MutexOps ops)
    {
        if(handle_ || !ops.create || !ops.lock || !ops.unlock || !ops.destroy) { return false; }
        void *candidate=ops.create(ops.context);
        if(!candidate) { return false; }
        ops_=ops; handle_=candidate; return true;
    }
    bool ready() const { return handle_!=nullptr; }
    bool lock(uint32_t timeout)
    {
        if(!handle_ || locked_ || !ops_.lock(ops_.context,handle_,timeout)) { return false; }
        locked_=true; return true;
    }
    void unlock()
    {
        if(locked_) { locked_=false; ops_.unlock(ops_.context,handle_); }
    }
};
}
