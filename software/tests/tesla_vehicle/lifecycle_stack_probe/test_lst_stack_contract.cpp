// LST1 lifecycle stack TRIAL contract (host only; NOT hardware evidence, host HWM values are synthetic).
// Wraps the UNCHANGED production module test modes (via the LSP1 probe-ON harness given by LST_LSP_MODULE_CPP) and,
// after the production main() returned, checks the tasks actually requested through the real
// BluetoothLifecycle::start()/run() path:
//  - every "bluetooth" lifecycle task requested exactly LST_EXPECTED_LIFECYCLE_STACK bytes;
//  - the LSP observer reports the same size (lifecycle_stack_probe::LIFECYCLE_STACK_BYTES; no stale 4096);
//  - "nimble_host" keeps CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE;
//  - prints a lifetime signature (task order, name, stack (lifecycle size masked), parked/deleted) that the runner
//    compares byte-for-byte with the same mode built from the LSP1 baseline source.
// A failed contract exits 3 after printing LST_CONTRACT ... result=FAIL (behavioural RED, not a compile error).
#ifndef LST_LSP_MODULE_CPP
#error "LST_LSP_MODULE_CPP must name the probe-ON module harness (test_lsp_module.cpp) of the tree under test"
#endif
#ifndef LST_EXPECTED_LIFECYCLE_STACK
#error "LST_EXPECTED_LIFECYCLE_STACK must be the trial lifecycle stack size in bytes"
#endif
#include LST_LSP_MODULE_CPP
struct LstContract {
    ~LstContract() {
        unsigned bt = 0, host = 0, bt_bad = 0, host_bad = 0;
        std::string sig, requested;
        for (const auto &t : tasks) {
            const bool lifecycle = t->name == "bluetooth";
            sig += t->name + "/" + (lifecycle ? std::string("L") : std::to_string(t->stack_bytes)) + "/" +
                   (t->parked ? "parked" : "unparked") + "/" + (t->deleted ? "deleted" : "kept") + ";";
            if (lifecycle) {
                ++bt; requested += std::to_string(t->stack_bytes) + ",";
                if (t->stack_bytes != LST_EXPECTED_LIFECYCLE_STACK) { ++bt_bad; }
            } else if (t->name == "nimble_host") {
                ++host; if (t->stack_bytes != CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE) { ++host_bad; }
            }
        }
        const bool probe_ok = lifecycle_stack_probe::LIFECYCLE_STACK_BYTES == uint32_t(LST_EXPECTED_LIFECYCLE_STACK) &&
                              lifecycle_stack_probe::HOST_STACK_BYTES == uint32_t(CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE);
        const bool ok = bt > 0 && bt_bad == 0 && host_bad == 0 && probe_ok;
        std::printf("LST_CONTRACT mode=%s bluetooth_tasks=%u requested=%s host_tasks=%u host_bad=%u probe_of=%u "
                    "probe_host_of=%u expected=%u result=%s\n", lsp_mode, bt, requested.empty() ? "-" : requested.c_str(),
                    host, host_bad, unsigned(lifecycle_stack_probe::LIFECYCLE_STACK_BYTES),
                    unsigned(lifecycle_stack_probe::HOST_STACK_BYTES), unsigned(LST_EXPECTED_LIFECYCLE_STACK),
                    ok ? "PASS" : "FAIL");
        std::printf("LST_LIFETIME mode=%s launches=%u sig=%s\n", lsp_mode, task_launches, sig.c_str());
        std::fflush(stdout);
        if (!ok) { std::_Exit(3); }
    }
};
static LstContract lst_contract;
