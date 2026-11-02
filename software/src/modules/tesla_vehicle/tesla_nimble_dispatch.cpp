/* Production Tesla-owned NimBLE call dispatcher binding (moved from the T1r13/T1r14 lab fixture
 * tesla_mem_fixture_radio_deny.cpp; the lab target gate, presence injection, scan tokens and --wrap linker
 * interception are NOT ported). Core state machine: tesla_ble_call_dispatch.h (byte-equal to the host-tested T1r13 core).
 *
 * Why: the Tesla worker runs on a PSRAM task stack (TESLA_WORKER_PSRAM_STACK). NimBLE HCI command/ACL TX executes
 * esp_vhci_host_send_packet() (closed BT controller, partly ROM) in the CALLER's context, so the five NimBLE submit
 * APIs used by the worker are executed by a dedicated helper task whose stack placement is read back and must be
 * internal before any call is admitted. Calls made on an internal stack (host task, lifecycle task) run directly.
 * Safety properties retained from T1r13 (OFFLINE_MEMORY_REVIEW A1-A4/B1/B2):
 *  - timeout = outcome unknown: helper keeps the job, dispatcher latches restart_required, refuses further bounced
 *    calls, refuses direct connects and refuses nimble_port_stop/deinit for the rest of the boot (teardown then fails
 *    closed, lifecycle context/cookie stay alive for late callbacks);
 *  - late completion owned by the helper; accepted late CONNECT retired from the internal stack;
 *  - every copied byte is wiped before Completed/Quarantined; helper never deleted; no FreeRTOS mutex.
 * Quarantine consumer (production): TeslaVehicle::loop() reads restart_required() and latches the runtime owner
 *    (restart_required state, terminal error, every admission refused, no retry/scan/pair/poll/connect/reboot).
 *    Direct (internal-stack) NEW submits - connect, GATT write/discovery, scan start - are refused once quarantined;
 *    terminate/cancel stay available so retirement/quiesce can still drain, host stop/deinit remain refused.
 * Additional fail-closed backstop (new, production): scan and GATT-discovery submits that are not routed through the
 * helper are refused when called from an external-RAM stack instead of reaching the controller from PSRAM.
 * Only tesla_nimble_central.cpp calls these functions (explicit source-level routing, no linker wraps).
 */
#include "tesla_nimble_dispatch.h"

#if defined(TESLA_NIMBLE_DISPATCH_BOUND)
#include <atomic>
#include <cstddef>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_cpu.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <esp_memory_utils.h>
#include <nimble/nimble_port.h>
#include "tesla_ble_call_dispatch.h"
#include "tesla_ble_operation.h"

static_assert(tesla_ble_dispatch::WORKER_WATCHDOG_MS == TeslaBleOperation::TIMEOUT_MS,
              "dispatcher margin is computed against the unchanged worker watchdog");

namespace {
constexpr uint32_t BOUNCE_STACK = 3072;

struct RtosOs {
    struct Sem { StaticSemaphore_t buf; SemaphoreHandle_t h; };
    using Task = TaskHandle_t;
    static constexpr uint32_t FOREVER = UINT32_MAX;
    static bool sem_init(Sem &s) { s.h = xSemaphoreCreateBinaryStatic(&s.buf); return s.h != nullptr; }
    static void give(Sem &s) { xSemaphoreGive(s.h); }
    static bool take(Sem &s, uint32_t ms) { return xSemaphoreTake(s.h, ms == FOREVER ? portMAX_DELAY : pdMS_TO_TICKS(ms) + 1) == pdTRUE; }
    static bool create_helper(void (*entry)(void *), void *arg, Task &t)
    {
        // Plain create: IDF pvPortMallocStack -> MALLOC_CAP_INTERNAL|8BIT. Placement is still read back (stack_internal).
        Task created = nullptr;
        if (xTaskCreatePinnedToCore(entry, "tesla_nb_bounce", BOUNCE_STACK, arg, configMAX_PRIORITIES - 5, &created,
                                    CONFIG_BT_NIMBLE_PINNED_TO_CORE) != pdPASS || !created) { return false; }
        t = created;
        return true;
    }
    static bool range_internal(const void *p, size_t n)
    {
        const auto *b = static_cast<const uint8_t *>(p);
        return p && n && esp_ptr_internal(b) && esp_ptr_internal(b + n - 1) && !esp_ptr_external_ram(b) && !esp_ptr_external_ram(b + n - 1);
    }
    static bool stack_internal(Task t) { return t && range_internal(pxTaskGetStackStart(t), BOUNCE_STACK); }
    static bool object_internal(const void *p, size_t n) { return range_internal(p, n); }
    static uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
    static void sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1); }
    static constexpr bool helper_exit_requested() { return false; } // the helper is never deleted
    static void before_step() {}
};

