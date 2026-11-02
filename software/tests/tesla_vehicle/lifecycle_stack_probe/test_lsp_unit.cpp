// LSP1 unit tests: real lifecycle_stack_probe.h (host build). Modes: ok | allocfail | uninit
#include "../../../src/modules/tesla_vehicle/lifecycle_stack_probe.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace L = lifecycle_stack_probe; using S = L::Stage;
static unsigned next_hwm = 0, hwm_calls = 0;
static unsigned fake_hwm(const void *) { ++hwm_calls; return next_hwm; }
struct Line { int kind; std::string what; uint32_t a, b, c, d; };
static std::vector<Line> lines;
static void sink(int k, const char *w, uint32_t a, uint32_t b, uint32_t c, uint32_t d) { lines.push_back({k, w, a, b, c, d}); }
int main(int argc, char **argv) {
    const std::string mode = argc > 1 ? argv[1] : "ok"; L::host_hwm = fake_hwm;
    if (mode == "uninit") { L::self(S::TaskEntry); L::handle(S::MainPreDelete, &mode); L::publish(sink);
        assert(L::dropped == 2 && hwm_calls == 0 && lines.empty()); std::puts("LSP_UNIT uninit PASS (dropped, no read, silent)"); return 0; }
    if (mode == "allocfail") { L::host_alloc = [](size_t) -> void * { return nullptr; };
        assert(!L::init() && L::state == L::Failed); L::self(S::Done); L::publish(sink); L::publish(sink);
        assert(L::dropped == 1 && hwm_calls == 0 && lines.size() == 1 && lines[0].kind == 0 && lines[0].what == "failed");
        std::puts("LSP_UNIT allocfail PASS (fail closed, one status line, no reads)"); return 0; }
    assert(L::init() && L::init() && L::state == L::Ready);
    L::publish(sink); assert(lines.size() == 1 && lines[0].what == "ready" && lines[0].c == sizeof(L::Table)); lines.clear();
    next_hwm = 3000; L::self(S::TaskEntry); next_hwm = 2000; L::self(S::TaskEntry); next_hwm = 2500; L::self(S::TaskEntry);
    L::View v{}; assert(L::read(S::TaskEntry, v) && v.count == 3 && v.last == 2500 && v.min == 2000 && v.seq == 6);
    L::handle(S::HostPreDelete, nullptr); assert(L::dropped == 1 && !L::read(S::HostPreDelete, v)); // null handle: no read
    // interval: ActiveTick rate-limited by writer clock
    next_hwm = 1800; L::self(S::ActiveTick, 0, 1000); next_hwm = 100; L::self(S::ActiveTick, 500, 1000);
    assert(L::read(S::ActiveTick, v) && v.count == 1 && v.min == 1800);
    next_hwm = 1700; L::self(S::ActiveTick, 1000, 1000); assert(L::read(S::ActiveTick, v) && v.count == 2 && v.min == 1700);
    // torn/odd slot is skipped by reader and publisher, published after completion
    L::Table *t = L::table.load(); t->slots[unsigned(S::Quiesce)].seq.store(1);
    assert(!L::read(S::Quiesce, v)); L::publish(sink);
    for (auto &l : lines) { assert(l.what != "quiesce"); } lines.clear();
    t->slots[unsigned(S::Quiesce)].seq.store(0); next_hwm = 900; L::self(S::Quiesce); L::publish(sink);
    assert(lines.size() == 1 && lines[0].what == "quiesce" && lines[0].c == 900 && lines[0].d == bluetooth_lifecycle_stack::BYTES && bluetooth_lifecycle_stack::BYTES == 3072); lines.clear();
    // ActiveTick republished only when min decreases
    next_hwm = 1750; L::self(S::ActiveTick, 2000, 1000); L::publish(sink); assert(lines.empty());
    next_hwm = 1600; L::self(S::ActiveTick, 3000, 1000); L::publish(sink); assert(lines.size() == 1 && lines[0].c == 1600); lines.clear();
    // host stage size + summary split lifecycle/host, summary printed before stage lines
    next_hwm = 1200; L::handle(S::HostPreDelete, &mode); next_hwm = 850; L::self(S::Done); L::publish(sink);
    assert(lines.size() == 3 && lines[0].kind == 2 && lines[0].what == "done" && lines[0].a == 850 && lines[0].b == 1200 && lines[0].c == 0);
    assert(lines[1].what == "host_pre_delete" && lines[1].d == L::HOST_STACK_BYTES && lines[2].what == "done"); lines.clear();
    next_hwm = 840; L::handle(S::MainPreDelete, &mode); L::publish(sink);
    assert(lines.size() == 2 && lines[0].kind == 2 && lines[0].what == "main_pre_delete" && lines[0].a == 840 && lines[0].c == 840); lines.clear();
    L::publish(sink); assert(lines.empty());
    // bounded budget
    for (unsigned i = 0; i < 400; ++i) { next_hwm = 5000 - i; L::self(static_cast<S>(i % 4)); L::publish(sink); }
    unsigned budget = 0; for (auto &l : lines) { budget += l.kind == 3; }
    assert(budget == 1 && t->lines == L::LINE_BUDGET && lines.size() <= L::LINE_BUDGET);
    std::printf("LSP_UNIT ok PASS (min latch, interval, torn skip, summary, budget=%u)\n", unsigned(L::LINE_BUDGET)); return 0;
}
