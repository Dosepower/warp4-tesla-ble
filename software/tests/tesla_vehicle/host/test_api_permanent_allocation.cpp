// Native regression for production Arena, make_permanent and API::register_events.
// Registration types and register_events are extracted verbatim by the runner.
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <vector>
#include "malloc.h"

class String {};
enum class Language { Default };
class ConfigRoot {};
class IChunkedResponse {};
class Ownership {};
#include "production_registrations.inc"

struct TestLogger {
    unsigned calls = 0;
    void printfln(const char *, ...) { ++calls; }
} logger;

struct API {
    struct RegistrationCollector {
        std::vector<StateRegistration> states;
        std::vector<CommandRegistration> commands;
        std::vector<ResponseRegistration> responses;
    };
    RegistrationCollector *reg_collector = new RegistrationCollector;
    std::span<StateRegistration> states;
    std::span<CommandRegistration> commands;
    std::span<ResponseRegistration> responses;
    void register_events();
    ~API() { delete reg_collector; }
};
#include "production_register_events.inc"

static RAM fail_ram = _NONE;
static size_t fail_bytes = 0;
static unsigned allocation_calls = 0;
static std::vector<void *> allocations;
static std::vector<RAM> selected_rams;
static std::vector<size_t> requested_bytes;
static Arena *test_arena = nullptr;

void *perm_aligned_alloc(size_t alignment, size_t size, RAM ram)
{
    ++allocation_calls;
    selected_rams.push_back(ram);
    requested_bytes.push_back(size);
    if (test_arena != nullptr) {
        assert(ram == IRAM);
        return test_arena->aligned_alloc(alignment, size);
    }
    if (ram == fail_ram && (!fail_bytes || size == fail_bytes))
        return nullptr;
    void *result = nullptr;
    assert(posix_memalign(&result, std::max(alignment, sizeof(void *)), size) == 0);
    allocations.push_back(result);
    return result;
}

static void cleanup()
{
    for (void *ptr : allocations)
        std::free(ptr);
    allocations.clear();
}

struct Probe {
    static inline unsigned moves = 0;
    unsigned value;
    explicit Probe(unsigned v) : value(v) {}
    Probe(Probe &&other) noexcept : value(other.value) { ++moves; other.value = 0; }
};

static void template_failure()
{
    fail_ram = IRAM;
    std::vector<Probe> source;
    source.reserve(200);
    source.emplace_back(42);
    auto *original = source.data();
    auto capacity = source.capacity();
    auto result = make_permanent(std::move(source), IRAM);
    assert(result.empty());
    assert(source.data() == original && source.capacity() == capacity);
    assert(source.size() == 1 && source[0].value == 42 && Probe::moves == 0);
    assert(selected_rams.back() == IRAM);
    assert(requested_bytes.back() == sizeof(Probe));
    fail_ram = _NONE;
    result = make_permanent(std::move(source), IRAM);
    assert(result.size() == 1 && result[0].value == 42 && source.empty());
    // Successful source buffers must not stay allocated when the API has to
    // retain the collector for a different failed table (-fno-exceptions).
    assert(source.capacity() == 0);
    result[0].~Probe();
    cleanup();
}

static void empty_source()
{
    std::vector<Probe> source;
    const auto before = allocation_calls;
    auto result = make_permanent(std::move(source), IRAM);
    assert(result.empty() && allocation_calls == before);
}