struct RealNimble {
    using Addr = ble_addr_t;
    using ConnParams = struct ble_gap_conn_params;
    using GapCb = ble_gap_event_fn;
    using AttrCb = ble_gatt_attr_fn;
    using SvcCb = ble_gatt_disc_svc_fn;
    using Uuid = ble_uuid_t;
    using UuidAny = ble_uuid_any_t;
    static constexpr int EINVAL_RC = BLE_HS_EINVAL;
    static constexpr uint8_t TERM_REASON = BLE_ERR_REM_USER_CONN_TERM;
    static size_t uuid_size(const ble_uuid_t *u)
    {
        if (!u) { return 0; }
        switch (u->type) {
        case BLE_UUID_TYPE_16: return sizeof(ble_uuid16_t);
        case BLE_UUID_TYPE_32: return sizeof(ble_uuid32_t);
        case BLE_UUID_TYPE_128: return sizeof(ble_uuid128_t);
        }
        return 0;
    }
    static int connect(uint8_t o, const ble_addr_t *p, int32_t d, const struct ble_gap_conn_params *c, ble_gap_event_fn *cb, void *a) { return ::ble_gap_connect(o, p, d, c, cb, a); }
    static int terminate(uint16_t c, uint8_t r) { return ::ble_gap_terminate(c, r); }
    static int conn_cancel() { return ::ble_gap_conn_cancel(); }
    static int write_flat(uint16_t c, uint16_t h, const void *d, uint16_t n, ble_gatt_attr_fn *cb, void *a) { return ::ble_gattc_write_flat(c, h, d, n, cb, a); }
    static int disc_svc(uint16_t c, const ble_uuid_t *u, ble_gatt_disc_svc_fn *cb, void *a) { return ::ble_gattc_disc_svc_by_uuid(c, u, cb, a); }
    static bool find_master_by_addr(const ble_addr_t &a, uint16_t &h)
    {
        struct ble_gap_conn_desc d{};
        if (ble_gap_conn_find_by_addr(&a, &d) != 0 || d.role != BLE_GAP_ROLE_MASTER) { return false; }
        h = d.conn_handle;
        return true;
    }
};

using Exec = tesla_ble_dispatch::NimbleExec<RealNimble>;
using Dispatcher = tesla_ble_dispatch::Dispatcher<RtosOs, Exec>;
Dispatcher bounce; // internal DRAM .bss (job placement read back at start; admission refused otherwise)
std::atomic<uint32_t> direct_calls{0}, quarantined_direct_connect{0}, quarantined_direct_submit{0}, host_stop_refused{0}, host_deinit_refused{0},
    external_stack_refused{0};

// New controller work after an unknown outcome is refused on every stack (fail closed, outcome stays unknown).
bool refuse_quarantined() { if (!bounce.restart_required()) { return false; } quarantined_direct_submit.fetch_add(1); return true; }

bool on_external_stack() { return esp_ptr_external_ram(reinterpret_cast<const void *>(esp_cpu_get_sp())); }

int to_rc(tesla_ble_dispatch::Result r)
{
    using tesla_ble_dispatch::Outcome;
    switch (r.outcome) {
    case Outcome::Executed: return r.rc;
    case Outcome::Timeout: return BLE_HS_ETIMEOUT;   // outcome UNKNOWN; dispatcher is quarantined (restart required)
    case Outcome::Busy: return BLE_HS_EBUSY;
    case Outcome::Rejected: return BLE_HS_EMSGSIZE;
    case Outcome::Unavailable: case Outcome::Quarantined: break;
    }
    return BLE_HS_ENOTSYNCED;
}
} // namespace

