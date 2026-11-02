// LSP1 module integration: the UNCHANGED production lifecycle/central/module test modes compiled with the probe ON.
// Invariant: every HWM read through a task handle happens before that handle's vTaskDelete (HostTask::deleted).
// test_nimble_module.cpp keeps its own main() (it relies on main's implicit return 0), so the probe hook is installed
// by a static initializer defined after it and the report is printed by the matching static destructor.
#include "../test_nimble_module.cpp"
#include "../../../src/modules/tesla_vehicle/lifecycle_stack_probe.h"
#include <cstdlib>
static std::atomic<unsigned> lsp_reads{0}, lsp_handle_reads{0};
static const char *lsp_mode = "multi";
static unsigned lsp_hwm(const void *t) {
    if (t) { assert(!static_cast<const HostTask *>(t)->deleted && "LSP1 HWM read of a deleted task handle"); ++lsp_handle_reads; }
    return 4000 - (++lsp_reads % 64) * 8;
}
struct LspModuleProbe {
    LspModuleProbe() { lifecycle_stack_probe::host_hwm = lsp_hwm; if (!lifecycle_stack_probe::init()) { std::fprintf(stderr, "LSP1 init failed\n"); std::abort(); } }
    ~LspModuleProbe() {
        std::string seen; for (unsigned i = 0; i < lifecycle_stack_probe::STAGES; ++i) { lifecycle_stack_probe::View v{};
            if (lifecycle_stack_probe::read(static_cast<lifecycle_stack_probe::Stage>(i), v)) { seen += lifecycle_stack_probe::name(static_cast<lifecycle_stack_probe::Stage>(i)); seen += "=" + std::to_string(v.count) + " "; } }
        std::printf("LSP_MODULE mode=%s reads=%u handle_reads=%u sink_lines=%u dropped=%u stages: %s\n", lsp_mode, lsp_reads.load(), lsp_handle_reads.load(), lsp_host_sink_lines, static_cast<unsigned>(lifecycle_stack_probe::dropped.load()), seen.c_str());
        std::fflush(stdout);
    }
};
static LspModuleProbe lsp_module_probe;
// Mode name for the report only; read from /proc/self/cmdline so main() stays the untouched production test main.
static const bool lsp_mode_read = [] { static char buf[256]; FILE *f = std::fopen("/proc/self/cmdline", "rb"); if (!f) return false;
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f); std::fclose(f); buf[n] = 0; const size_t a = std::strlen(buf);
    if (a + 1 < n) lsp_mode = buf + a + 1; return true; }();