static void api_case(const std::string &mode)
{
    unsigned callback_calls = 0;
    {
        API api;
        auto *collector = api.reg_collector;
        collector->states.reserve(300);
        collector->commands.reserve(200);
        collector->responses.reserve(4);
        collector->states.push_back({"state", nullptr, nullptr, nullptr, 0x05000000});
        for (unsigned i = 0; i < 164; ++i)
            collector->commands.push_back({"command", nullptr, nullptr,
                [&callback_calls](Language, String &) { ++callback_calls; }, 0x07000000});
        collector->responses.push_back({"response", nullptr, nullptr,
            [&callback_calls](Language, IChunkedResponse *, Ownership *, uint32_t) { ++callback_calls; }, 8, 0});
        auto *old_states = collector->states.data();
        auto *old_commands = collector->commands.data();
        auto *old_responses = collector->responses.data();
        const auto command_capacity = collector->commands.capacity();
        if (mode == "states") {
            fail_ram = IRAM;
            fail_bytes = sizeof(StateRegistration);
        } else if (mode == "commands") {
            fail_ram = IRAM;
            fail_bytes = 164 * sizeof(CommandRegistration);
        } else if (mode == "responses") {
            fail_ram = DRAM;
            fail_bytes = sizeof(ResponseRegistration);
        }
        api.register_events();
        assert(allocation_calls == 3);
        assert((selected_rams == std::vector<RAM>{IRAM, IRAM, DRAM}));
        assert(requested_bytes[1] == 164 * sizeof(CommandRegistration));
        assert(api.states.size() == 1 && api.commands.size() == 164 && api.responses.size() == 1);
        if (mode == "success") {
            assert(api.reg_collector == nullptr);
            assert(logger.calls == 0);
        } else {
            assert(api.reg_collector == collector);
            assert(logger.calls == 1);
            if (mode == "states") {
                assert(api.states.data() == old_states && collector->states.size() == 1);
                assert(collector->commands.empty() && collector->responses.empty());
                assert(collector->commands.capacity() == 0 && collector->responses.capacity() == 0);
            } else if (mode == "commands") {
                assert(api.commands.data() == old_commands && collector->commands.size() == 164);
                assert(collector->commands.capacity() == command_capacity);
                assert(collector->states.empty() && collector->responses.empty());
                assert(collector->states.capacity() == 0 && collector->responses.capacity() == 0);
            } else {
                assert(api.responses.data() == old_responses && collector->responses.size() == 1);
                assert(collector->states.empty() && collector->commands.empty());
                assert(collector->states.capacity() == 0 && collector->commands.capacity() == 0);
            }
        }
        String error;
        for (auto &command : api.commands) {
            assert(std::strcmp(command.path, "command") == 0);
            command.callback(Language::Default, error);
        }
        api.responses[0].callback(Language::Default, nullptr, nullptr, 0);
        assert(callback_calls == 165);
        // Spans don't own permanent registrations; destroy them for host leak checks.
        if (api.commands.data() != old_commands)
            for (auto &command : api.commands) command.~CommandRegistration();
        if (api.responses.data() != old_responses)
            api.responses[0].~ResponseRegistration();
    }
    cleanup();
}

static size_t backend_size = 0;
static unsigned backend_calls = 0;
static void *exhausted_backend(size_t, size_t size)
{
    ++backend_calls;
    backend_size = size;
    return nullptr;
}
struct TargetCommand {
    uint32_t words[8];
};
static_assert(sizeof(TargetCommand) == 32);

static void target_request()
{
    // Match target payload capacity despite the host's 32-byte BlockHeader.
    alignas(Arena::MAX_ALIGNMENT) unsigned char reserved[8176 + sizeof(Arena::BlockHeader)]{};
    Arena arena{exhausted_backend, 4, 8192, 1024};
    arena.setup({{reserved, sizeof(reserved)}});
    assert(arena.head->capacity == 8176);
    assert(backend_calls == 0);
    // Deterministically create insufficient arena space; this is NOT a claim
    // about the unknown state-count/arena-used values in the hardware trace.
    assert(arena.aligned_alloc(4, 6000) != nullptr);
    test_arena = &arena;
    std::vector<TargetCommand> source(164);
    source.reserve(200);
    fail_ram = IRAM;
    fail_bytes = 5248;
    // Exercise the real Arena's large-request path and its null result.
    auto *result = arena.aligned_alloc(4, source.size() * sizeof(TargetCommand));
    assert(result == nullptr && backend_size == 0x1480 && backend_calls == 1);
    // Exercise the unchanged real make_permanent with the same null result.
    auto span = make_permanent(std::move(source), IRAM);
    assert(span.empty() && source.size() == 164);
    assert(requested_bytes.back() == 5248 && selected_rams.back() == IRAM);
    assert(backend_size == 5248 && backend_calls == 2);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const std::string mode = argv[1];
    if (mode == "template") template_failure();
    else if (mode == "empty") empty_source();
    else if (mode == "target") target_request();
    else api_case(mode);
    std::printf("PASS %s: production allocator/template/API path\n", argv[1]);
}