namespace tesla_nimble_dispatch {
bool start()
{
    const auto r = bounce.start();
    return r == tesla_ble_dispatch::StartResult::Ready || r == tesla_ble_dispatch::StartResult::AlreadyReady;
}
bool restart_required() { return bounce.restart_required(); }
uint32_t refused_external_stack_calls() { return external_stack_refused.load(); }

int gap_connect(uint8_t own_addr_type, const ble_addr_t *peer, int32_t duration_ms,
                const struct ble_gap_conn_params *params, ble_gap_event_fn *cb, void *cb_arg)
{
    if (!on_external_stack()) {
        if (bounce.restart_required()) { quarantined_direct_connect.fetch_add(1); return BLE_HS_ENOTSYNCED; }
        direct_calls.fetch_add(1);
        return ::ble_gap_connect(own_addr_type, peer, duration_ms, params, cb, cb_arg);
    }
    return to_rc(bounce.run([&](Exec::Job &j) { return Exec::fill_connect(j, own_addr_type, peer, duration_ms, params, cb, cb_arg); }));
}
int gap_terminate(uint16_t conn, uint8_t reason)
{
    if (!on_external_stack()) { direct_calls.fetch_add(1); return ::ble_gap_terminate(conn, reason); }
    return to_rc(bounce.run([&](Exec::Job &j) { return Exec::fill_terminate(j, conn, reason); }));
}
int gap_conn_cancel()
{
    if (!on_external_stack()) { direct_calls.fetch_add(1); return ::ble_gap_conn_cancel(); }
    return to_rc(bounce.run([&](Exec::Job &j) { return Exec::fill_conn_cancel(j); }));
}
int gattc_write_flat(uint16_t conn, uint16_t handle, const void *data, uint16_t len, ble_gatt_attr_fn *cb, void *cb_arg)
{
    if (!on_external_stack()) {
        if (refuse_quarantined()) { return BLE_HS_ENOTSYNCED; }
        direct_calls.fetch_add(1);
        return ::ble_gattc_write_flat(conn, handle, data, len, cb, cb_arg);
    }
    return to_rc(bounce.run([&](Exec::Job &j) { return Exec::fill_write(j, conn, handle, data, len, cb, cb_arg); }));
}
int gattc_disc_svc_by_uuid(uint16_t conn, const ble_uuid_t *uuid, ble_gatt_disc_svc_fn *cb, void *cb_arg)
{
    if (!on_external_stack()) {
        if (refuse_quarantined()) { return BLE_HS_ENOTSYNCED; }
        direct_calls.fetch_add(1);
        return ::ble_gattc_disc_svc_by_uuid(conn, uuid, cb, cb_arg);
    }
    return to_rc(bounce.run([&](Exec::Job &j) { return Exec::fill_disc_svc(j, conn, uuid, cb, cb_arg); }));
}
// Backstop: these submits are issued from the lifecycle task / NimBLE host callbacks (internal stacks). If any
// future path calls them from an external-RAM stack, refuse instead of entering the controller from PSRAM.
int gap_disc(uint8_t own_addr_type, int32_t duration_ms, const struct ble_gap_disc_params *params, ble_gap_event_fn *cb, void *cb_arg)
{
    if (on_external_stack()) { external_stack_refused.fetch_add(1); return BLE_HS_EREJECT; }
    if (refuse_quarantined()) { return BLE_HS_ENOTSYNCED; }
    return ::ble_gap_disc(own_addr_type, duration_ms, params, cb, cb_arg);
}
int gap_disc_cancel()
{
    if (on_external_stack()) { external_stack_refused.fetch_add(1); return BLE_HS_EREJECT; }
    return ::ble_gap_disc_cancel();
}
int gattc_disc_all_chrs(uint16_t conn, uint16_t start, uint16_t end, ble_gatt_chr_fn *cb, void *cb_arg)
{
    if (on_external_stack()) { external_stack_refused.fetch_add(1); return BLE_HS_EREJECT; }
    if (refuse_quarantined()) { return BLE_HS_ENOTSYNCED; }
    return ::ble_gattc_disc_all_chrs(conn, start, end, cb, cb_arg);
}
int gattc_disc_all_dscs(uint16_t conn, uint16_t start, uint16_t end, ble_gatt_dsc_fn *cb, void *cb_arg)
{
    if (on_external_stack()) { external_stack_refused.fetch_add(1); return BLE_HS_EREJECT; }
    if (refuse_quarantined()) { return BLE_HS_ENOTSYNCED; }
    return ::ble_gattc_disc_all_dscs(conn, start, end, cb, cb_arg);
}
// Host teardown interlock: no stop/deinit while a bounced call is in flight, abandoned or quarantined.
int port_stop()
{
    if (!bounce.host_begin()) { host_stop_refused.fetch_add(1); return BLE_HS_EBUSY; }
    const int rc = ::nimble_port_stop();
    bounce.host_end();
    return rc;
}
esp_err_t port_deinit()
{
    if (!bounce.host_begin()) { host_deinit_refused.fetch_add(1); return ESP_ERR_INVALID_STATE; }
    const esp_err_t rc = ::nimble_port_deinit();
    bounce.host_end();
    return rc;
}
} // namespace tesla_nimble_dispatch
#endif
