#pragma once
// Test-only access to the production TeslaBleCommandOutcome publication mutex.
// Explicit template instantiation is exempt from access checking ([temp.explicit]),
// so no production declaration is widened. Used only to model the MAIN task's
// zero-wait publication critical section overlapping a worker WRITE gate.
namespace card_confirm_access {
struct OutcomeMutex { using type = tesla_warp::CheckedMutex TeslaBleCommandOutcome::*; friend type get(OutcomeMutex); };
struct MutexLock { using type = bool (tesla_warp::CheckedMutex::*)(bool); friend type get(MutexLock); };
struct MutexUnlock { using type = void (tesla_warp::CheckedMutex::*)(); friend type get(MutexUnlock); };
template <class Tag, typename Tag::type Member> struct Rob { friend typename Tag::type get(Tag) { return Member; } };
template struct Rob<OutcomeMutex, &TeslaBleCommandOutcome::mutex>;
template struct Rob<MutexLock, &tesla_warp::CheckedMutex::lock>;
template struct Rob<MutexUnlock, &tesla_warp::CheckedMutex::unlock>;
inline void lock(TeslaBleCommandOutcome &o) { auto &m = o.*get(OutcomeMutex{}); const bool held = (m.*get(MutexLock{}))(false); assert(held); (void)held; }
inline void unlock(TeslaBleCommandOutcome &o) { auto &m = o.*get(OutcomeMutex{}); (m.*get(MutexUnlock{}))(); }
}
