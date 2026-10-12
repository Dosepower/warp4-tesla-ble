// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/support.hpp>
#include <cassert>
#include <cstdio>
#include <type_traits>
#include <utility>
using namespace tesla_clean;
struct UnsafeMove { UnsafeMove()=default; UnsafeMove(UnsafeMove &&) noexcept(false) {} void operator()() {} };
struct SafeMove { SafeMove()=default; SafeMove(SafeMove &&) noexcept=default; SafeMove(const SafeMove &) noexcept(false) {} void operator()() {} };
template<typename F,typename=void> struct CanInstall : std::false_type {};
template<typename F> struct CanInstall<F,std::void_t<decltype(std::declval<CheckedCallback<void()> &>().assign(std::declval<F>()))>> : std::true_type {};
int main()
{
    assert(!CanInstall<UnsafeMove>::value); // no copying/moving throwing capture after a checked allocation
    assert(CanInstall<SafeMove>::value);
    assert(!CanInstall<SafeMove &>::value); // unsafe lvalue copy cannot sneak in through forwarding
    CheckedCallback<void()> cb; assert(cb.assign(SafeMove{})); cb();
    std::puts("Clean no-exceptions callback capture: nothrow move accepted, throwing move/copy unavailable passed");
}
