#include <cassert>
#include <future>
#include <thread>
#include "../../src/modules/tesla_vehicle/tesla_ble_operation.h"

int main()
{
    TeslaBleOperation op;
    uint32_t clock = 1;
    auto now = [&]() { return clock; };
    auto pause = [&]() { ++clock; };
    auto no_cancel = []() { return false; };
    // An immediate submission error must not enter an indefinite wait.
    assert(!op.run(1, 2, 3, now, []() { return false; }, pause, no_cancel));
    assert(clock == 1);
    // Registration/descriptor completion never arrives, even after disconnect.
    assert(!op.run(2, 2, 4, now, []() { return true; }, pause, no_cancel));
    assert(op.retired() && clock <= 1 + TeslaBleOperation::TIMEOUT_MS + 1);
    TeslaBleOperation cancelled;
    clock = 1;
    assert(!cancelled.run(3, 2, 5, now, []() { return true; }, pause, [&]() { return clock > 3; }));
    assert(cancelled.retired() && clock == 4);
    // Terminal evidence is causal, not the time of a later main publication.
    assert(cancelled.elapsed_ms(200) == 3);
    // Correlation: wrong operation, handle and connection cannot complete a wait.
    TeslaBleOperation good;
    clock = 1;
    assert(good.run(4, 2, 6, now, [&]() {
        good.complete(4, 3, 6, true);
        good.complete(4, 2, 7, true);
        good.complete(3, 2, 6, true);
        return true;
    }, [&]() { good.complete(4, 2, 6, true); ++clock; }, no_cancel));
    // The fragmentation boundary must recheck stop/identity between submissions.
    int writes = 0;
    bool stop = false;
    assert(!tesla_ble_write_fragments(100, 20, [&]() { return stop; }, [&](size_t, size_t) {
        ++writes; stop = true; return true;
    }));
    assert(writes == 1);
    // IDF submission itself is stalled, even though a completion callback
    // already ran. Supervisor must retire ownership without joining/deleting it.
    TeslaBleOperation stalled;
    std::promise<void> entered, resume;
    auto signal = resume.get_future();
    std::thread worker([&]() {
        const bool accepted = stalled.run(4, 2, 6, []() { return 1U; }, [&]() {
            stalled.complete(4, 2, 6, true);
            entered.set_value(); signal.wait(); return true;
        }, []() {}, []() { return false; });
        assert(!accepted);
    });
    entered.get_future().wait();
    assert(stalled.supervise(TeslaBleOperation::TIMEOUT_MS + 2));
    assert(stalled.retired());
    resume.set_value();
    worker.join();
    assert(stalled.cause() == TeslaBleOperation::Cause::Deadline);
    assert(stalled.elapsed_ms(90000) == TeslaBleOperation::TIMEOUT_MS + 1);
    assert(stalled.kind() == 4 && stalled.generation() == 1);
    // Deterministic cancellation-first interleaving: a later supervisor cannot
    // relabel the earlier terminal or report the time it happened to observe it.
    assert(cancelled.supervise(20000));
    assert(cancelled.cause() == TeslaBleOperation::Cause::Cancelled);
    assert(cancelled.elapsed_ms(20000) == 3 && cancelled.kind() == 3);
    // A delayed supervisor clock sample cannot retire a newly armed successor.
    TeslaBleOperation successor;
    clock = 100;
    assert(successor.run(7, 2, 6, now, [&] {successor.complete(7, 2, 6, true);return true;}, pause, no_cancel));
    clock = 200;
    assert(successor.run(8, 2, 7, now, [&] {
        assert(!successor.supervise(100));
        successor.complete(8, 2, 7, true);return true;
    }, pause, no_cancel));
    assert(!successor.retired() && successor.generation() == 2 && successor.kind() == 8);
}
