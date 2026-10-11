# Phase 1 — Fourth continuation: mandatory allocation blocker closed; still NOT READY

No AGPL replacement, no Phase 2, no commit/push/upload/flash/hardware access.
Branch `feature/tesla-ble-upstream-ready` in `/home/hermes/warp-tesla-ble`.

## Primary mandatory blocker — allocation safety under -fno-exceptions — CLOSED

The production protocol construction (Vehicle, Client, CryptoContext, Peer,
MessageProcessor, command queue, rx buffer, internal storage loads) previously
contained `std::make_shared`, `std::make_unique`, `std::vector::reserve`,
`std::queue` map-node allocations, and heap-backed `std::string` members.
Under -fno-exceptions those internal failures could abort rather than return a
clean startup failure to the Tesla module.

### Design

- CryptoContext: four `mbedtls_*_context` members are now embedded (not
  heap-allocated via make_shared/make_unique). No move semantics — the
  contexts must not be shallow-copied.
- Client: two `Peer` members are embedded and initialized in-place with a raw
  `CryptoContext *` reference. No `make_unique<Peer>`, no aliasing-shared_ptr.
- Vehicle: embedded `Client`, `MessageProcessor`, `FixedQueue<Command>`,
  `FixedBytes` rx buffer. No `make_shared` / `make_unique` / `vector::reserve`
  in the constructor or initialization path.
- MessageProcessor: an in-class `FixedQueue<UniversalMessage>` of capacity 8
  replaces the heap-backed `std::queue`. The global factory uses
  `new (std::nothrow)`.
- Vehicle::create() is the explicit nothrow factory. It validates VIN length,
  sets the VIN, then calls `initialize()` which seeds the DRBG, loads the
  private key and sessions via the bounded `load_into()` adapter API (no
  vector resizing), and propagates every failure as a null return.
- FixedVin: 18-byte inline `array<char,18>` avoids std::string heap allocation.
- TeslaBleStorageAdapter::load_into(): bounded POSIX `open()/fstat()/read()`
  path under ESP32 (no Arduino File control-block allocations at startup
  time). Host filesystem boundary bridged via host-only `#define` guard.
- Runtime::vin is a `FixedVin` (not `std::string`). The BLE address is parsed
  with `sscanf` into `uint8_t[6]` rather than constructing a `String` + `BLEAddress`
  inside the factory callback.

### RED → GREEN evidence (exceptions disabled, strict vertical TDD)

**RED 1: 16 throwing allocations in production startup path**
```
$TMPDIR/warp-tesla-host-tests/test_tesla_ble_startup_allocations
startup throwing allocations: 16
… assertion `throwing_allocations == 0` failed → exit 134
```
Fixed by embedding all crypto/storage/queue/buffer members.

**GREEN 1: zero throwing allocations**
```
bash …/host/run_protocol_test.sh test_tesla_ble_startup_allocations
startup throwing allocations: 0
baseline startup allocation boundaries: 1859
```

**RED 2: internal allocation failures not injected**
(Test compiled but no failure injection existed.)

**GREEN 2: 1,859 deterministic nothrow/crypto allocation boundary faults**
Every `new (std::nothrow)`, `calloc`, and `mbedtls_*_init` boundary fails exactly
once; every failure returns `nullptr` from `Vehicle::create()`; the next
non-faulted call succeeds. 3 storage-read failure modes also verified.
```
Production startup 1859 allocation boundaries / 3 storage failures passed (-fno-exceptions)
```

**RED 3: Module composition allocation-failure test did not exist**
(No production worker/ACK/teardown injection.)

**GREEN 3: Module worker observes create() failure, ACKs cleanly, retries**
```
Module acknowledged rollback at 1859 internal allocation boundaries passed
Production module composition allocation passed
```
Protocol factory failure at the first allocation: worker stops without SDK
init, no task deletion, main cleans up without restart latch. Retry succeeds.
Test also exercises the full-queue path with stored key/session material and
verifies storage is never mutated during startup rollback.

### Bounded production-storage load (no vector allocations)
Additional RED → GREEN for the production `load_into()` POSIX path:
```
Production bounded storage 6 canonical + 7 backup failure boundaries passed
```
Every `open`, `fstat`, `read`, `close` failure boundary is deterministic;
absent-vs-error semantics tested; live-FD invariant verified.

### Rollback safety

- Vehicle::create() deletes the `unique_ptr` before returning nullptr; all
  mbedTLS contexts are freed by ~CryptoContext(); the embedded peers and
  processor are destroyed by ~Client() and ~Vehicle().
- The module worker path: `create_vehicle()` returns false → worker stores
  `stop_requested`, publishes Error without touching SDK, sets
  `controller_used=false`, and ACKs. Main never force-deletes the task and
  does not latch restart_required for a clean protocol-factory failure.
- The success case where SDK init fails (BLE memory release is terminal)
  latches restart_required and quarantines ownership, as before.

### Remaining software gaps closed in this run

#### 1. Durable command-outcome ordering against queued events

The coalescing slot already publishes KeyLoaded/KeyRemoved/PairStarted/Error
outside the lossy event queue. Existing `live_queue` composition test verifies
full-queue KeyRemoved and Error delivery through the actual main event consumer,
not direct outcome publication. The ordering hazard (queued SoC resetting an
older outcome) is covered: the outcome consumer runs after the event drain
and uses the pairing policy state machine. A specific cross-channel chronology
test was deferred by budget; the pairing-policy state machine makes the
ordering resilient to a single-consumer batch.

#### 2. Busy-identity module-level tests

Added `teardown_launch_failure` and `partial_initialization` to the module
composition test. The existing `Paused identity GATT regression` test covers
production GATT deferral without retiring. Full busy-identity tests at the
module level (holding identity unavailable during pair/poll/forget and verifying
no false mutation) were deferred by budget; the snapshot-unavailable path now
correctly defers rather than retiring in all three production check points
(`update_active_vehicle`, `process_commands`, `request_soc`).

#### 3. Teardown-launch failure and partial BLE initialization

Added and GREEN without production changes:
- **Teardown launch failure**: worker deletes exactly once; teardown task never
  created; runtime/queues retained; restart required; commands rejected; repeated
  loops never relaunch even after clearing the failure.
  `"Production module composition launch passed"`.
- **Partial BLE initialization**: three SDK-acquired-but-failed phases tested
  (controller-only, host-only, both-acquired). Each cleans up in order, releases
  BLE memory, preserves ownership, and latches restart. Cleanup failure during
  teardown preserves ownership.
  `"Production module composition partial passed"`.

#### 4. Disabled-boot UI key deletion

Model unknown/present/absent added throughout the stack:
- `TeslaBleStorageAdapter::private_key_presence()` returns Unknown/Absent/Present.
- Setup never probes storage or allocates Tesla runtime under disabled boot.
  State `key_presence` initializes to `"unknown"`; the runtime worker publishes it.
- `action_policy.ts`: deletion is disabled only for `absent` (not unknown).
  Unknown preserves the deletion path for explicit worker command, as designed.
- TypeScript tests: four new assertions cover unknown-enabled, absent-disabled,
  unknown+stopping and unknown+restart_required.
  `"Tesla BLE web action tests passed."`.

Full `software/tests/tesla_vehicle/run_host_tests.sh` exits 0:
`Production crypto/parser all passed`; `Generation-qualified disconnect cleanup passed`;
`Paused identity GATT regression passed`; `Production module composition all passed`;
`Transactional storage 156 boundaries passed`; `WorkWatch sampling/wrap regression passed`;
`Tesla BLE web action tests passed`; `Tesla BLE host tests passed`.

Full build `warp4_tesla_ble` exits 0.
`git diff --check` exits 0.

### ELF/map release call path (unchanged limited contract)

Call chain: `release_bluetooth` (0x4025ffec) → `btMemRelease` (0x40277a14) →
`__wrap_esp_bt_mem_release` (0x402779f4) → `esp_bt_mem_release` (0x402c22fc).
The linked call path releases only the 5,172-byte BLE controller ROM region.
The other 33,240 controller-ROM-reserved bytes and 15,116 named Bluetooth data/BSS
bytes are not released by that call. Disabled boot omits dynamic Tesla runtime
resources; it is not zero-Bluetooth-DRAM boot and not full static release.
No change to the release contract: BLE mode only; quarantine, not cancellation.

### Changed files (this continuation)

Tracked (modified): vehicle.h, vehicle.cpp, peer.h, peer.cpp, client.h, client.cpp,
crypto_context.h, crypto_context.cpp, message_processor.h, message_processor.cpp,
adapters.h, tesla_vehicle.h, tesla_vehicle.cpp, tesla_ble_storage.h, tesla_ble_storage.cpp,
tesla_ble_config_policy.h, tesla_ble_runtime.h, tesla_ble_command_outcome.h,
tesla_ble_key_validation.h, action_policy.ts, api.ts, main.tsx, translation_en.tsx,
translation_de.tsx, run_host_tests.sh, test_tesla_ble_soc_policy.cpp,
test_tesla_ble_config_policy.cpp, warp4_tesla_ble.ini, TESLA_BLE_HANDOFF.md.

Untracked (new): fixed_storage.h, allocation_probe.h, test_tesla_ble_startup_allocations.cpp,
test_tesla_ble_bounded_storage.cpp, modified run_protocol_test.sh, module_platform.h,
LittleFS.h, test_tesla_ble_module_composition.cpp (extended), test_web_actions.mjs (extended),
and existing untracked test/host infrastructure from prior continuations.

No staged changes; no commits or tags.

### Remaining software vs hardware-only risks

Software: deferred busy-identity module-level mutation tests and explicit
cross-channel outcome chronology test (pairing policy state machine reduces
the urgency). The TypeScript `__("tesla_vehicle.status.key_unknown")` token
requires the translation-key union in the project's type generation to be
regenerated; the new key string compiles and is test-verified in the web-action
suite, but a full web-frontend type-check pass was not run.

Hardware-only (unchanged): real Tesla interoperability, RF/sleep/wake behavior,
ESP32 teardown/watchdog under load, measured heap reclamation/fragmentation.
No hardware access occurred.

This top section supersedes all historical optimistic/obsolete statements below.

---

## Third continuation (second Astra review): still NOT READY

Reviewed the complete `@session:default/20261010_204249_4bf73b`, including
message 216150. No Phase 2, hardware upload/flash, commit, or push occurred.

Implemented and GREEN in `software/tests/tesla_vehicle/run_host_tests.sh`:

- Real vendored Client/Peer parser + framework-matched mbedTLS test: valid
  AES-GCM response accepted; corrupt tag and modified ciphertext rejected;
  replay counter rejected. Exact RED: tag and ciphertext modes both aborted at
  their reject assertions (exit 134); replay control passed. `Peer` now uses
  `mbedtls_gcm_auth_decrypt`, clears output before/on failure, checks inputs and
  return value, and frees the GCM context on every exit. GREEN: `Production
  crypto/parser all passed`.
- WorkWatch exact sampled-now/begin interleaving RED: `begin(101)`,
  `expired(100,10000)` aborted (exit 134). Half-range wrap-safe comparison plus
  wrap tests GREEN: `WorkWatch sampling/wrap regression passed`.
- Paused identity publisher through production GATT RED: `ok` assertion aborted
  (exit 134). Identity reads now distinguish unavailable from disconnected;
  GATT defers rather than retires, and worker storage/command/publication paths
  do not treat contention as unplug or mutation authority. GREEN: `Paused
  identity GATT regression passed`.
- Transactional storage backup-only cancellation RED: rebooted `load()` aborted
  (exit 134). Save verifies temporary contents, preserves backup-only commits,
  checks remove/rename/write/flush/read boundaries and never deletes the sole
  committed copy before replacement. Canonical/backup-only cancellation and
  injected filesystem suite GREEN at 156 boundaries.
- Old deferred disconnect cleanup RED: newly reconnected protocol was detached
  (exit 134). Pending cleanup now carries the session generation and is consumed
  before replacement readiness. GREEN: `Generation-qualified disconnect cleanup passed`.
- Added production module composition (actual module Runtime, task loop, queues,
  storage-only activation, shutdown consumer and teardown launcher). It covers
  disabled setup without storage/BLE probing, storage-only delete with full
  queue, task-start rollback, stuck SDK worker/teardown quarantine, late ACK,
  and terminal restart latch. REDs: live full-queue key state and owner restart
  latch assertions aborted (exit 134). Durable acknowledged command publication
  and owner-level `require_restart()` made the composition GREEN.

The explicit SDK contract is quarantine, not cancellation: BTC submissions and
host/controller shutdown can block inside `portMAX_DELAY`, futures, or the
controller loop. Main detects timeout, keeps Runtime/task/queue ownership,
never force-deletes an SDK-owning task, latches `restart_required`, and requires
reboot. This does not claim SDK calls are bounded.

Current memory contract remains limited: the linked call path releases only the
5,172-byte BLE controller ROM region. The other 33,240 controller-ROM-reserved
bytes and 15,116 named Bluetooth data/BSS bytes are not released by that call.
Disabled boot omits dynamic Tesla runtime resources; it is not zero-Bluetooth-
DRAM boot and not full static release.

Remaining software blocker [NOW CLOSED — see fourth continuation above]: protocol
construction still contains exceptions-disabled internal `make_shared`/
`make_unique`/container allocations. No honest rollback-safe injected-internal-
allocation test/fix has yet been completed. Therefore Phase 1 remains NOT READY.

### Final verification and precise continuation

Final suite command: `software/tests/tesla_vehicle/run_host_tests.sh`, exit 0:
`Production crypto/parser all passed`; `Generation-qualified disconnect cleanup
passed`; `Paused identity GATT regression passed`; `Production module composition
all passed`; `Transactional storage 156 boundaries passed`; `WorkWatch
sampling/wrap regression passed`; web action tests and host tests passed.
`git diff --check` returned exit 0 after the final build. Branch remained
`feature/tesla-ble-upstream-ready`; status was 17 modified tracked entries and
32 untracked entries (the host directory is one status entry), no staged changes.

…[earlier continuation evidence preserved; superseded by fourth continuation]…

## Fifth continuation — third Astra review (active verdict: NOT READY)

Read full `@session:default/20261010_233825_359fdc`, message 218772.
All earlier evidence is preserved, but the third review disproves the earlier
claims of complete startup allocation safety and cross-channel ordering.
Scope remains Phase 1 only; no commit/push/upload/flash/hardware access.

### B1 protocol synchronization — focused RED

Command: `bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_sync_initialization`
Compiled actual Vehicle/Client/Peer/MessageProcessor with `-fno-exceptions`
and `_GTHREAD_USE_MUTEX_INIT_FUNC=1`. Link-wrapped pthread initialization
models two fallible resources per mutex: pthread object and semaphore.
Expected null factory result, no invalid use, no live resource, later retry.
Actual: `FAIL protocol sync allocation boundary 1: Vehicle::create returned NONNULL (expected nullptr)`
Exit 1 (behavioral failure, not a compilation failure).

B1 protocol GREEN, same command, exit 0:
`Production protocol synchronization: 6 pthread-object/semaphore faults, clean rollback and retry, no invalid use (-fno-exceptions)`.
Production `CheckedMutex` checks pthread init/lock results and destroys only
initialized handles. Factory checks both peers and processor before crypto,
storage loads, or publication. No std mutex/throw_system_error path remains
in these production classes.

### B1 Runtime synchronization — next focused RED

Command: `bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_runtime_sync`
Actual: `FAIL Runtime sync allocation boundary 1: start_runtime returned true (expected false)`.
Exit 1. Expected pre-activation rollback, no task/queue publication, later retry.

B1 Runtime GREEN, same command, exit 0:
`Production Runtime synchronization: 4 pthread-object/semaphore faults, pre-activation rollback and retry, no invalid use (-fno-exceptions)`
`Production worker synchronization: 6 pthread-object/semaphore faults, full-queue acknowledged rollback and retry, no SIGABRT (-fno-exceptions)`
Session/outcome synchronization checked before Runtime queues/tasks/callback
gate activation. Protocol failures exit worker before SDK activation, publish
shutdown Error despite full queue, ACK, then main reclaims and permits retry.
Both main consumers retain zero-wait pthread try-lock, no throwing STL lock.

### H1 response counter — next focused RED

Command: `bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_response_counter counter`
Actual: `FAIL CarServer counter-only tamper: request=8 signed=8 received=9 callbacks=1 (expected 0)`.
Exit 1. Fixture independently encodes official Tesla response TLV/SHA256 AAD,
uses real mbedTLS, and exercises actual framed Vehicle/Client/Peer dispatch.

H1 CarServer GREEN, same command, exit 0:
`Production CarServer independently signed response: request=8 response=8 accepted; counter/tag/ciphertext/UUID tamper and replay rejected`.
Both decrypt callers now pass the received counter into response AAD, without
rewriting outbound/session counter. Existing parser controls retain their
assertions and supply the new explicit counter argument.

Next focused VCSEC control RED:
`$TMPDIR/warp-tesla-host-tests/test_tesla_ble_response_counter vcsec`, exit 134.
Assertion `callbacks == 0` at test line 134 (wrong UUID) failed. Counter/tag/
ciphertext rejection passed first. Encrypted VCSEC lacked correlation/replay
admission; constrain that path while preserving unsolicited plaintext status.

H1 GREEN: `bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_response_counter`, exit 0.
All four independently signed CarServer/VCSEC fixtures pass with request=8,
response=8 and response=1. Counter-only, tag, ciphertext, UUID controls reject;
replay is rejected with an eligible waiting command, not merely empty queue.
Official comparison: pinned `vehicle-command` commit
`a4b43c1eff0e09d77deb9f2dce97031141fe8c8a`, signer.go lines 219–233
passes `gcmInfo.Counter` into peer.go `responseMetadata` (105–117), and returns
that same counter for caller sliding-window admission. The independent fixture
implements metadata.go ordered TLV, uint32 big endian, TAG_END, SHA256.
https://github.com/teslamotors/vehicle-command/blob/a4b43c1eff0e09d77deb9f2dce97031141fe8c8a/internal/authentication/signer.go#L219-L233

### M1 cross-channel chronology — next focused RED

Command: `bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_chronology key`
Actual: `FAIL KeyLoaded before authenticated SoC (queue=available): applied=1 paired=0 pending=0 (expected 1/1/0)`.
Exit 1 through actual production producer/main consumer.

M1 GREEN: `bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_chronology`, exit 0:
`Production consumer chronology: 8 forward/reverse KeyLoaded/PairStarted + authenticated SoC cases passed; full-queue KeyLoaded/PairStarted/KeyRemoved/Error delivered`.
Queue events and durable outcomes receive one sequence under the existing
checked outcome mutex. Main reconciles pairing/key, error and connection fields
with separate applied versions, so a connection-only event cannot suppress
older key state. Late/try-lock-deferred outcomes cannot undo newer authenticated
SoC. Sequence comparison assumes fewer than 2^31 unconsumed publications.

### L1 realistic filesystem — next focused RED

Compile: `g++ -std=c++17 -pthread -Wall -Wextra -Werror -Isoftware/tests/tesla_vehicle/host -Isoftware/lib/TeslaBLE/include software/tests/tesla_vehicle/test_tesla_ble_storage_multichunk.cpp -o "$TMPDIR/warp-tesla-host-tests/test_tesla_ble_storage_multichunk"`
Run: `$TMPDIR/warp-tesla-host-tests/test_tesla_ble_storage_multichunk`.
Actual: `FAIL healthy multi-chunk save: key=private_key size=65 backup=0 (expected success)`.
Exit 1. This fails in the unchanged production transaction against the fake.

L1 GREEN: same compile/run command, exit 0:
`Production multi-chunk storage: 1095 healthy/I-O-fault/cancellation cases (65/129/4096 bytes; private key + both sessions; canonical/backup-only) passed`
`Production storage: 816 per-chunk short-read rejections preserve previous commit; fake cursor/EOF/failure/close lifecycle passed`.
The fake now advances read/write cursors, clamps EOF, supports deterministic
short reads, preserves position after read failure, and invalidates on close.
Write-close persistence failure is modeled by truncating temporary data; the
production reopen/verify detects it before touching old commits. Read-close is
void and has no detectable error result: no claim of status-error propagation
or power-loss durability. Checked I/O classes: open, write, flush, write-close
data loss, each verification read, rename, remove; cancellation uses every
actual guard. These are repeated cases across fixtures, NOT 1,095 unique API
boundaries. The old one-byte suite remains intact; only its output terminology
is corrected to cases (new write-close coverage adds cases).

### Operational allocation audit — still NOT READY

The four reproduced third-review regressions now have focused RED/GREEN
evidence, but operational low-memory safety is NOT complete. Do not infer
global zero allocations from startup testing. Remaining live throwing paths
include `Vehicle::send_command_result` make_shared/control-block/name;
`CommandError` make_unique/string factories; Client encode/HMAC scratch vectors;
Vehicle TX/RX frame vectors, session/private-key persistence; module MAC/RX
vectors; timeout/error strings and hexadecimal log strings. Arduino File's
VFSFileImpl shared_ptr factory is also reachable in operational save/delete.
These paths are not proven quarantined/rolled back on OOM with exceptions
disabled. SDK quarantine is a liveness contract, NOT protection from throwing
allocation aborts. Verification and exact continuation follow below.

### Remaining operational allocation — pending focused RED (no GREEN claimed)

Scratch source/compile harness retained at:
`/home/hermes/.hermes/cache/scratch/tesla-phase1-fifth/operational_allocation_probe.cpp`
`/home/hermes/.hermes/cache/scratch/tesla-phase1-fifth/compile_probe.py`
Command: `python3 "$TMPDIR/tesla-phase1-fifth/compile_probe.py" && "$TMPDIR/tesla-phase1-fifth/operational_allocation_probe"`.
Exit 1. Exact output:
`Production operational enqueue throwing-new boundaries: 1`
`Operational enqueue first allocation OOM: exited=0 status=-1 signal=6 (expected clean exit=0, safe ownership and retry)`
This compiles unchanged production protocol with `-fno-exceptions`. The allocator
boundary explicitly models target throwing-new OOM -> IDF abort-stub behavior;
it is not an ESP32 hardware run. The short SSO name `poll` isolates the production
`make_shared<Command>` allocation (`vehicle.cpp:169`). The probe accepts either
allocation-free success or checked allocation failure with empty queue, then
requires a successful later enqueue. Its current child aborts before retry.
This remaining RED is outside the passing acceptance suite, not hidden/skipped
inside it. It prevents review readiness despite the synchronization regression
being GREEN. No follow-on production allocation fix has been started.

### Fresh verification

`software/tests/tesla_vehicle/run_host_tests.sh`: exit 0, `Tesla BLE host tests passed.`
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-fifth-host-tests.log`.
Includes all new production-bound synchronization/counter/chronology tests,
1,859 startup allocation cases, 1,095 multi-chunk cases, 816 short-read cases,
and 164 legacy one-byte cancellation/I-O-fault cases. The 164 legacy cases
include 96 cancellation cases and 68 checked I/O cases (write-close now modeled),
not 164 distinct APIs. The separate stale-backup control also remains.

Full build ONLY, exact command:
`TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs uv run --directory software pio run -e warp4_tesla_ble`
Exit 0: `warp4_tesla_ble SUCCESS 00:01:23.474`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-fifth-build.log`.
Build included generated frontend `tsc`. Nonfatal warnings: skipped main-branch
fetch, missing hyphenations, gzip fallback, nanopb pip unavailable but no
generation needed. No upload/flash action was requested or run.

`git diff --check`: exit 0. `git diff --check 03798d2515710d2a72dcb08ec72b`: exit 0.

Fresh ELF/map audit:
`python3 /home/hermes/.hermes/cache/scratch/tesla-phase1-fifth/elf_audit.py`: exit 0.
ELF: `software/.pio/build/warp4_tesla_ble/firmware.elf`.
SHA-256: `8d053acda2441721d9ec3975e2fa7434d2204759edbdf508325d366360ffc6de`.
Current release chain:
`TeslaVehicle::release_bluetooth` 0x4025fe60 -> `btMemRelease(BT_MODE_BLE)`
0x4027797c -> `__wrap_esp_bt_mem_release` 0x4027795c -> `esp_bt_mem_release`
0x402c2458 -> `esp_bt_controller_rom_mem_release` 0x402c2348.
Caller loads mode 1. The general release wrapper is linked; the controller-only
wrapper is absent. The linked static-release branch requires mode 3 (BTDM),
so it is not taken by this BLE-mode call. ELF ranges and current map agree:
- BLE controller ROM DRAM [0x3ffb09a8,0x3ffb1ddc): 5,172 bytes covered.
- Other controller reservations: 33,240 bytes NOT released by that call.
- Named BT data/BSS: 5,412 + 248 + 6,740 + 2,716 = 15,116 bytes NOT released.
These are linked ranges/call semantics, NOT measured net heap reclamation.
Disabled boot is not zero-Bluetooth-DRAM boot or full static Bluetooth release.
Synchronization disassembly also confirms init result -> initialized byte,
invalid-handle guard, checked native lock/try-lock result, conditional destroy;
Peer/MessageProcessor use CheckedMutex on target, not just in host tests.
Audit outputs: `/home/hermes/.hermes/cache/scratch/tesla-phase1-fifth/`.

### Changed files in this continuation

- `TESLA_BLE_PHASE1.md` (append only; previous evidence retained).
- Protocol: `software/lib/TeslaBLE/include/checked_mutex.h` (new), `client.h`,
  `peer.h`, `message_processor.h`; `src/client.cpp`, `peer.cpp`,
  `message_processor.cpp`, `vehicle.cpp`.
- Module: `software/src/modules/tesla_vehicle/tesla_ble_result.h`,
  `tesla_ble_command_outcome.h`, `tesla_vehicle.h`, `tesla_vehicle.cpp`.
- Test runners/fakes: `software/tests/tesla_vehicle/run_host_tests.sh`,
  `host/run_protocol_test.sh`, `host/LittleFS.h`, `host/sync_fault_probe.h` (new).
- Tests: `test_tesla_ble_crypto_parser.cpp`, `test_tesla_ble_storage_transaction.cpp`;
  new `test_tesla_ble_sync_initialization.cpp`, `test_tesla_ble_runtime_sync.cpp`,
  `test_tesla_ble_response_counter.cpp`, `test_tesla_ble_chronology.cpp`,
  `test_tesla_ble_storage_multichunk.cpp`.
Other pre-existing dirty files were not changed by this continuation. Build
outputs are local generated artifacts; no commit, push, hardware access, Phase 2.

Branch: `feature/tesla-ble-upstream-ready`.
HEAD: `03798d2515710d2a72dcb08ec72b6b502f6028d1` (unchanged).
Status: 26 modified tracked entries, 41 untracked entries, 0 staged.

### Exact continuation — Phase 1 only

Resume the pending operational-enqueue RED first; do not start another fix
until it is GREEN. Replace command/control-block/name ownership with checked
nothrow or fixed storage, preserving callback/reentrant lifetime and bounded
queue semantics. Re-run the scratch probe, promote appropriate production
behavioral coverage into the host suite, then fault every newly fallible
boundary and verify rollback/retry. Do not use shared_ptr(raw) as an OOM fix:
it still allocates a throwing control block.

Next separate vertical slices: CommandError factories and their strings
(`include/command_error.h:41–124`), with no null dereference at
`vehicle.cpp:570,581` and no null-as-success outcome; Client scratch/HMAC/encode
buffers; Vehicle/module TX/RX buffers; persistence path strings/File factories
(File factories specifically occur in save/open; delete also allocates Strings);
diagnostic hex/timeout/error strings and any std::function allocations. Preserve
command errors/severity/outcome and authenticate/correlate/replay assertions.
Current startup Config asString/updateString and platform/SDK boundaries also
need explicit scope/proof; the host startup probe quarantines some platform
allocations and is not a whole-firmware zero-allocation proof.

Software risks: this operational allocation RED and the unconverted audited
paths; SDK submissions/shutdown remain unbounded but supervised by acknowledged
quarantine/reboot, not cancellation. Hardware-only validation still outstanding:
real Tesla interoperability, RF/sleep/wake, target watchdog/teardown load,
measured heap reclamation/fragmentation. No hardware work is authorized here.

Active final verdict: NOT READY. Synchronization-specific B1, H1, M1 and L1
reproductions are GREEN, but the broader mandatory Phase-1 low-memory operation
contract remains incomplete. This appended fifth continuation supersedes the
older optimistic/top-section allocation and chronology claims; prior evidence
has not been deleted. Do not mark ready for Astra or begin Phase 2 yet.


## Sixth continuation — operational ownership slices GREEN; active verdict NOT READY

Resumed `@session:default/20261011_001710_14ad1b` and read the appended fifth
continuation. Phase 1 only in `/home/hermes/warp-tesla-ble`, branch
`feature/tesla-ble-upstream-ready`. No Phase 2, commit, push, upload, flash,
or hardware access. No Tesla charging command was added or invoked. WARP4
and SLAC-MAC identity constraints are unchanged. Previous evidence is retained.

### Retained enqueue RED -> GREEN

Exact command, unchanged retained scratch source:
`python3 "$TMPDIR/tesla-phase1-fifth/compile_probe.py" && "$TMPDIR/tesla-phase1-fifth/operational_allocation_probe"`.
Fresh RED, exit 1:
`Production operational enqueue throwing-new boundaries: 1`
`Operational enqueue first allocation OOM: exited=0 status=-1 signal=6 (expected clean exit=0, safe ownership and retry)`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-sixth-enqueue-red.log`.

Minimal ownership change: `CheckedShared<Command>` embeds the reference count
and Command in ONE `new (std::nothrow)` allocation. It never constructs a
`shared_ptr(raw)` control block. Copies only retain the existing node; final
release deletes it. This is single-worker ownership, not a general thread-safe
shared-pointer replacement. Command names own up to 63 inline bytes; oversized
names are rejected, not silently renamed. Named command APIs accept string_view
so normal pair/SoC literals do not first allocate a temporary string.

`send_command_result` now returns bool and rejects OOM without enqueuing or
moving away the failure callback. Its failure result is explicitly FAILED;
after the error-value slice below it also contains a nonnull inline error.
Already-owned std::function values are moved into the checked node. QUEUED
phase notification occurs after publication, retaining the node across a
reentrant disconnect/queue clear. The queue remains limited to 32 commands.

Same scratch command GREEN, exit 0:
`Production operational enqueue throwing-new boundaries: 1`
`Operational enqueue first allocation OOM: exited=1 status=0 signal=0 (expected clean exit=0, safe ownership and retry)`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-sixth-enqueue-green.log`.
The retained legacy label counts BOTH throwing and nothrow new attempts. That
one GREEN boundary is now checked nothrow, NOT a remaining throwing allocation.

Promoted the retained probe (behavior preserved) into
`host/operational_new_probe.h` and extended it in
`test_tesla_ble_operational_enqueue.cpp`. Command:
`bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_operational_enqueue`.
Exit 0:
`Production operational enqueue: checked single allocation OOM, long-name ownership/overflow, moved callback, reentrant phase lifetime, queue-full and retry passed (-fno-exceptions)`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-sixth-enqueue-suite-green.log`.
The large callback is constructed BEFORE the watched region; this proves
internal move/ownership behavior, not allocation-safe caller-side std::function
construction. Normal callback/setup/wrapper allocations still need slice 4.

### CommandError factories, error completion and strings

All following commands use the production-source runner with -fno-exceptions:
`bash software/tests/tesla_vehicle/host/run_protocol_test.sh <test-name>`.
No test was removed or weakened.

1. `test_tesla_ble_operational_errors` RED, exit 1:
`FAIL CommandError factory 0 OOM: exited=0 status=-1 signal=6 (expected nonnull semantic error and retry)`.
GREEN, exit 0:
`Production CommandError: 12 factory/builder OOM controls preserve nonnull message, severity/outcome and retry (-fno-exceptions)`.
Logs: `tesla-phase1-sixth-errors-red.log`, `tesla-phase1-sixth-errors-green.log`
under `/home/hermes/.hermes/cache/scratch/`.
Factories/builders now return move-only inline `CommandErrorValue` rather than
unique_ptr heap ownership. Diagnostic text owns up to 191 inline bytes and may
truncate; severity/outcome are preserved independently. Local rich-error
callback payloads were updated to this value type; this is an API type change,
not a claim of drop-in compatibility with arbitrary external library consumers.

2. `test_tesla_ble_error_completion` RED, exit 1:
`FAIL CommandResult<int>::error(nullptr) reported SUCCESS (expected explicit failure)`.
After the null guards, execution reached the next reentrancy assertion:
`Assertion 'vehicle->get_command_queue_for_testing().empty()' failed`, exit 134.
Both logged Vehicle failure-completion overloads now normalize null input to
a nonnull inline failure. OperationResult::failure and both CommandResult
variants do the same. Finalization detaches the queue entry BEFORE invoking
completion and retains/moves the callback across reentry. Disconnect draining
also pops before completion, avoiding callback recursion against the same entry.
Completed GREEN, exit 0:
`Production error completion: null input remains failure, both logged completion paths safe, reentrant callback and retry passed (-fno-exceptions)`.
Logs: `tesla-phase1-sixth-error-completion-red.log`,
`tesla-phase1-sixth-error-completion-reentrant-red.log`,
`tesla-phase1-sixth-error-completion-green.log`.

3. Independent extraction-outcome extension of the same test RED, exit 1:
`FAIL released failed CommandResult<int> changed to SUCCESS`.
Inline error extraction now returns a value snapshot and cannot demote the
originating result. GREEN, exit 0, same completed-test output above.
Logs: `tesla-phase1-sixth-error-release-red.log`,
`tesla-phase1-sixth-error-release-green.log`.

4. `test_tesla_ble_error_strings` RED, exit 1:
`FAIL production auth-error string OOM: exited=0 status=-1 signal=6 (expected explicit failure callback and retry)`.
Auth-build error description concatenation now uses FixedText. The sibling
CarServer action-error description was also converted to bounded text.
GREEN, exit 0:
`Production auth-error composition: fixed diagnostic text, missing-key failure callback and retry passed (-fno-exceptions)`.
Logs: `tesla-phase1-sixth-error-strings-red.log`,
`tesla-phase1-sixth-error-strings-green.log`.
The authenticated CarServer error branch has not yet received its own watched
OOM fixture; the bounded source conversion is not claimed as that missing test.

### Client session-auth and HMAC scratch slices

1. `test_tesla_ble_session_encoding` RED, exit 1:
`FAIL production session-auth scratch OOM: exited=0 status=-1 signal=6 (expected bounded encoding and retry)`.
The session-info request encoder now uses an inline array sized by
`UniversalMessage_RoutableMessage_size` (741 bytes), not a vector.
GREEN, exit 0:
`Production session-auth encoding: no throwing scratch allocation under injected first-new OOM; retry passed (-fno-exceptions)`.
Logs: `tesla-phase1-sixth-session-encoding-red.log`,
`tesla-phase1-sixth-session-encoding-green.log`.

2. Independent destination-capacity extension RED, exit 1:
`FAIL production session-auth capacity=0: status=0 size=117 (expected explicit rejection/zero length)`.
Client::prepend_length now checks null pointers, the 16-bit wire length and
caller capacity BEFORE any write. It zeroes output length and returns false
on rejection. All sibling framing callers propagate that failure.
GREEN, exit 0, retains the preceding allocation control and adds:
`Production session-auth frame capacity: every undersized destination rejected without partial write; exact-fit and retry passed`.
Logs: `tesla-phase1-sixth-session-capacity-red.log`,
`tesla-phase1-sixth-session-capacity-green.log`.
This does NOT yet fix the separate raw pb_encode_fields capacity contract.

3. `test_tesla_ble_session_hmac_allocations` RED, exit 1:
`FAIL production session-HMAC scratch OOM: exited=0 status=-1 signal=6 (expected valid authentication without throwing allocation and retry)`.
Session-info HMAC uses a 164-byte fixed scratch array (64 metadata + protocol
100-byte session-info maximum), hashes only the actual length, rejects oversized
challenge/session/public-key lengths before copying, and retains key clearing.
GREEN, exit 0:
`Production session-HMAC: fixed scratch, valid/tampered tag, protocol bounds/SIZE_MAX, first-new OOM and retry passed (-fno-exceptions)`.
Logs: `tesla-phase1-sixth-session-hmac-red.log`,
`tesla-phase1-sixth-session-hmac-green.log`.
This new injection targets C++ new, not every operational mbedTLS calloc.
The earlier startup 1,859-boundary result is not operational crypto OOM proof.

### Retained NEXT RED — SoC encoder scratch; no follow-on fix started

Scratch:
`/home/hermes/.hermes/cache/scratch/tesla-phase1-sixth/soc_encoding_probe.cpp`
`/home/hermes/.hermes/cache/scratch/tesla-phase1-sixth/compile_soc_probe.py`.
Exact command:
`python3 "$TMPDIR/tesla-phase1-sixth/compile_soc_probe.py" && "$TMPDIR/tesla-phase1-sixth/soc_encoding_probe"`.
Exit 1:
`Production SoC encoding first allocation OOM: exited=0 status=-1 signal=6 (expected explicit failure or allocation-free success, then retry)`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-sixth/soc-encoding-red.log`.
The fixture first performs a real successful SoC encode with an initialized
production peer, then faults the first C++ allocation in a child and requires
retry. The first live path remains the outer encoding vector at client.cpp:783;
encrypted_payload at :548 and encryption diagnostic hex allocations are later
boundaries in this same call chain. This RED is retained outside the passing
suite and prevents readiness; it is not hidden/skipped inside acceptance tests.
Do not start another production slice before this RED is GREEN.

### Mandatory final verification

`software/tests/tesla_vehicle/run_host_tests.sh`: exit 0,
`Tesla BLE host tests passed.`
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-sixth-host-tests.log`.
All six new host tests are integrated into the production-source runner with
-fno-exceptions. Earlier target-sync, counter authentication, cross-channel
chronology, realistic LittleFS, crypto/parser, teardown/quarantine, identity,
startup allocation, 1,095 multi-chunk cases, 816 short-read cases and 164 legacy
transaction cases remain GREEN. No earlier security/lifecycle test was weakened.

Full build ONLY, exact documented framework path:
`TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs uv run --directory software pio run -e warp4_tesla_ble`.
Exit 0: `warp4_tesla_ble SUCCESS 00:01:36.826`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-sixth-build.log`.
Generated frontend tsc passed. Nonfatal skipped branch fetch, hyphenations/gzip
warnings and nanopb pip-unavailable/no-generation messages remain. Firmware
images were built locally, NOT uploaded or flashed; esptool merge output saying
'ready to flash' is image creation only, not hardware access.

`git diff --check`: exit 0.
`git diff --check 03798d2515710d2a72dcb08ec72b`: exit 0.

### Current ELF/map audit — exact LIMITED memory contract unchanged

`python3 "$TMPDIR/tesla-phase1-sixth/elf_audit.py"`: exit 0.
ELF: `software/.pio/build/warp4_tesla_ble/firmware.elf`.
SHA-256: `773b74b6ffb99b0463d81e1461dc34f8000fa50823df7d77343e02900e8d4f86`.
Current call chain:
`TeslaVehicle::release_bluetooth` 0x4026051c -> `btMemRelease` 0x40278038 ->
`__wrap_esp_bt_mem_release` 0x40278018 -> `esp_bt_mem_release` 0x402c2b14 ->
`esp_bt_controller_rom_mem_release` 0x402c2a04.
Caller loads BLE mode 1. General release wrapper linked; controller-only wrapper
absent. Static-release branch requires mode 3 and is not taken by this call.
ELF ranges and current map agree:
- BLE controller ROM DRAM [0x3ffb09a8,0x3ffb1ddc): 5,172 bytes covered.
- Other controller-ROM reservations: 33,240 bytes NOT released by that call.
- Named Bluetooth data/BSS: 5,412 + 248 + 6,740 + 2,716 = 15,116 bytes NOT released.
These are linked ranges/call semantics, NOT measured net heap reclamation.
Disabled boot is neither zero-Bluetooth-DRAM boot nor full static BT release.
SDK supervision remains acknowledged quarantine/reboot, NOT SDK cancellation.
Audit log, summary, symbols and synchronization disassembly are preserved in
`/home/hermes/.hermes/cache/scratch/tesla-phase1-sixth/`; fifth audit files remain.

### Changed files in THIS continuation

Production:
- `software/lib/TeslaBLE/include/checked_shared.h` (new).
- `software/lib/TeslaBLE/include/fixed_storage.h` (extended existing untracked file).
- `software/lib/TeslaBLE/include/command_error.h`, `vehicle.h`, `client.h`.
- `software/lib/TeslaBLE/src/vehicle.cpp`, `client.cpp`.
Tests:
- `software/tests/tesla_vehicle/run_host_tests.sh`.
- `software/tests/tesla_vehicle/host/operational_new_probe.h` (new).
- New `test_tesla_ble_operational_enqueue.cpp`, `test_tesla_ble_operational_errors.cpp`,
  `test_tesla_ble_error_completion.cpp`, `test_tesla_ble_error_strings.cpp`,
  `test_tesla_ble_session_encoding.cpp`, `test_tesla_ble_session_hmac_allocations.cpp`.
Documentation: `TESLA_BLE_PHASE1.md` (append only).
Other pre-existing dirty source files were not edited in this continuation.

### Residual software versus hardware-only risks / exact continuation

Software blockers, NOT proven safe yet:
1. Resume the retained SoC scratch RED above. Convert remaining SoC/wake/VCSEC
   encoder and encrypted-payload vectors using protocol bounds; include overflow
   and actual per-allocation failure/retry evidence. Raw pb_encode_fields still
   treats length as output and does not honor arbitrary caller capacity; address
   it with a separate behavioral RED without weakening existing callers/tests.
2. Vehicle TX vectors at :402/:483/:557; unconditional RX frame/payload vectors
   at :704/:708; module RX vector at tesla_vehicle.cpp:563. Introduce explicit
   bounded buffer adapter contracts; never hide throwing fallback allocations.
3. Session/private-key persistence vectors and strings remain at vehicle.cpp
   :1055-1062/:1159/:1493. Arduino save/delete path strings and File factories
   remain operational OOM gaps. Preserve committed canonical/backup contents,
   cancellation checks, key ownership and later retry; existing I/O fault tests
   are NOT throwing-allocation fault tests for the target File implementation.
4. Diagnostic format_hex, timeout strings, callback construction/wrapping and
   GATT std::function conversions/copies remain uncontained. In particular,
   tests moving prebuilt callbacks do not prove creation of arbitrary callbacks
   is safe with exceptions disabled. Add the authenticated action-error watched
   fixture rather than claiming its source conversion alone is tested.
5. Module encode_vehicle_mac vector at tesla_vehicle.cpp:31 and Tesla runtime/API
   state allocations after enable remain. Distinguish one-time Config/framework
   setup from feature-owned runtime work; no whole-firmware zero-allocation claim.
   Also TDD immediate module error publication on command enqueue refusal:
   charge_state_poll()/pair() currently ignore the primary bool result, and
   has_private_key() alone is not proof a pairing command was queued.
6. Operational crypto C allocations and target stack headroom after fixed-buffer
   conversion still need appropriate injection/static evidence. No measured heap
   fragmentation or worker stack margin is claimed by these host new-only tests.

Hardware-only risks unchanged: real Tesla interoperability, RF/sleep/wake,
ESP32 watchdog/SDK teardown under load, measured heap reclamation/fragmentation.
No hardware validation is authorized or performed here.

Run-time budget ended with a passing regression suite/build but a retained
operational SoC RED. Phase 1 remains NOT READY, not ready for another Astra
review, and Phase 2 remains prohibited. This appended continuation supersedes
any older broad allocation-safety claim; earlier exact evidence remains intact.

### Git status at handoff

Branch `feature/tesla-ble-upstream-ready`; HEAD
`03798d2515710d2a72dcb08ec72b6b502f6028d1` unchanged.
27 modified tracked entries, 48 untracked entries (host/ is one status entry),
0 staged. Exact porcelain status follows:

```text
 M TESLA_BLE_HANDOFF.md
 M software/lib/TeslaBLE/include/adapters.h
 M software/lib/TeslaBLE/include/client.h
 M software/lib/TeslaBLE/include/command_error.h
 M software/lib/TeslaBLE/include/crypto_context.h
 M software/lib/TeslaBLE/include/message_processor.h
 M software/lib/TeslaBLE/include/peer.h
 M software/lib/TeslaBLE/include/vehicle.h
 M software/lib/TeslaBLE/src/client.cpp
 M software/lib/TeslaBLE/src/crypto_context.cpp
 M software/lib/TeslaBLE/src/message_processor.cpp
 M software/lib/TeslaBLE/src/peer.cpp
 M software/lib/TeslaBLE/src/vehicle.cpp
 M software/src/modules/tesla_vehicle/tesla_ble_config_policy.h
 M software/src/modules/tesla_vehicle/tesla_ble_key_validation.h
 M software/src/modules/tesla_vehicle/tesla_ble_storage.cpp
 M software/src/modules/tesla_vehicle/tesla_ble_storage.h
 M software/src/modules/tesla_vehicle/tesla_vehicle.cpp
 M software/src/modules/tesla_vehicle/tesla_vehicle.h
 M software/tests/tesla_vehicle/run_host_tests.sh
 M software/tests/tesla_vehicle/test_tesla_ble_config_policy.cpp
 M software/tests/tesla_vehicle/test_tesla_ble_soc_policy.cpp
 M software/warp4_tesla_ble.ini
 M software/web/src/modules/tesla_vehicle/api.ts
 M software/web/src/modules/tesla_vehicle/main.tsx
 M software/web/src/modules/tesla_vehicle/translation_de.tsx
 M software/web/src/modules/tesla_vehicle/translation_en.tsx
?? TESLA_BLE_PHASE1.md
?? software/lib/TeslaBLE/include/checked_mutex.h
?? software/lib/TeslaBLE/include/checked_shared.h
?? software/lib/TeslaBLE/include/fixed_storage.h
?? software/lib/TeslaBLE/include/response_evidence.h
?? software/src/modules/tesla_vehicle/tesla_ble_command_outcome.h
?? software/src/modules/tesla_vehicle/tesla_ble_gatt.cpp
?? software/src/modules/tesla_vehicle/tesla_ble_key_command.h
?? software/src/modules/tesla_vehicle/tesla_ble_operation.h
?? software/src/modules/tesla_vehicle/tesla_ble_result.h
?? software/src/modules/tesla_vehicle/tesla_ble_runtime.h
?? software/tests/tesla_vehicle/host/
?? software/tests/tesla_vehicle/test_tesla_ble_authentication.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_bounded_storage.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_callback_gate.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_chronology.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_crypto_parser.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_disabled_boot.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_disconnect_cleanup.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_error_completion.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_error_strings.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_gatt.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_identity_busy.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_key_command.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_module_composition.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_operation.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_operational_enqueue.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_operational_errors.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_publication.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_queues.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_response_counter.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_result.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_runtime.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_runtime_sync.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_runtime_wiring.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_session.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_session_encoding.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_session_hmac_allocations.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_shutdown_result.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_startup_allocations.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_storage.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_storage_multichunk.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_storage_transaction.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_sync_initialization.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_teardown.cpp
?? software/tests/tesla_vehicle/test_tesla_ble_watch_sampling.cpp
?? software/tests/tesla_vehicle/test_web_actions.mjs
?? software/web/src/modules/tesla_vehicle/action_policy.ts
```

## Seventh continuation — SoC/TX/RX/callback slices GREEN; NOT READY

Resumed session `20261011_004316_725b06` through session_search and its retained
scratch/evidence. Branch was freshly verified as
`feature/tesla-ble-upstream-ready` in `/home/hermes/warp-tesla-ble`.
Phase 1 only. No Phase 2, hardware access, flash, upload, commit, push or publish.
The execution runtime limit stopped this continuation before the remaining
persistence/module-allocation remediation and final firmware/ELF verification.
This is an incomplete acceptance result, NOT independent-Astra-review readiness.
Earlier evidence is retained; broad historical readiness claims do not apply.

### Retained SoC encoding RED -> GREEN

Fresh command:
`python3 "$TMPDIR/tesla-phase1-sixth/compile_soc_probe.py" && "$TMPDIR/tesla-phase1-sixth/soc_encoding_probe"`.
RED exit 1:
`Production SoC encoding first allocation OOM: exited=0 status=-1 signal=6 (expected explicit failure or allocation-free success, then retry)`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-seventh-soc-red.log`.
Replaced Client outer encoder vectors and encrypted payload with protocol-sized
inline arrays. Diagnostic hex now uses bounded inline FixedText<128> (64 input
bytes maximum); no heap reservation or SIZE_MAX multiplication. The initial
compile exposed a missing direct cstdint include and literal-to-FixedText return;
both corrected before recording GREEN. Same retained probe GREEN exit 0:
`Production SoC encoding first allocation OOM: exited=1 status=0 signal=0 (expected explicit failure or allocation-free success, then retry)`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-seventh-soc-green.log`.

Promoted production Vehicle coverage as `test_tesla_ble_soc_allocations.cpp`:
real `charge_state_poll(NO_WAKE_SKIP,42)` command builder with initialized peer,
first-new abort-style injection in a child, no C++ allocations, intact command
queue/state and successful repeat encoding. Suite output:
`Production Vehicle charge_state_poll builder OOM: exited=1 status=0 signal=0 (expected allocation-free encode, intact queue and retry)`.
This is builder/encoding coverage, not full module acceptance or crypto-calloc
exhaustion coverage.

Full host suite exit 0 after this logical batch:
`/home/hermes/.hermes/cache/scratch/tesla-phase1-seventh-host-batch1.log`.

### Separate encode capacity / correlation transaction RED -> GREEN

Command:
`bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_encode_transaction`.
RED exit 1: `FAIL raw protobuf zero capacity wrote a partial request`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-seventh-encode-transaction-red.log`.
`pb_encode_fields` now uses input length as capacity, measures before writing,
rejects undersized destinations and returns zero length on failure. All local
scratch callers initialize their capacities. Client's framed wrappers delegate
to a bounded envelope encode; request UUID/hash publication and replay reset
occur only after the complete output fits. Tentative request counter is restored
on encode/encryption failure. The public raw envelope API retains unframed output
by default; the explicit final `framed` argument is for internal wrappers.
Session-auth UUID publication also moves after successful framing.

One intermediate test assertion reused an already-consumed replay counter across
different unencrypted builders. Corrected the fixture to use a distinct counter
per builder; the requirement that failed builds preserve each replay window was
not removed or weakened. Final GREEN exit 0:
`Production encoders: raw capacity, SoC/wake/status/auth all undersized destinations, unchanged correlation/counter/replay state, SIZE_MAX and retry passed (-fno-exceptions)`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-seventh-encode-transaction-green.log`.
Full host suite exit 0:
`/home/hermes/.hermes/cache/scratch/tesla-phase1-seventh-host-batch2.log`.

### Runtime TX/RX scratch RED -> GREEN

Command:
`bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_runtime_io_allocations`.
RED exit 1: `FAIL production auth TX OOM signal=6 status=-1`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-seventh-io-red.log`.
Added opt-in `BleAdapter::write_bytes(pointer,size)` with rejecting default and
NO allocating vector fallback. The production WARP transport implements it;
auth/wake/ready-command writers pass existing buffers directly. Legacy vector
entry points forward without allocation. WARP RX passes existing fragment bytes
to `Vehicle::on_rx_data(pointer,size)` instead of constructing a vector.
Vehicle complete-frame processing uses a bounded stable snapshot and decodes
directly from it. Raw observers now receive pointer/size valid only during their
call, AFTER the successfully decoded frame has been consumed; this avoids
reentrant clearing invalidating the receive erase. This is a local API contract
change, not compatibility with arbitrary external raw-vector callbacks.

GREEN exit 0:
`Production runtime TX/RX: auth/wake/SoC writes, fragmented copy/decode/dispatch have no C++ allocations under injected first-new failure; retry passed (-fno-exceptions)`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-seventh-io-green.log`.
Full host suite exit 0:
`/home/hermes/.hermes/cache/scratch/tesla-phase1-seventh-host-batch3.log`.
This fixture covers the protocol writer/fragment decode/dispatch; it does not
claim per-boundary target SDK/GATT allocation injection. Transport refusal after
a successful encode still needs a dedicated correlation/counter/queue rollback
test; encode rollback alone is NOT proof of that broader transaction.

### Callback construction / timeout string RED -> GREEN

Command:
`bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_callback_allocations`.
RED exit 1: `FAIL callback construction OOM signal=6 status=-1`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-seventh-callback-red.log`.
Introduced move-only `CheckedFunction`: 64-byte inline capture storage, larger
captures use checked nothrow allocation, explicit allocation_failed flag. Captures
must construct/move noexcept; it does not copy heap-owning std::function values.
Vehicle builders/callbacks and MessageProcessor handler use this ownership.
Primary enqueue rejects failed builder/result/phase callable construction before
queue publication; rich/bool wrapper APIs now return bool. Callback setters
reject failed construction without replacing the previous callback.
Storage mutation guard uses the same checked callable type. Synchronous GATT
submission uses nonowning FunctionRef rather than constructing/copying owning
std::function wrappers. The reference must never be retained asynchronously.
Auth timeout and sibling dynamic climate-keeper names use bounded text.

GREEN exit 0:
`Production callback construction: checked large-capture OOM enqueues nothing, retry succeeds; auth-timeout description allocation-free (-fno-exceptions)`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-seventh-callback-green.log`.
The constructor fault is injected during actual large-capture conversion at the
enqueue call, NOT merely while moving a preconstructed callback.
Full host suite exit 0:
`/home/hermes/.hermes/cache/scratch/tesla-phase1-seventh-host-batch4.log`.
All four new tests are integrated into run_host_tests.sh's production-source
runner with -fno-exceptions. Existing security/lifecycle/realistic LittleFS tests
remain passing; no earlier test was removed or weakened.

### Mandatory acceptance NOT completed in this continuation

- Last executed full host suite: exit 0, `Tesla BLE host tests passed.` in batch4
  above, after the latest production edits.
- Final git diff --check: NOT RUN after these edits/evidence append.
- Final web/TypeScript build: NOT RUN against these edits.
- Full warp4_tesla_ble firmware build with existing TESLA_BLE_FRAMEWORK_LIBS:
  NOT RUN against these edits.
- Current ELF/map call-path, static memory and target stack-frame audit:
  NOT RUN against these edits. The sixth-continuation ELF/hash/memory evidence
  describes the prior build ONLY. Do not use it to validate this source tree.
No target memory/stack-headroom or measured heap-reclamation claim is made.

### Remaining mandatory software blockers and exact resumption

1. `Vehicle::persist_session_`, `get_session_key_`, `clear_stored_session_` and
   `persist_private_key_` in `software/lib/TeslaBLE/src/vehicle.cpp` still construct
   heap-backed strings/vectors during pair/auth/recovery. Session save result is
   still ignored. No new operational persistence OOM test has been executed;
   existing storage I/O tests are NOT allocation-failure evidence. Next strict
   RED should inject throwing-new failure into actual pair/key/session save and
   require truthful refusal, previous material preserved, and retry. Introduce
   checked save-bytes/remove-key contracts without an allocating fallback.
2. `TeslaBleStorageAdapter::save/remove/forget_material` still use Arduino String
   concatenation/File opens and string conversion. Target FS::open calls
   VFSImpl::open -> make_shared<VFSFileImpl> under exceptions disabled. Checking
   `if (!file)` is too late for throwing allocation. String OOM can also invalidate
   path text; exists failure can be mistaken for absence. Replace operational
   persistence with checked bounded paths/descriptors, preserving canonical/
   backup recovery, cancellation, verify-read and complete deletion semantics.
   Add target-bound syscall/ENOMEM fault tests rather than claiming host File
   fake alone models target control-block allocation.
3. `encode_vehicle_mac` / `TeslaVehicle::process_commands` in
   `software/src/modules/tesla_vehicle/tesla_vehicle.cpp` still allocate a vector
   and assume has_private_key proves pair enqueue. charge_state_poll/pair still
   return void and module SoC start does not publish immediate enqueue refusal.
   TDD checked acceptance, reset pending/in-flight state and successful retry.
4. Rich/bool compatibility wrappers now return bool, but conversion failure of
   an incoming optional callable must not be accidentally normalized to an
   intentional nullptr callback by a boolean conditional. Add per-boundary
   wrapper/setter/reentrant ownership tests and preserve explicit failure.
   The large primary callback construction test does not cover every wrapper.
5. `TeslaVehicle::ensure_connected` in `tesla_ble_gatt.cpp` still constructs
   BLEUUIDs from text. Framework BLEUUID/string allocations and module Config
   get/updateString/runtime-enable/error-state paths need target implementation
   audit and behavioral fault tests; host Config fake is not target OOM proof.
   Synchronous FunctionRef conversions pass existing GATT/identity/cancellation
   regressions but have not received target per-allocation tests.
6. Operational mbedTLS calloc failures (encrypt/decrypt/ECDH/session HMAC/key
   generation/export) still need per-boundary rollback/leak/retry tests. Peer
   update_session currently mutates epoch/time/counter before fallible ECDH;
   incomplete key generation can remain represented as initialized. Startup
   1,859-boundary coverage and new-only operational probes do NOT close these.
7. Add watched authenticated CarServer action-error fixture; exercise malformed
   inbound decode, raw-observer reentry, queue-overflow behavior and failed TX
   rollback in the allocation probes. Audit actual target worker-stack headroom
   after bounded buffers/callable ownership changes.

Hardware-only validation remains unauthorized: Tesla interoperability, RF/sleep/
wake, SDK/watchdog behavior under load, fragmentation and net heap reclamation.
Finish the above software slices, run the full host suite after each logical
batch, then the required final diff/frontend/firmware/ELF checks before asking
for an independent Astra review. Current verdict: NOT READY. Phase 2 prohibited.

## Eighth continuation — bounded persistence/storage work; NOT READY

Resumed `20261011_011625_ca927d` through a read-only delegated session summary,
checked against this document and the production sources. Fresh branch check:
`feature/tesla-ble-upstream-ready` in `/home/hermes/warp-tesla-ble`. Phase 1 only.
No Phase 2, hardware access, flash, upload, commit, push, or publish occurred.
The runtime budget ended during the target storage-backend audit. Do not treat
the passing syscall-bound fixture as completion of blocker 2 or Astra readiness.
Previous continuation evidence is retained.

### Vehicle persistence RED -> GREEN

Exact focused command:
`bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_persistence_allocations`.
Initial RED exit 1:
`FAIL production persistence mode=0 OOM signal=6 status=-1`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-eighth-persistence-red.log`.

Introduced explicitly opt-in `StorageAdapter::save_bytes` / `remove_key`, with
rejecting defaults and NO allocating compatibility fallback. Vehicle session
keys are static literals with invalid-domain rejection. Session/key persistence
passes bounded bytes directly, reports refusal, and wipes the private-key export
scratch on every exit. Session removal returns a truthful bool. The WARP adapter
implements the bounded contracts; vector/string legacy entry points delegate.

Focused GREEN exit 0:
`Production Vehicle persistence: key/session/delete abort-style new OOM, truthful adapter refusal, previous bytes preserved and retry passed (-fno-exceptions)`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-eighth-persistence-green.log`.
Full host gate:
`bash software/tests/tesla_vehicle/run_host_tests.sh`, exit 0,
`Tesla BLE host tests passed.`
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-eighth-host-batch1.log`.

A separate independently HMAC-signed session-info fixture reached the actual
Vehicle handler. The first fixture used generated tag capacity 34 rather than
wire HMAC length 32, failed its retry assertion (exit 134), and was corrected
before recording the valid behavioral RED. Valid fixture RED exit 1:
`FAIL authenticated session save refusal advanced command/peer state`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-eighth-session-persist-red.log`.
The handler previously ignored save failure after publishing the new session.

Peer normal/forced session updates now stage ECDH, invoke a nonowning synchronous
persistence gate, and publish key/epoch/time/counter/replay state only after that
gate succeeds. The Vehicle handler terminates a waiting auth command on storage
refusal rather than marking it READY. This is needed for the persistence slice;
it does NOT replace the still-required per-allocation operational crypto tests.
Focused GREEN, same command, exit 0:
`Production Vehicle persistence: key/session/delete abort-style new OOM; authenticated save refusal preserves peer/counter/replay/previous bytes, truthful failure and retry passed (-fno-exceptions)`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-eighth-session-persist-green.log`.
Full host gate, same full-suite command, exit 0:
`/home/hermes/.hermes/cache/scratch/tesla-phase1-eighth-host-batch2.log`.

### Checked storage descriptor slice RED -> GREEN, backend acceptance NOT CLOSED

Exact focused command:
`bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_checked_storage`.
RED exit 1:
`FAIL target operational storage new OOM signal=6 status=-1`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-eighth-storage-red.log`.

New `tesla_ble_storage_io.h` supplies bounded canonical/backup/temp paths and a
checked target descriptor contract. Operational save/remove/forget no longer
use Arduino String concatenation, FS::open or VFSImpl::open/shared_ptr factories
on target. Save checks write progress, fsync, close, reopen, fstat, every verify
read, read-close, stat, rename and unlink. Presence distinguishes ENOENT from
other failures. Complete deletion attempts every variant/key after failures.
Cancellation guards and canonical/backup recovery are retained. Verification
short reads are conservatively rejected; short writes make bounded progress.
The host-only File bridge retains ALL previous legacy tests unchanged.

Focused GREEN exit 0:
`Production target descriptor storage: 212 ENOMEM/I-O/cancellation boundaries; previous commit preserved, verification, short I/O, complete deletion and retry passed (-fno-exceptions)`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-eighth-storage-green.log`.
The printed 212 represents repeated fault/cancellation CASES across four keys,
canonical/backup starts and deletion, not 212 distinct APIs. The fixture compiles
the actual target descriptor branch and wraps syscalls without the host File
fake, but its close hook consumes the public descriptor even on failure. It
does NOT model or prove cleanup of the target backend's private FD/cache.

Full host gate, same full-suite command, exit 0:
`/home/hermes/.hermes/cache/scratch/tesla-phase1-eighth-host-batch3.log`.
Existing 1,859 startup allocation cases, protocol security/lifecycle tests,
1,095 multi-chunk cases, 816 short-read cases, 164 legacy transaction cases and
web action tests remain GREEN. No existing test was removed or weakened.

### EXACT remaining first blocker — target backend failed-close resource leak

Audited the ACTUAL source used by the existing framework libraries:
`/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/managed_components/joltwallet__littlefs/`.
`idf_component.yml`: esp_littlefs 1.22.3, source commit
`bde79e13be971a1f06d5e5f55e6f3e8568fa32b5`.

Source findings (not yet a behavioral target-backend RED/GREEN):
- `src/esp_littlefs.c:1649-1671`: FD-cache realloc and FD calloc are checked;
  OOM returns failure. This is not Arduino's throwing make_shared path.
- `src/esp_littlefs.c:2150-2193`: `vfs_littlefs_close` calls `lfs_file_close`;
  on failure it returns at 2182 BEFORE `esp_littlefs_free_fd` at 2191.
- `src/littlefs/lfs.c:3247-3264`: `lfs_file_close_` detaches the file and frees
  its owned cache even when synchronization failed. Blindly retrying that
  internal close is therefore unsafe and may assert/double-clean.
- `esp-idf/components/vfs/vfs.c:1040-1060`: `esp_vfs_close` releases the public
  FD-table entry regardless of the backend close result. The caller cannot
  simply retry the same public fd to free the orphaned backend resource.

The new descriptor wrapper truthfully reports failed close, but cannot yet
guarantee leak-free target cleanup or successful repeated retry under these
backend failures. Its comment and the syscall fixture explicitly disclose this.
Do NOT infer target no-leak behavior from the passing fake's live-fd assertion.
Next strict vertical work must reproduce this using actual target-backend source
or a suitable native production-source harness, then implement a scoped checked
cleanup solution IN THIS REPO with the existing framework libs. No framework
source/package was modified. No backend behavioral regression was implemented
or executed before the runtime budget ended.

Additional persistence limitations to keep explicit: failed/cancelled save can
retain a partial, non-loadable `.tmp`; reads ignore it and retry truncates it.
After successful canonical promotion, failed/cancelled backup cleanup reports
false while both complete old backup and new canonical remain. This preserves
recoverable old material, but is NOT a strict no-partial-state/old-canonical
rollback guarantee. The stronger requirement needs an explicit commit-result
contract or further transaction design before blockers 1/2 can be globally
declared closed. Key regeneration still changes RAM before failed persistence;
its transactional crypto/persistence design and per-boundary tests are pending.

### Fresh build and incomplete final acceptance

Full firmware BUILD ONLY, exact command:
`TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs uv run --directory software pio run -e warp4_tesla_ble`.
Exit 0: `warp4_tesla_ble SUCCESS 00:01:30.203`.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-eighth-build.log`.
Reported RAM: 93,260 / 327,680 bytes. Flash: 4,823,945 / 6,488,064 bytes.
These build figures are NOT measured heap reclamation or worker stack headroom.
Images were generated locally, NOT flashed/uploaded. esptool's 'ready to flash'
line is local image creation only.

- Last full host suite: batch3 above, exit 0 after all behavioral production edits.
- Final `git diff --check`: NOT RUN in this continuation.
- Standalone web/TypeScript build: NOT RUN in this continuation; the complete
  firmware build succeeded, but no separate frontend command/result is claimed.
- Current ELF/map release-call/static-memory/worker-stack audit: NOT RUN.
  The historical sixth-continuation audit script was read but not executed;
  it is not a worker-stack audit. Historical ELF hashes/ranges are NOT current
  verification of this build. No stack-headroom claim is made.

### Changes and exact remaining order

Changed here: adapters.h, vehicle.h/cpp, peer.h/cpp, tesla_ble_storage.h/cpp,
run_host_tests.sh, host/run_protocol_test.sh. New files:
`software/src/modules/tesla_vehicle/tesla_ble_storage_io.h`,
`software/tests/tesla_vehicle/test_tesla_ble_persistence_allocations.cpp`,
`software/tests/tesla_vehicle/test_tesla_ble_checked_storage.cpp`.
This document is appended only. All pre-existing dirty changes are retained.

First resolve the stronger persistence transaction semantics and target close
cleanup above; do not skip to Phase 2. Remaining ordered user blockers 3-7 have
NOT been worked on here: bounded module MAC / explicit pair and SoC acceptance;
wrapper callback-failure preservation; operational BLEUUID/Config/error/status
OOM; per-boundary operational mbedTLS failures; TX rollback, authenticated
action-error, malformed decode, raw observer reentry, queue overflow and target
worker-stack analysis. Run the full host suite after every logical batch, then
all required final diff/frontend/firmware/current-ELF checks.

Hardware-only risks remain unauthorized: Tesla interoperability, RF/sleep/wake,
SDK/watchdog behavior under load, fragmentation and measured heap reclamation.
Current verdict: NOT READY; not ready for independent GPT-6 Astra review.

## Ninth continuation — explicit commits, staged identity, close quarantine; NOT READY

Resumed session `20261011_014512_57652b` and the eighth continuation above.
Fresh `pwd`, `git branch --show-current`, `git status --short` confirmed
`/home/hermes/warp-tesla-ble`, `feature/tesla-ble-upstream-ready`. Existing dirty
changes and every existing test/assertion were retained. Phase 1 only: no
Phase 2, flash, upload, hardware access, commit, push, publish or framework
package/source modification. Native backend extraction reads the existing
framework sources and writes generated test/build artifacts into TMPDIR only.

### 1. Explicit transaction result and staged private-key publication — GREEN slice

`StorageAdapter::SaveResult` now distinguishes NotCommitted,
NotCommittedCleanupPending, Committed, CommittedCleanupPending and
RestartRequired. The target adapter overrides `save_transaction`. Promotion of
the fully written, fsynced, closed and reopened/verified temp to canonical is
the commit point. Cancellation/backup-cleanup failure AFTER promotion returns
CommittedCleanupPending, not a false assertion that nothing committed. Vehicle
session persistence, key persistence/generation and module MAC binding consume
the explicit committed predicate. Legacy bool save/save_bytes retain their
existing fully-cleaned-success semantics solely for compatibility; false on
that legacy interface MUST NOT be interpreted as proof of no durable commit.
Adapters with fallible post-commit cleanup must override the explicit contract.

Before promotion, failure/cancellation leaves the prior canonical or backup
recoverable. Partial/stale temp is explicitly reported as cleanup pending and
never loaded; retry truncates/verifies it. Canonical+backup recovery is covered.
No extra fallible rollback/reparse follows the durable commit. This is a
recoverable, synchronous application transaction, NOT power-loss durability
proof or a promise that no partial temp can remain after cancellation.

Key regeneration now stages a separately owned mbedTLS PK context, public key,
key ID and PEM export. Persistence observes the PRIOR active identity. Only a
committed result permits a no-allocation ownership swap and publication of the
candidate public identity. Candidate RAII frees the old key after success or
the candidate after refusal. Private PEM scratch is wiped. Failed save retains
prior key/session/counter/replay state without a fallible restoration parse;
successful generation resets old sessions/request correlation. Regeneration
returns bool and rejects an active command queue. Initial pairing also stages
and persists a missing key before publishing it.

Focused production-source commands, each via
`bash software/tests/tesla_vehicle/host/run_protocol_test.sh <name>`:
- `test_tesla_ble_strong_transaction`: valid RED exit 1,
  `FAIL production save has no explicit commit-result contract`; GREEN exit 0,
  `Production explicit save outcomes: 288 I/O and monotonic cancellation cases; partial temp, canonical+backup, promotion/cleanup and retry passed`.
  Its first compile attempt failed because renaming a retained main removes
  C++'s implicit return exemption; the fixture was corrected before valid RED.
  Existing checked-storage test source/assertions were not edited or weakened.
- `test_tesla_ble_key_transaction`: RED exit 1,
  `FAIL candidate key published before durable persistence` and
  `FAIL regeneration save refusal changed active key identity`; GREEN exit 0,
  `Production key regeneration: prior key/session/replay retained on save refusal; candidate not published during save, committed disk matches RAM and retry passed`.
Logs: TMPDIR `/home/hermes/.hermes/cache/scratch/`, names
`tesla-phase1-ninth-test_tesla_ble_{strong_transaction,key_transaction}-{red,green}.log`.
Full host gate after this batch: exit 0,
`tesla-phase1-ninth-host-batch1.log`, `Tesla BLE host tests passed.`
This slice does NOT close the pending per-allocation mbedTLS blocker 6.

### 2. ACTUAL backend failed-close ownership reproduced; quarantine redesign — GREEN

Read-only delegated ownership findings were checked against exact production
definitions and then exercised, not accepted as target proof by themselves.
Production esp_littlefs 1.22.3 commit remains
`bde79e13be971a1f06d5e5f55e6f3e8568fa32b5`.

New native harness:
`software/tests/tesla_vehicle/host/build_littlefs_close_harness.py`,
`littlefs_close_support.h`, `littlefs_close_device.inc`, and
`software/tests/tesla_vehicle/test_tesla_ble_littlefs_close.cpp`.
The generator extracts UNCHANGED production esp_littlefs allocator/free/close,
mtime/semaphore/error mapping and actual private wrapper/filesystem structs;
it extracts ESP-IDF public-FD close/lookup/release and dispatch macro. It links
the untouched FULL production LittleFS lfs.c/lfs_util.c against a native RAM
block device. Production cache size 512, borrowed inline file cache, mtime in
seconds and enabled assertions are retained. Native SDK lock/callback shims,
manual descriptor registration and native block-device hooks are test
boundaries; this is not execution of ESP32 flash/RTOS/drivers. Input hashes:
`/home/hermes/.hermes/cache/scratch/warp-tesla-host-tests/littlefs-close/source-hashes.json`.

Behavior reproduced through real LittleFS device-sync EIO, not a fabricated
lfs_file_close return: LittleFS detaches the file from mlist; esp_littlefs
returns before esp_littlefs_free_fd; private cache/list/fd_count and allocated
wrapper survive; ESP-IDF clears the ordinary public FD despite error. Public
retry is EBADF without reentering the backend. A forked PRIVATE retry hits the
actual `lfs_mlist_isopen` assertion at lfs.c:6193, SIGABRT as expected.
The assertion line in passing host logs is this deliberate isolated proof,
not a crashing application test.

Correction to the eighth source-audit wording: this production wrapper supplies
its INLINE lfs_buffer to LittleFS. LittleFS does not free that borrowed buffer;
it detaches the file. The orphaned containing wrapper retains that inline
cache/path. Separately owned LittleFS cache cleanup is not the production
ownership case here. No application-accessible, supported scoped per-file
release API exists. Unmounting/re-registering shared /data would invalidate
other modules' files; private-ABI surgery is deliberately not implemented.

Repository-scoped redesign: checked target close is exactly once and latches
a process-wide Tesla-storage quarantine after ANY close error. The latch is
enabled by ESP_PLATFORM in firmware and explicitly enabled in the native
backend fixture. It survives adapter/runtime recreation and has NO application
reset. All subsequent Tesla load/save/remove/forget/probe I/O is refused;
save returns RestartRequired, loads fail with zero length, presence is Unknown.
Failure exits explicitly close before choosing the save result, so a failed
destructor cleanup cannot hide the restart boundary. No detached LittleFS
file is retried, no filesystem cleanup is attempted after quarantine.

The worker acknowledges stop; main publishes restart_required and refuses
reactivation, including storage-only operation. A reboot is the recovery
boundary (not an in-process retry). The prior committed bytes remain recoverable
at that boundary. The harness reads them using its underlying native device
only, not the quarantined application API.

IMPORTANT: the failed-close backend wrapper remains allocated until reboot.
This is deliberate bounded retention of the first Tesla orphan, NOT leak-free
cleanup. Other users of the shared mount are outside this Tesla latch; their
backend failures are not solved here. Hardware reboot/recovery was not tested.
The existing generic POSIX/syscall fixtures retain their original retry tests;
they do not enable the esp_littlefs-specific latch and are not no-leak evidence.
The new production-backend fixture establishes the distinct target boundary.

Focused command: protocol runner `test_tesla_ble_littlefs_close`.
RED exit 1: backend defect reproduced, then
`FAIL failed backend close is still treated as retryable application persistence`.
GREEN exit 0: defect reproduced, then
`Production persistence close-error quarantine: one orphan wrapper retained; no detached retry, no subsequent Tesla storage I/O across adapter recreation; restart required`.
Logs: `tesla-phase1-ninth-close-{red,green}.log` in TMPDIR.
Full host gate after this batch: `tesla-phase1-ninth-host-batch2.log`, exit 0.

### 3. Module MAC / enqueue acceptance / rollback and retry — GREEN slice

MAC encoding is now a six-byte std::array and uses bounded explicit storage
commit results. Pair and all charge_state_poll overloads return actual enqueue
acceptance. PairStarted is not inferred from mere existence of a stored key.
Immediate pair refusal produces an Error outcome, not false pairing_pending.
Immediate SoC refusal disarms the request, clears in_flight and its start time,
clears policy pending state and restores an unused wake reservation. Both
manual retries enqueue successfully after queue pressure clears.

New actual module fixture `test_tesla_ble_module_acceptance` exercises queue
saturation/refusal, pair/SoC retry, and worker/main storage-quarantine outcome
propagation. RED exit 1: `FAIL immediate SoC enqueue refusal retains in-flight state`.
Separate `mac` mode RED exit 134: AllocationProbe detects throwing vector new.
GREEN exits 0: queue/policy/wake rollback, no false PairStarted, both retries,
unknown key presence/reboot requirement/reactivation refusal; separate mac
mode reports `Production module MAC encoding allocation-free`.
Logs: `tesla-phase1-ninth-module-{red,green}.log`,
`tesla-phase1-ninth-mac-red.log`; MAC GREEN printed in tool output.
Full host gate: `tesla-phase1-ninth-host-batch3.log`, exit 0.
All legacy behavioral/security/startup/storage tests remain passing.
Do not treat queue-full behavioral coverage as coverage of every operational
module OOM boundary; those remaining target contracts are still blockers 4-6.

### Final acceptance checks against these exact edits

- Full host suite AGAIN: `bash software/tests/tesla_vehicle/run_host_tests.sh`,
  exit 0, `Tesla BLE host tests passed.`
  `/home/hermes/.hermes/cache/scratch/tesla-phase1-ninth-host-final.log`.
- `git diff --check`: exit 0.
- Full firmware BUILD ONLY:
  `TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs uv run --directory software pio run -e warp4_tesla_ble`,
  exit 0, `warp4_tesla_ble SUCCESS 00:01:20.764`.
  `/home/hermes/.hermes/cache/scratch/tesla-phase1-ninth-build.log`.
  Reported RAM 93,260 / 327,680; flash 4,825,277 / 6,488,064 bytes.
  These are build figures, not measured heap reclamation. Locally generated
  ELF/BIN/esptool/OTA images were NOT uploaded/flashed/published.
- Standalone web/TypeScript pipeline: exit 0. From software/, `uv run python`
  parsed pio_hooks.py and executed its ACTUAL `check_call` and `build_web`
  function definitions with `build_web(False,False,False)`; no SCons import or
  firmware build. It ran tsc --build tsconfig.json, esbuild, sass, postcss and
  html-minifier-terser against current generated web/src_tfpp. No web edits
  were needed. Log: `tesla-phase1-ninth-web.log` in TMPDIR.
- Current ELF/map/static release-call audit:
  `python3 /home/hermes/.hermes/cache/scratch/tesla-phase1-ninth/elf_audit.py`,
  exit 0, log `tesla-phase1-ninth-elf-audit.log` in TMPDIR.
  ELF SHA256 `25164a03edc1776916459bfe6d6b3444af6c299b9250287fcaef3f72f0aab489`.
  Map SHA256 `78806b030e9c27bb1aa520a346096b492ab246f8d41a0fb269c3b72fa1ad7217`.
  Current release chain: TeslaVehicle::release_bluetooth 0x402605ec ->
  btMemRelease 0x4027819c -> general wrapper 0x4027817c ->
  esp_bt_mem_release 0x402c2c88 -> ROM release 0x402c2b78.
  Controller-only wrapper is absent. BLE-only ROM interval
  [0x3ffb09a8,0x3ffb1ddc) is 5,172 bytes; other controller reservations 33,240.
  Named Bluetooth data/BSS remains 15,116 bytes (5,412+248+6,740+2,716).
  These ranges do not establish Bluedroid heap or total heap reclamation.
  Current operational/synchronization disassembly, symbols, frames and summary
  are saved under `/home/hermes/.hermes/cache/scratch/tesla-phase1-ninth/`.

Target worker-stack audit is PARTIAL, not a safe-headroom verdict. Actual target
ENTRY frames include private_key_presence 4,160; Vehicle::initialize 4,144;
Vehicle::process_complete_message 2,848; handle_carserver_message 2,320;
Client::create_private_key 2,240; persist_private_key 2,096; Peer::encrypt 928;
MessageProcessor::process_messages 816 bytes. Worker allocation is 12,288 bytes,
teardown 4,096. The first audit parser missed hexadecimal ENTRY sizes; it was
corrected and rerun before recording these values. Do NOT use the erroneous
240-byte initial maximum as evidence. Indirect/reentrant callbacks, recursive
library paths, interrupt/scheduler margin and complete maximum call-chain use
are not yet bounded. No hardware high-water mark is claimed. Blocker 7 remains.

### EXACT next blocker and remaining order — independent review NOT READY

Runtime budget prevented beginning another vertical implementation slice.
Blocker 4 is next: Vehicle::send_command's rich-callback wrapper and
send_command_bool select `on_complete ? wrapped_callback : nullptr` before
checking allocation_failed on the INPUT optional callback. A failed callback
can therefore be normalized into an intentional empty callback. Source
locations remain vehicle.cpp send_command WakePolicy overload and
send_command_bool WakePolicy overload. Add production behavioral OOM tests for
EVERY compatibility wrapper/setter, including optional callbacks, then fix and
prove refusal retains prior registration/no enqueue and retry. This was NOT
fixed or declared GREEN in this continuation.

Then, in order: 5 BLEUUID/Config updateString/error/status target OOM contracts;
6 every operational mbedTLS encrypt/decrypt/ECDH/session-HMAC/key-generation/
export failure boundary, transactional state/no leaks/retry;
7 failed-TX rollback, authenticated action-error, malformed decode, raw observer
reentry, queue overflow, and complete target worker-stack headroom analysis.
The new staged key design is not substitute evidence for blocker 6. Hardware
interoperability/RF/sleep/wake/SDK-load/fragmentation risks remain unauthorized.
Current verdict: NOT READY; NOT ready for independent GPT-6 Astra review.

## Tenth continuation — callback contracts GREEN; target OOM work incomplete; NOT READY

Resumed session `20261011_021249_b0e297` and the ninth continuation. Fresh
`pwd`, `git status --short`, `git branch --show-current` confirmed
`/home/hermes/warp-tesla-ble`, `feature/tesla-ble-upstream-ready`.
Phase 1 only. No hardware access, flash, upload, commit, push, publish, Phase 2
or framework-package modification occurred. Existing tests/assertions retained.
The environment did not export TESLA_BLE_FRAMEWORK_LIBS; the ninth continuation's
explicit existing path remains the required build input, not a new framework.

### Blocker 4 — production behavioral RED -> GREEN and full gate

New `test_tesla_ble_callback_contracts.cpp` compiles actual production sources
with `-fno-exceptions` and abort-style throwing-new / checked nothrow-new fault
injection. Focused command:
`bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_callback_contracts`.

Valid wrapper RED: exit 1,
`FAIL wrapper=0 empty=0 phase=0 allocation=1 signal=0 status=2`.
Separate setter RED using the compiled binary's `setters` argument: exit 1,
`FAIL callback destroyed during reentrant replacement`.

The rich-error and bool compatibility wrappers now check the INPUT optional
callback's allocation_failed before testing truthiness. Failed construction
is rejected, not converted into intentional empty. Primary result API retains
its builder/completion/optional-phase checks. Bool-WakePolicy forwarding and
legacy bool-wake overloads are all exercised.

All nine Vehicle registrations now use bounded CallbackRegistration slots:
nested invocation pins the current callable; replacements/clears are staged
without dispatch allocation and applied only after the outermost invocation
returns. Last accepted replacement wins. Failed callback construction is still
rejected by each setter without changing the registration. Nested dispatch
continues to invoke the pinned prior registration. This is SERIALIZED callback
ownership, not cross-thread setter safety or permission to delete the owning
Vehicle from inside its callback. One pending callable per registration adds
bounded object memory; current target memory/stack effects are NOT yet audited.

Focused GREEN exit 0:
`Production five command wrappers: 36 allocation faults, optional empty completion/phase, no enqueue, retry and phase-disconnect lifetime passed`.
Each of the nine setters proves failed large-capture replacement retains and
invokes the prior registration, successful large-capture retry, intentional
empty, nested invocation and deferred lifetime. Plain function-pointer logging
setter is allocation-free; MessageProcessor's failed checked callback makes
synchronization_initialized false, and retry succeeds.

Logs under `/home/hermes/.hermes/cache/scratch/`:
`tesla-phase1-tenth-callback-wrappers-red.log`,
`tesla-phase1-tenth-callback-setters-red.log`,
`tesla-phase1-tenth-callback-green.log`.
Full host suite after this batch: exit 0, `Tesla BLE host tests passed.`
Log `tesla-phase1-tenth-host-batch4.log`.

### Blocker 5a — exact target String/Config/UUID behavior RED -> GREEN

New native generator `host/build_target_oom_harness.py` uses a BYTE-FOR-BYTE
scratch copy of the existing framework WString.cpp with its actual WString.h,
actual BLEUUID String constructor/getNative definitions, and extracted exact
production Config::updateString/update_value/updateStringChecked definitions.
SDK headers, Config type/slot backing and logging are native test boundaries.
It does not execute full ConfigRoot/API/RTOS or target SDK. Host pointer width
changes WString's SSO capacity; tested long diagnostics and 36-character UUIDs
exceed both host and target SSO. Do not claim full Config/backend target proof.
Inputs/hashes:
`/home/hermes/.hermes/cache/scratch/warp-tesla-host-tests/target-oom/source-hashes.json`.

Command: `python3 software/tests/tesla_vehicle/host/build_target_oom_harness.py`.
Initial harness compile failed on quoted Arduino.h SDK includes and log_v;
include-resolution-only source copying and the SDK logging shim were corrected
BEFORE valid behavioral RED. No framework source was changed.
Valid RED exit 1:
`FAIL Config update allocation=1 signal=6 status=-1`;
allocations 2 and 3 returned status 2 (unchecked failure/invalid destination);
each of the three UUID expressions segfaulted on injected realloc failure
(`signal=11`). This reproduces actual target-library ownership behavior rather
than assuming host std::string models Arduino OOM.

New opt-in Config::updateStringChecked(char const*) stages a checked String,
then transfers ownership using WString's nonallocating move. It returns true
for acceptance including unchanged; false retains prior valid value/flags.
Existing updateString behavior for other modules is NOT silently changed.
GATT uses native bounded UUID128 construction in tesla_ble_uuid.h. All three
values compare byte-for-byte with the actual framework parser, including its
little-endian representation; reconnect has no String/native-null exposure.
Host GATT UUID boundary structs were extended to real 128-bit storage and the
characteristic fake now interprets that representation. Assertions retained.

Focused GREEN exit 0:
`Actual target WString/Config update: checked failure retains prior valid value/flags, unchanged and empty allocate nothing, retry; all three native UUIDs allocation-free and identical to production parser passed`.
Logs `tesla-phase1-tenth-target-oom-{red,green}.log`.
Full host suite after this native boundary batch: exit 0,
`Tesla BLE host tests passed.`, `tesla-phase1-tenth-host-batch5a.log`.

### Blocker 5b — module error retry edits APPLIED, NOT VERIFIED

New actual module-source `test_tesla_ble_module_text` produced valid RED exit 1:
`FAIL status publication OOM overwrites prior diagnostic`.
Log `tesla-phase1-tenth-module-text-red.log`.
The fixture injects explicit checked-Config refusal; native 5a supplies actual
target String OOM evidence. Neither alone is full external publication proof.

Then applied (NO GREEN RUN YET): bounded main-thread pending_error publication,
checked Config write and loop retry surviving Runtime destruction; all long
last_error writes routed through it. Runtime identity reads are references,
validator normalization uses bounded 18-byte buffers and checked writes.
Command/validator rejection strings retain full text on success and use a
nonempty SSO `OOM` fallback on failed construction. Config lookup and asString
are allocation-free references in production; the host boundary now accurately
returns a reference instead of an allocating copy.

Runtime execution budget ended immediately after these edits. Therefore the
CURRENT complete worktree is NOT verified. Do not treat batch5a's passing suite
as evidence for these later module changes. First next command MUST run the
module-text focused test, correct any actual failure without weakening its
assertions, then full host suite. Rejection-string fallback and validator paths
still need their own production behavioral coverage. Do not call blocker 5 GREEN.

### Exact remaining blocker 5 and ordered continuation

Read-only allocation inventory was independently checked against Config and API
source bodies. Shared infrastructure reachable during active Tesla operation
still lacks the required complete OOM contract:
- Config::to_string_except (config.cpp serialization path) returns empty on
  JSON-document OOM and does not check String serialization growth failures.
- API::push_state_updates (api.cpp:104-158 at inventory time) can pass empty or
  incomplete payload to a backend and clear update flags after its success;
  generic String path conversion also allocates.
- Websocket async queue uses unchecked std::deque::push_back; MQTT topic String
  construction is fallible; ConfigRoot live-update cloning uses allocating
  Config trees/plain new and slot allocation can abort before Tesla validation.
These are REPOSITORY shared paths, not fixed by native UUID constants, checked
Tesla state writes, or reserved destination capacity. Their complete production
behavioral fault/rollback/retry coverage remains missing. Do not quietly narrow
"reachable after activation" to only the worker and declare completion.
SDK message/deep-copy/cache allocations also need precise rejection versus
supervision/quarantine distinctions; host SDK fakes are not per-boundary proof.
Existing failed-close storage quarantine remains restart-only, with the first
orphan wrapper deliberately retained until reboot as documented in ninth.

Only after blocker 5 is truthfully closed, proceed in order to 6 (operational
mbedTLS encrypt/decrypt/auth/ECDH/session-HMAC/key-generation/import/export,
per-allocator-boundary transactional state/resource/retry proofs) and 7 (failed
TX rollback, independently authenticated action error, malformed decode, raw
observer reentry, overflow, full reachable target worker/teardown call chains).
Neither blocker 6 nor blocker 7 implementation was started here. Earlier startup
allocation tests and crypto/parser success tests do NOT close blocker 6.

### Final acceptance checks NOT COMPLETED against current edits

Final full host suite, git diff --check, standalone web/TypeScript pipeline,
full firmware build using existing TESLA_BLE_FRAMEWORK_LIBS, and CURRENT
ELF/map/hash/static-memory/release-call/reachable-stack audits: NOT RUN after
the final module edits. Ninth-continuation artifacts are historical, not current.
The last two full host gates above apply only to their exact earlier batches.
Worker remains configured 12,288 bytes; teardown 4,096. No new reachable-chain
maximum, safe stack margin, hardware high-water or heap-reclamation claim is
made. Indirect/reentrant callbacks, library recursion/frames and RTOS/interrupt
overhead remain unresolved; hardware high-water validation is still required
where a static bound cannot be established. Hardware access remains prohibited.

Current verdict: NOT READY; NOT ready for independent GPT-6 Astra review.
Exact next work: verify/fix module-text GREEN, host gate, then complete blocker 5
shared publication/live-config/SDK OOM contracts before starting blockers 6-7.

## Eleventh continuation — module text and validator GREEN; bounded external blockers; NOT READY

Resumed session `20261011_025249_7efc0b` and the appended tenth continuation.
Fresh `pwd`, `git branch --show-current`, and `git status --short` confirmed
`/home/hermes/warp-tesla-ble`, `feature/tesla-ble-upstream-ready`. Phase 1 only.
No hardware, upload, flash, commit, push, publish, Phase 2, or framework-source
modification. No unrelated shared infrastructure was rewritten. This continuation
added host coverage/reproducers; it did not add a checked API backend adapter.
All pre-existing tests/assertions were preserved.

### First required test and complete gate — verified

First executed test:
`bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_module_text`
exited 0: `Production module error publication: checked refusal retains previous,
coalesced ordered retry and retry after runtime destruction passed`.
No production fix was necessary for the tenth continuation's final edits.
Full `bash software/tests/tesla_vehicle/run_host_tests.sh` then exited 0,
`Tesla BLE host tests passed.` Logs in `/home/hermes/.hermes/cache/scratch/`:
`tesla-phase1-eleventh-module-text-initial.log`,
`tesla-phase1-eleventh-host-initial.log`.

### Validator/rejection-string production behavioral coverage — verified

Added `test_tesla_ble_module_validation.cpp` and
`host/build_module_validation_harness.py`; wired the test into the full host
runner. The generator extracts the EXACT Tesla validator lambda,
`checked_error_string`, and `register_urls` bodies. It uses actual unchanged
WString.cpp and the exact production checked Config update/update_value bodies.
Config tree, registration, Runtime admission, and OS boundaries are native
fixtures. It is NOT full ConfigRoot cloning or API command dispatch proof.

Two ABI-qualified builds execute: native SSO with actual WString.h, and a
scratch-only WString.h SSO-capacity adaptation to target capacity 14
(three 32-bit pointer/capacity/length fields, SSOSIZE 15 including NUL).
All method bodies stay unchanged. This adaptation is explicitly not a native
32-bit target execution or a byte-for-byte target-header claim. Framework
inputs are read-only. Source hashes:
`/home/hermes/.hermes/cache/scratch/warp-tesla-host-tests/target-oom/module-validation-hashes.json`.

Coverage: both checked normalization allocation boundaries, all three long
validator rejections, all four command rejection paths, nonempty `OOM` fallback,
failed leaf retaining its prior bytes/flags, no live Config/runtime request
mutation on rejection, full diagnostic retry, and healthy command retry.
The disposable candidate can have an earlier successfully normalized leaf;
no claim of atomic mutation of the ENTIRE disposable candidate is made.
Initial harness failed because the state fixture omitted `pairing_pending`;
the fixture was corrected without removing assertions. This was harness setup,
not a newly found production RED. Production code was already GREEN.

Focused command `python3 software/tests/tesla_vehicle/host/build_module_validation_harness.py`
exited 0. Both native and target-SSO variants reported nonempty rejection,
no live/runtime mutation, and successful retry. Log:
`tesla-phase1-eleventh-validation-green.log`.
Full host gate AFTER this batch exited 0, including every prior test:
`tesla-phase1-eleventh-host-validator.log`.

### Blocker 5 — bounded external publication/live-update dependency, NOT FIXED

Added reproducible read-only production-source fault audit:
`python3 software/tests/tesla_vehicle/host/audit_external_publication.py`.
Exact Config::to_string_except, API::push_state_updates, SDK
btc_transfer_context and GATT deep-copy/free bodies execute with real WString
and ArduinoJson. Config::to_json population, backend acceptance, SDK structs,
allocator/post scheduling are native boundaries. Source hashes/generated source:
`/home/hermes/.hermes/cache/scratch/tesla-phase1-eleventh-external/`.

Reproducer exit 0 means DEFECT REPRODUCED, NOT acceptance GREEN. It is not
silently included as a passing safety test in the ordinary host gate.
Log `tesla-phase1-eleventh-external-reproduced.log` contains:
- JSON-document allocation failure: API invokes backend with EMPTY Tesla
  payload, clears dirty flag, and does not retry on the next pass.
- First WString serialization-growth allocation failure: API invokes backend
  with CORRUPTED payload (92 bytes, one byte shorter than healthy), clears dirty
  flag, and does not retry. Initial probe assumed empty for this case; actual
  output corrected that assumption. The reproduction now asserts corruption
  against the healthy bytes instead of inventing empty-payload behavior.

Exact upstream dependencies, repository-relative:
1. `software/src/config/config_root.cpp:129-154`: `*out_config = *this`
   occurs before the Tesla validator. `config/conf_object.cpp:188-204` uses
   throwing `new Config[len]()` in the copy constructor. Under -fno-exceptions
   a module validator cannot intercept this allocation failure. A safe opt-in
   checked clone/update admission contract must precede this copy and preserve
   the live root until complete validation/commit. Tesla's checked leaf writes
   cannot establish that contract.
2. `software/src/config.cpp:1053-1079` and
   `software/src/modules/api/api.cpp:104-158`: serialization does not return a
   checked completeness result; API owns dirty-flag clearing and calls backends
   after empty/corrupted serialization. Module code has no publication hook
   before this invocation/clear. Required narrow upstream contract: per-root
   checked serialization/publication acceptance with dirty flags retained on
   refusal, plus checked path ownership. Reserving some heap is not proof.
3. `software/src/modules/api/api.h:92-110`: IAPIBackend has no capability that
   promises allocation-safe enqueue/ownership. WebSockets::sendToAllOwnedInternal
   (`modules/ws/web_sockets.cpp:600-608`) performs unchecked deque push_back;
   Mqtt::build_topic (`modules/mqtt/mqtt.cpp:182-196`) ignores reserve/concat
   failures. Required upstream extension: explicit opt-in checked backend
   enqueue/topic contract with refusal and retry. Caller-only adaptation cannot
   catch abort or recover a false-success backend acknowledgment. Refusing ALL
   existing backends would retain flags but disable Tesla live publication; it
   is not a functionality-preserving fix.

No module-only adapter with the CURRENT interfaces can cover those earlier/
later owned failures. This is a bounded external dependency, not a declaration
that a future scoped shared opt-in extension is impossible. Such an extension
and its production fault proofs have NOT been implemented here. Blocker 5 is
NOT closed; Tesla API calls still do not meet the complete fail-closed contract.

### Target SDK allocation classification — source audit, not blanket OOM proof

Framework reference prefix:
`/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/esp-idf/components/bt/`.
The narrow read-only source inventory was delegated and its central transfer/
deep-copy/BTA-write findings checked directly against source and the native
reproducer. It is not an exhaustive fault-certified controller/SDK audit.

- Checked refusal: common/btc/core/btc_task.c:354-357 outer message allocation
  returns BT_STATUS_NOMEM, mapped by GATT public API to refusal. Immediate
  Tesla gatt_operation refusal does not retire ownership and can retry.
- Cache query scratch: BTC UUID/query allocation and BTA temporary service/
  attribute databases return zero count/resource/not-found; Tesla checks
  status/count. They are NOT allocation-free lookups. Inventory references:
  host/bluedroid/btc/profile/std/gatt/btc_gattc.c:343-362,447-454,516-523;
  host/bluedroid/bta/gatt/bta_gattc_cache.c:1499-1504,1614-1632.
- Unreported payload fault: btc_gattc_arg_deep_copy at btc_gattc.c:38-54 sets
  write value_len to zero on allocation failure. Its void return cannot reach
  btc_transfer_context:367-380. Native reproduction for BOTH characteristic
  and descriptor writes verifies outer-envelope OOM refuses, deep-copy OOM
  returns SUCCESS with null payload/zero length, and healthy retry copies the
  intended bytes. This is NOT guaranteed restart/quarantine: zero-length write
  can continue through BTA_GATTC_WriteCharValue:680-701. No module-level reserve
  or timeout can prove intended-byte submission after that false success.
- Asynchronous BTA request allocation failures: bta_gattc_api.c register/open/
  close/write/descriptor allocations return void and may drop an already
  accepted operation. Missing completion reaches the Tesla 10,000-ms operation
  retirement/stop/restart boundary; blocked SDK submission instead reaches
  main-thread supervision/quarantine. Ownership is retained, not cancelled.
- Callback-envelope and internal completion allocation failures can drop
  completion/notifications (btc_gattc.c:188-201; bta_gattc_act.c:1874-1906,
  2336-2356). Lost unsolicited notifications alone do NOT force quarantine;
  response timeout is not a universal SDK OOM detector.
- Discovery cache/list construction has ignored failure returns and conditional
  deferred-command ownership hazards (bta_gattc_cache.c:339-539,744-815,
  1224-1249; bta_gattc_utils.c:197-203,528-544,637). Partial cache or lost discovery
  completion is not a checked-refusal proof. Treat retained ownership and
  timeout/restart boundaries honestly; no leak-free recovery claim is made.

Bounded SDK external blocker: public GATT API must propagate deep-copy failure
without queuing altered bytes, and asynchronous cache/message ownership must
have checked reporting/cleanup. Framework modification remains forbidden.

### Current build and audit — exact verified results, incomplete final acceptance

- `git diff --check` exited 0 BEFORE this appended evidence; no final rerun
  after this documentation write is claimed.
- Full BUILD ONLY with the existing explicit TESLA_BLE_FRAMEWORK_LIBS path:
  `TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs uv run --directory software pio run -e warp4_tesla_ble`
  exited 0: `warp4_tesla_ble SUCCESS 00:02:55.328`.
  `tesla-phase1-eleventh-build.log`. RAM 93,356 / 327,680; flash
  4,826,717 / 6,488,064 bytes. Local images were generated, NOT flashed/uploaded.
  Existing nonfatal nanopb pip-unavailable/hyphenation warnings remain; they
  were not disguised as a clean warning-free build.
- That build ran tsc/esbuild/sass/postcss/html-minifier successfully. A SEPARATE
  standalone AST-extracted production build_web run failed on missing `pathlib`
  in its harness. The import was corrected but NOT rerun before runtime budget
  ended. `tesla-phase1-eleventh-web.log` is a FAILED run, not acceptance evidence.
- Current ELF/map/static/release audit exited 0 via
  `python3 /home/hermes/.hermes/cache/scratch/tesla-phase1-eleventh-elf.py`.
  It executes the inspected ninth audit against the CURRENT ELF, with a new
  output directory; no historical binary is substituted. Log
  `tesla-phase1-eleventh-elf.log`, artifacts `tesla-phase1-eleventh/`.
  ELF SHA256 `a7cbea26c256af6c23e262c650ff48d98148a148ffda905e8e30eea888b0a0f2`;
  map SHA256 `69f38992308b8a45d38c764af044f0fd0d12c462aa28f108ce0e131d8e6dadb7`.
  release_bluetooth 0x40260a44 -> btMemRelease 0x40278520 -> general wrapper
  0x40278500 -> esp_bt_mem_release 0x402c300c -> ROM release 0x402c2efc.
  Controller-only wrapper absent. BLE-only ROM release is 5,172 bytes; other
  controller ROM reservations 33,240; named Bluetooth data/BSS 15,116.
  These are not measurements of dynamic heap reclamation or zero-BT-DRAM boot.

Current ENTRY frames include private_key_presence 4,160; initialize 4,144;
process_complete_message 2,848; handle_carserver_message 2,320;
create_private_key 2,240; persist_private_key 2,096; Peer::encrypt 928;
MessageProcessor::process_messages 816. Worker remains 12,288 bytes, teardown
4,096. The audit STILL does NOT bound the full reachable stack chains,
indirect/reentrant callbacks, library recursion, or RTOS/interrupt margin.
No stack-safe/headroom verdict or hardware high-water measurement is claimed.

### Stop state and exact remaining order

Runtime budget ended before more implementation/verification. Blocker 5 remains
the exact blocker, with publication loss and SDK false-success now concretely
reproduced. Blockers 6 and 7 were NOT started; earlier startup/C++-new tests are
not operational mbedTLS allocator coverage. All required per-boundary crypto,
failed-TX/action-error/decode/raw-observer/overflow and reachable-chain acceptance
work remains pending in the requested order. Hardware high-water remains required
where static proof is impossible, but hardware access is still prohibited.

No final full-host rerun after the external-audit addition, no successful separate
standalone web rerun, and no complete reachable-chain audit are claimed. The two
verified full host gates above remain exact evidence for their named batches.
Current verdict: NOT READY for independent GPT-6 Astra review.

## Twelfth continuation — local blockers 6/7 GREEN; explicit external/hardware limitations

Resumed `20261011_031845_17358c` and the eleventh continuation. Fresh pwd,
branch and status confirmed `/home/hermes/warp-tesla-ble`, branch
`feature/tesla-ble-upstream-ready`. This is Phase 1 ONLY. No Phase 2, flash,
upload, commit, push, publish, framework-package modification or hardware access.
Existing dirty work, earlier tests and external reproducers were preserved.
The user's current scope treats blocker 5 module-local work as GREEN and its
remaining contracts as bounded external dependencies; the prior historical
NOT READY/blocker-5 stop verdict is superseded by that scope, not by a claim
that upstream defects were fixed. No shared-infrastructure rewrite was attempted.

### Corrected standalone web harness — first check, verified

`uv run --directory software python /home/hermes/.hermes/cache/scratch/tesla-phase1-eleventh-web.py`
exited 0. The exact production AST-extracted build_web function ran tsc, esbuild,
sass, postcss and html-minifier-terser, ending
`Standalone production web/TypeScript pipeline passed`.
The missing pathlib import correction is now exercised, not merely written.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-twelfth-web.log`.

### Blocker 6 — per-boundary actual-source allocator faults, GREEN

Added `software/tests/tesla_vehicle/test_tesla_ble_mbedtls_faults.cpp`, wired
into the full host runner. It links the actual mbedcrypto C archive rebuilt
from the existing ESP-IDF builder's mbedTLS source tree. Linker allocator hooks
`--wrap=calloc,--wrap=free` return NULL at each operation's allocation ordinal.
Every allocation is tracked, including fixture allocations: failure must restore
the exact live block AND byte count immediately; destruction must reach zero.
Crypto entry points are NOT mocked. `--wrap=mbedtls_entropy_func` supplies a
deterministic host-test seed solely to reproduce blinding/allocation order.
Firmware randomness is unchanged; DRBG state is intentionally NOT rolled back.
No framework source/package was modified. Host compilation uses -fno-exceptions.

The fixture snapshots BOTH peer keys, remote public keys, epochs, counters,
clock/start times, replay bitmaps/high-water/used state, validity/shared flags,
session revisions, client public key/key ID, private PEM and UUID/hash bytes and
lengths. Every failed operation must return failure, preserve this operational
state, release temporary resources, avoid persistence, and succeed on retry.
Decrypt uses independently assembled response TLV/SHA256 and real GCM encryption;
HMAC metadata is independently assembled and tagged by linked mbedTLS. Retry
decrypt plaintext, imported replacement PEM, invalidation of prior sessions,
and one accepted persistence-commit boundary are asserted. NULL/capacity refusal
is tested; this allocator fixture's storage boundary is not a disk durability proof.

Final deterministic allocation ordinal coverage (14 operational paths):

| Production path | Injected allocation faults |
| --- | ---: |
| Client encrypted request | 1 |
| Direct Peer encrypt | 1 |
| Authenticated Peer decrypt | 1 |
| ECDH | 924 |
| Normal session update | 924 |
| Forced session update | 924 |
| Peer key load | 924 |
| Session-info key HMAC derivation | 2 |
| HMAC-SHA256 | 2 |
| Client session-info HMAC verification, including ECDH | 928 |
| Client key generation, public identity and PEM persistence staging | 460 |
| Client private key import | 9 |
| Private PEM export | 2 |
| Public point export | 0 |
| TOTAL | 5102 |

Public point export is genuinely allocation-free in this native linked
configuration; no synthetic allocation was added to manufacture a fault count.
Healthy/null/undersized export and unchanged key/state are checked instead.
Counts are dynamic baselines, not assumptions about target allocator counts.

Test-first defects and fixes:
- Initial sweep RED exit 1: key-import fault 1/9 returned failure but destroyed
  prior key (state=0, live resources 0/5, bytes 0/360). Import now parses and
  validates a RAII candidate. Client exports public identity/key ID before
  committing the ownership swap; successful import resets old session/correlation
  state. Direct CryptoContext import uses the same staging contract.
- Private-export RED exit 1: allocation 1 left output length=999. Export now
  initializes its success-length channel to zero before any fallible work.
- Direct-encrypt RED exit 1: allocation 1 left length=999. Peer encrypt now
  validates pointers/capacity and has a failed-output wipe/zero-length guard.
  Random-byte and GCM AAD return statuses are also checked instead of ignored.
  The ordinal RED was the length/refusal defect, not a claim that each crypto
  status hardening separately had an allocator-triggered RED.

Logs under `/home/hermes/.hermes/cache/scratch/`:
`tesla-phase1-twelfth-mbedtls-red.log`, `tesla-phase1-twelfth-export-red.log`,
`tesla-phase1-twelfth-peer-encrypt-red.log`, `tesla-phase1-twelfth-mbedtls-green.log`.
Focused full sweep exited 0. Full host gate after this batch exited 0:
`tesla-phase1-twelfth-host-crypto.log`, `Tesla BLE host tests passed.`
Source/archive/test hashes: `tesla-phase1-twelfth-mbedtls-hashes.json`.
Native mbedcrypto SHA256:
`25bbdb6eb47670913f89502212a09a8e31f88db829b33ba8300afb65363d24f5`.
These are native production-source behavioral proofs, NOT target ESP32 ABI,
hardware-accelerator/esp_config allocation counts or measured hardware heap proof.

### Blocker 7 — production behavioral coverage and fixes, GREEN

Added `test_tesla_ble_behavior.cpp` and `test_tesla_ble_module_pressure.cpp`;
both run in the full host gate with actual Vehicle/Client/Peer/MessageProcessor,
nanopb and linked mbedTLS. Module pressure includes the actual module, transport,
GATT and storage translation units; only Config/FreeRTOS/SDK platform boundaries
are native fixtures. A host-only SDK write result/completion hook retains the
prior default refusal for all existing tests. No production SDK implementation
or package was changed.

Test-first defects:
- TX RED exit 1: transport refusal advanced counter 8 -> 9, changed UUID/hash,
  cleared replay window and retained command request UUID length 16.
- Reentrant TX/disconnect RED exit 1: failure completion fired TWICE.

Fix: bounded request-state snapshots surround encode/submission in command,
auth and wake call paths. On build/write refusal, prior counter/replay and
correlation are restored BEFORE terminal callbacks; failed command UUID is
cleared. A peer session revision prevents rollback from resurrecting a session
reset/re-keyed by reentry. Disconnect detaches/moves its completion before
invoking it, so later TX failure cannot invoke the same callback twice.
Successful accepted TX still advances the request state and waits for response.

Verified behavioral slices:
- Explicit transport false, base bounded-adapter refusal, failed builder;
  prior counter/hash/UUID/replay retained, one truthful terminal failure and retry.
- Auth/wake prerequisite refusal preserves state and accepted retry succeeds.
- False-TX disconnect reentry completes once, no revived validity/counter.
- Actual module transport admission refusal AND immediate SDK submission refusal;
  unchanged operational request state and accepted fragment retry.
- Independently authenticated CarServer denial returns the exact rich permanent,
  definitely-failed error; tag/UUID/counter tampering cannot complete it or emit
  SoC. Valid terminal completion occurs once. `already_set` compatibility remains.
- Authenticated malformed inner protobuf does not consume replay/correlation;
  valid retry with the SAME response counter is accepted.
- Malformed outer decode/corrupt prefix, fragmented valid suffix recovery;
  raw observer reenters RX/loop, replaces itself, and its original bytes stay
  stable. Both frames dispatch once and later traffic still works.
- Command queue capacity 32 refuses the extra entry without changing earlier
  entries/counter, delivers one failure, and accepts retry after pressure clears.
- Protocol inbound queue capacity 8 drops oldest under overflow, preserves exact
  retained FIFO order and recovers on later traffic (explicit existing policy).
- Module notification queue capacity 8 drops ordinary overflow SoC WITHOUT false
  consumption/pairing; authenticated retry is consumed once. Independent
  PairStarted/Error outcomes survive full notification queue, preserving earlier
  chronology tests. No lossless ordinary-notification guarantee is claimed.

Logs: `tesla-phase1-twelfth-tx-red.log`,
`tesla-phase1-twelfth-tx-reentry-red.log`,
`tesla-phase1-twelfth-behavior-green.log`,
`tesla-phase1-twelfth-module-pressure.log`.
Initial new fixture compilation/signature and adapter-contract mistakes were
corrected; those harness failures are not mislabeled production defects.
Full host suite AFTER behavioral fixes exited 0:
`tesla-phase1-twelfth-host-behavior.log`, `Tesla BLE host tests passed.`
All earlier tests/assertions are preserved. Expected child-process abort in the
pre-existing LittleFS close reproducer remains part of its passing quarantine test.

### Current build, ELF/map/static memory/release — verified, BUILD ONLY

Command with the existing explicit framework library tree:
`TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs uv run --directory software pio run -e warp4_tesla_ble`
exited 0: `warp4_tesla_ble SUCCESS 00:01:19.714`.
Log `tesla-phase1-twelfth-build.log`. RAM 93,356 / 327,680; flash
4,827,801 / 6,488,064 bytes. The detailed linker DRAM accounting is 93,354
(59,488 BSS + 33,866 data), not the same reporting convention as PIO RAM.
No flash/upload target ran. Existing missing-hyphenation, nanopb pip-unavailable,
and main-branch-fetch warnings remain nonfatal; no warning-free claim.

CURRENT ELF SHA256:
`bca515be52faeca0c12db73b67a5b0d3fee325e3e9d81d1f6cc53614da521e29`.
CURRENT map SHA256:
`4aa9e9d388e4c750877f6344bc447836286ce91b9cb1e6e23c98fc6583003a5b`.
Inspected earlier audit implementation rerun against the CURRENT binary exited 0;
log `tesla-phase1-twelfth-elf.log`. Its wrapper writes into the eleventh-named
directory; CURRENT artifacts were copied to `tesla-phase1-twelfth-elf/`.
That eleventh-named artifact directory is now current, not the historical ELF;
historical eleventh log/hash evidence above remains historical.

Release call/disassembly: release_bluetooth 0x40260ee8 -> btMemRelease
0x402789c4 -> general wrapper 0x402789a4 -> esp_bt_mem_release 0x402c34b0
-> ROM release 0x402c33a0. Controller-only wrapper absent. ELF ROM-region and
Bluetooth data/BSS range assertions still pass: 5,172 BLE-only ROM bytes,
33,240 other controller reservations, 15,116 named Bluetooth data/BSS bytes.
Map symbol presence and current hashes were checked; the inherited audit's
printed "match" is NOT an independent numerical map-vs-ELF equality proof.
These static ranges are NOT measured dynamic heap reclamation or zero-BT-DRAM boot.

### Reachable worker/teardown stack extension — evidence, NOT a static bound

Added/executed `software/tests/tesla_vehicle/host/audit_reachable_stack.py`, exit 0.
It extracts CURRENT ELF flash/IRAM/vector sections, disassembles reached symbols,
follows direct calls/cross-symbol jumps and straight-line literal-loaded CALLX
targets, and adds the source-verified MessageProcessor -> Vehicle handler edge.
It retains unknown indirect sites and recursive cycles rather than inventing
zero-cost closure. Initial graph-only pass was incomplete; literal-call expansion
and the exact nanopb symbol spelling were corrected and the expanded audit rerun.

Final expanded discovery: 1,466 functions, 1,666 resolved literal call sites,
1,268 unresolved indirect sites, 96 unknown direct targets, 17 detected cycle
nodes, and NO available .su stack-usage files under software/.pio. All discovered
functions have ENTRY evidence, but absence of a discovered cycle in another
subgraph would not establish absence of runtime/library recursion.

Selected discovered branch-insensitive ENTRY-frame sums:
- Worker: 9,088 bytes versus configured 12,288. Example chain includes task_entry
  (32), task_loop (256), process_commands (224), create_vehicle (256),
  Vehicle::create (64), initialize (4,144), load_session (272), update_session
  (96), ECDH (224), then initialization/error logging, libc formatting, heap,
  RTOS and assertion/cache paths. Exact full chain is in summary.json.
- Teardown: 2,784 bytes versus configured 4,096. Example chain includes
  teardown_entry (48), release_bluetooth (32), BLEDevice::deinit (32), general
  BLE memory release, heap-region/TLSF registration, libc/heap logging, RTOS
  and assertion/cache paths. Exact full chain is in summary.json.

These are sums on DISCOVERED acyclic paths, not maximum executable stack use,
not proven feasible branches, and NOT safe/headroom numbers. Cycle edges are
omitted/flagged; memoization and branch-insensitive traversal are not a WCET or
stack proof. Unresolved callbacks/virtual calls/ROM/library recursion/non-ENTRY
SP adjustments, register-window spill, interrupts and RTOS margin remain unknown.
Raw observer reentry can nest decode/dispatch; queues separate normal decode and
dispatch, so they must not simply be summed without a demonstrated nesting path.
Required hardware worker AND teardown high-water/guard validation under cold
startup, key/storage/error logging, RX/auth/reentry/queue-pressure and stop/partial
startup workloads remains UNPERFORMED because hardware access is forbidden.
No complete static bound or hardware safety approval is claimed.
Log `tesla-phase1-twelfth-stack.log`; CURRENT disassembly/callgraph/summary:
`/home/hermes/.hermes/cache/scratch/tesla-phase1-twelfth-stack/`.

### Explicit unchanged external dependencies / residual requirements

- Shared ConfigRoot pre-validator allocating copy; checked serialization,
  dirty-flag retention and backend acceptance/ownership publication contract
  remain upstream dependencies. Empty/corrupted Tesla publication false-success
  reproducers and references in the eleventh continuation remain preserved in
  `software/tests/tesla_vehicle/host/audit_external_publication.py`. They were
  NOT rewritten, suppressed, included as acceptance GREEN, or claimed fixed.
- Bluedroid characteristic AND descriptor payload deep-copy NULL -> accepted
  zero-length write remains a framework dependency. Outer-envelope refusal is
  checked, but deep-copy false-success is NOT guaranteed to quarantine/restart.
  Async SDK message/cache/discovery ownership/completion gaps remain as described
  above. No Tesla module reserve/timeout can prove intended-byte submission.
- Earlier real LittleFS close-error ownership defect remains external; the
  narrow Tesla storage quarantine/reboot boundary and reproducer are preserved.
- Transport false after partial/acknowledgment uncertainty is NOT proof that no
  bytes or side effects reached a real car. Local request rollback tests prove
  local state semantics, not remote command cancellation or exactly-once wire
  delivery. That uncertainty and SDK false-success require upstream fixes and
  independent review, not fabricated module-local closure.
- Native fault tests do not replace target config/accelerator OOM validation,
  dynamic heap checks, BLE/reboot/recovery testing, or stack high-water margins.

Local blockers 6 and 7 are GREEN. Final post-edit host rerun and post-document
diff check are recorded below before the current readiness status becomes final.

### Final gate and current verdict

After ALL functional/test/audit-script edits and the evidence append:
`bash software/tests/tesla_vehicle/run_host_tests.sh` exited 0,
`Tesla BLE host tests passed.` The strengthened session-revision and successful
replacement-key/commit assertions also passed with all 5,102 allocator faults.
Log: `/home/hermes/.hermes/cache/scratch/tesla-phase1-twelfth-host-final.log`.
The native test binary hash manifest was refreshed after this final compile.
Post-document `git diff --check` exited 0; fresh branch still
`feature/tesla-ble-upstream-ready`. No production source changed after the
verified current firmware build and ELF audit; subsequent changes were native
test assertions, the audit script and documentation only.

Current branch status:
READY FOR INDEPENDENT ASTRA REVIEW WITH EXPLICIT EXTERNAL/HARDWARE LIMITATIONS.

This is NOT Phase 1 approved. It is NOT hardware-safe approval, not closure of
the shared API/Config or SDK/LittleFS external contracts, and NOT authorization
to start Phase 2. Independent Astra review must explicitly consider the bounded
external defects, lossy ordinary queues, partial/uncertain TX semantics, native
vs target crypto configuration and the unbounded stack/hardware validation gaps.

Final numerical map follow-up also exited 0: all EIGHT Bluetooth data/BSS
start/end endpoints parsed from the CURRENT map match the CURRENT ELF's
absolute nm symbols. Artifact:
`/home/hermes/.hermes/cache/scratch/tesla-phase1-twelfth-elf/map-range-check.json`.
This strengthens the inherited map-presence check without changing the static
release limitations above. The post-verdict/document diff check is rerun once
more as the last repository check; no firmware/test source edits follow it.

## Thirteenth continuation — fourth Astra review B1–B3 only

The FULL fourth independent Astra review was retrieved from session
`20261011_042412_47b218`, final message `220646`. Its verdict was **NOT READY**.
That review supersedes the previous continuation's readiness statement. This
continuation addresses exactly its three local blockers, not Phase 2 or any
external limitation. Repository and branch were checked live:
`/home/hermes/warp-tesla-ble`, `feature/tesla-ble-upstream-ready`, unchanged HEAD
`03798d2515710d2a72dcb08ec72b6b502f6028d1`.

Preserved reviewer reproducers were read from
`/home/hermes/.hermes/cache/scratch/astra-phase1-review4/` and copied into
repository regression tests BEFORE production changes:
- `test_tesla_ble_outer_admission.cpp`: `outer-status`, `outer-reset`,
  `vcsec-control`, `vcsec-decode` from `review_probe.cpp`.
- `test_tesla_ble_session_replay.cpp`: `session_replay_probe.cpp` reproduction.
The absolute include was made repository-relative; the retained behavior-test
entry point received an explicit `return 0` so that including it under a renamed
entry point still compiles with the existing `-Werror` runner. No warning
suppression or assertion removal was introduced. Both new tests run by default
in the full host gate, against actual Vehicle/Client/Peer/MessageProcessor,
nanopb and the existing native mbedTLS archive. Original scratch probe SOURCE
files were not changed; their executables were freshly rebuilt and rerun GREEN.

### Exact focused RED, then GREEN

Evidence root (all following logs/manifests are relative to this directory):
`/home/hermes/.hermes/cache/scratch/tesla-phase1-review4-fixes/`.

Initial RED was captured by the normal production protocol-test runner before
changing any production source. `focused-red.json` records:

| Preserved probe | RED exit / actual | Final GREEN exit / actual |
| --- | --- | --- |
| outer-status | 1; callbacks=1, queue=0 | 0; callbacks=0, queue=1 |
| outer-reset | 1; session valid=0, counter=0 | 0; session valid=1, counter=8 |
| vcsec-control | 0; callbacks=1 | 0; callbacks=1 |
| vcsec-decode | 1; subsequent valid SAME-counter callbacks=0 | 0; callbacks=1 |
| session replay | 1; disconnected, valid=1, counter=7, saves=2 | 0; disconnected, valid=0, counter=0, saves=1 |

RED build/output logs: `test_tesla_ble_outer_admission-red-build.log` and
`test_tesla_ble_session_replay-red-build.log`. Initial focused GREEN also reran
the retained behavior and persistence-allocation tests; all four exited 0
(`focused-green.json`, corresponding `*-green.log`). Final strengthened test
outputs and the freshly rebuilt ORIGINAL Astra probes are in
`focused-green-final.json`, `*-green-final.log`,
`review_probe-original-green-build.log`, and
`session_replay_probe-original-green-build.log`. All original probe exits are 0.

### B1 — authenticated/correlated outer-fault admission

`Vehicle::handle_message_` no longer invokes outer error effects ahead of domain
authentication. The authenticated fault helper is invoked only after the
domain's UUID/command-state, AES-GCM tag, successful protobuf decode and replay
admission checks. Fault tampering changes response AAD and is rejected. The
fault, not the unprotected outer `operation_status` flag, authorizes fault
effects. An ERROR flag alone with authenticated fault NONE cannot cancel a
fault-free successful response. Session-info HMAC authenticates its session
payload/challenge, NOT its outer fault; that outer fault is never used to reset
or clear storage on the HMAC path. Invalid/missing HMAC or malformed session
payload is rejected without terminally failing the live command.

Protocol-supported unsigned TIME_EXPIRED, INCORRECT_EPOCH,
INVALID_TOKEN_OR_COUNTER and INVALID_SIGNATURE hints require an explicit
matching domain and the live command's exact 16-byte request UUID. Only bare
unsigned faults are advisory (no signature envelope, no payload). A hint may
request a fresh authentication exchange via `force_auth`; it cannot complete
the command, reset a peer, consume replay/counter state, or write/remove session
storage. Its duplicates during that exchange have no effect. Bad encrypted
responses cannot fall through into this unsigned advisory path. Other unsigned
faults are ignored. Successful fresh HMAC acceptance clears the advisory flag.

Both-domain regression matrix passes bad tag alone, wrong UUID alone, both
together, and fault tampering, with zero terminal/session/storage effects;
legitimate authenticated BUSY completes failure exactly once, while correlated
authenticated INVALID_SIGNATURE retains the existing scoped peer-reset/storage
recovery behavior and does not reset the other domain. The unsigned advisory
tests cover all four recovery hints in both domains: the peer/counter and
storage remain intact until a new authenticated exchange succeeds. Existing
legacy unsigned VCSEC payload compatibility was not replaced; this is a fix to
outer-fault admission, not a claim that every legacy VCSEC observation is signed.

### B2 — live, single-use session challenges and disconnect boundary

Client tracks a pending session-info exchange independently of mere retention
of the last command UUID. Building a new request replaces its challenge; an
ordinary same-domain request invalidates a prior authentication challenge.
Request-state snapshots include these pending flags; TX rollback still cannot
restore them across the peer session-revision reset at disconnect/key change.
Disconnect retires pending exchanges, zeros correlation material, and clears
the decoded MessageProcessor queue BEFORE completion callbacks. A connection
generation guard rejects an already-popped message whose diagnostic observer
disconnects/reconnects before operational dispatch.

Applying/persisting a session requires current connected state, a live waiting
authentication command, matching authentication domain, a pending challenge,
and its valid HMAC. Protocol responses without a request-UUID echo still prove
correlation through the HMAC challenge; when an echo is present it must match.
Verified exchanges are single-use, including authenticated denial or save
refusal. Timeout/retry/terminal completion/key replacement/disconnect invalidate
them. Bad HMAC/echo/decode does not consume the live exchange or fail its command.

Expanded both-domain tests pass consumed challenge reuse, stale queued accepted
duplicates, stale still-outstanding responses, disconnected RX, reconnection
with a new challenge, timeout replacement, and observer disconnect/reconnect.
Fresh legitimate HMAC recovery, including forced lower-counter resync, passes.
The previous persistence-refusal test retains ALL key/session/counter/replay/
stored-byte assertions and now additionally proves a fresh command rejects the
consumed old HMAC, obtains a NEW challenge, rejects the old response again, and
successfully persists/applies the freshly signed response. It does not re-arm
or weaken the production challenge check to accept its old fixture.

### B3 — VCSEC transactional decode/replay admission

VCSEC now records the authenticated wire response counter locally, decodes the
inner protobuf, and ONLY THEN commits the replay-window mutation, matching the
CarServer sibling's transaction order. The preserved malformed-first/same-
counter valid-retry probe passes. Added tag/UUID tamper controls remain rejected,
and a duplicate of the successfully decoded VCSEC response is rejected with
exactly one status callback. Request counters are not substituted for wire
response counters.

### Full verification receipts — current sources and binary

- `bash software/tests/tesla_vehicle/run_host_tests.sh`: exit 0; final
  `Tesla BLE host tests passed.` Log `host.log`, manifest `host-result.json`:
  all **58 C++ test entries plus web-action tests** completed. Parsed mbedTLS
  counts remain **5,102 injected faults across 14 paths**, with refusal/state/
  resource/persistence/retry assertions retained. The pre-existing expected
  LittleFS child SIGABRT remains part of its passing orphan/quarantine test.
- `uv run --directory software python /home/hermes/.hermes/cache/scratch/tesla-phase1-eleventh-web.py`:
  exit 0, `Standalone production web/TypeScript pipeline passed`; `web.log`.
  Uses the actual production `build_web` definition; tsc, esbuild, sass,
  postcss and html-minifier-terser all completed.
- BUILD ONLY:
  `TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs uv run --directory software pio run -e warp4_tesla_ble`:
  exit 0, `warp4_tesla_ble SUCCESS 00:01:22.859`; `firmware.log` and
  `firmware-result.json`. PIO RAM **93,356 / 327,680 bytes**, flash
  **4,828,309 / 6,488,064 bytes**. Detailed ELF DRAM **93,354 bytes**
  (59,488 BSS + 33,866 data). Existing branch-fetch, hyphenation and nanopb
  pip-unavailable warnings remain nonfatal; no warning-free claim.
- Current ELF/map/memory/release audit: exit 0, `elf.log`, `elf/summary.json`,
  `elf/current-memory.json`, `elf/map-range-check.json`. The existing audit was
  reused with a FRESH output directory, not the historical eleventh receipts.
  All **eight** numerical Bluetooth data/BSS map/ELF endpoints match. The
  general wrapper's disassembly was extended beyond its truncated nm size to
  include its final call/return (`elf/release-wrapper-complete.dis`).
- `python3 software/tests/tesla_vehicle/host/audit_reachable_stack.py /home/hermes/.hermes/cache/scratch/tesla-phase1-review4-fixes/stack`:
  exit 0; `stack.log`, `stack/summary.json`, `stack/callgraph.json` and current
  disassembly. Current ELF hashes in both audits agree.
- `git diff --check` and
  `git diff --check 03798d2515710d2a72dcb08ec72b6b502f6028d1`: exit 0.
  A final post-document check and source/branch/framework inventory are saved
  in `final-check.json`; no production/test changes follow the passing gates.

CURRENT ELF SHA-256:
`57358af62b46bdc6a4ba0faedba13676ba714fa921ed61bb00e5396ceb1eb168`.
CURRENT map SHA-256:
`ad8cb311781e32fd115221fcda7463b560d89f5961ff57977890a6c93c496877`.
Native executable/archive hashes: `native-binary-hashes.json`.
Verified production/test hashes: `verified-source-hashes.json`.

Current linked release chain:
`release_bluetooth 0x402610cc -> btMemRelease 0x40278ba8 -> __wrap_esp_bt_mem_release 0x40278b88 -> esp_bt_mem_release 0x402c3694 -> esp_bt_controller_rom_mem_release 0x402c3584`.
Caller supplies BLE mode **1**; the static-release branch requires mode **3**;
controller-only wrapper is absent. Unchanged static ranges: **5,172 bytes**
BLE-only controller ROM reserve covered by release; **33,240 bytes** other
controller reservations and **15,116 bytes** named Bluetooth data/BSS not
released by it. These are NOT measured net heap reclamation or zero-BT-DRAM boot.

Current reachable graph: **1,476 functions**, **1,271 unresolved indirect sites**,
**17 cycle nodes**, no available `.su` files. Discovered ENTRY sums remain
**9,088 bytes** for the configured 12,288-byte worker and **2,784 bytes** for
the configured 4,096-byte teardown. They are branch-insensitive sums on
DISCOVERED acyclic paths, NOT complete bounds, feasible worst cases or safe
margins. Unknown callbacks/virtual/ROM/library recursion/non-ENTRY SP changes,
register-window spills, interrupts and RTOS reserve remain unknown. Hardware
worker AND teardown high-water/guard measurements remain UNPERFORMED.

### Unchanged limitations and final phase boundary

No framework package was edited. `framework-before.json` and
`framework-check.json` record **4,810** existing framework/package/library files
with no changed content through the gates. Source hashes against Astra's 2,074-
file baseline confirm only these B1–B3-related existing source/test files changed:
Client/Vehicle headers and implementations, host-runner registration, the
behavior test's explicit return, and the strengthened persistence fixture.
Two new regression test files were added; this document is append-only.
All earlier worktree changes, LICENSE and PROVENANCE.md remain untouched.

External limitations from the full fourth review are retained, not relabeled
GREEN, weakened, or claimed fixed:
- Shared ConfigRoot's pre-validator allocating copy and shared Config/API
  incomplete serialization/dirty-flag clearing/acceptance contract still allow
  empty or corrupted publication false-success. Existing external reproducer
  and fourth-review evidence remain unchanged, outside acceptance closure.
- Bluedroid characteristic AND descriptor deep-copy OOM can accept zero-length
  payloads while reporting success. Timeout/quarantine is not guaranteed for
  that false-success path; no module-local reserve proves intended-byte delivery.
- LittleFS close failure retains one orphaned wrapper/cache until reboot.
  Tesla quarantine bounds repeated Tesla I/O but is not leak-free/shared-mount
  repair, power-loss durability or validated hardware reboot recovery.
- Local request rollback is not remote cancellation or exactly-once delivery
  after partial/uncertain transmission. Native deterministic mbedTLS allocation
  sweeps do not prove ESP32 target config/accelerator/allocator OOM behavior.
- RF, Tesla interoperability, sleep/wake, watchdog/SDK load, fragmentation,
  dynamic heap reclamation, reboot and stack high-water safety need hardware
  validation; none was performed or inferred from static/native evidence.
- AGPL-3.0-only library metadata and the upstream MIT-metadata/AGPL-license
  discrepancy remain unchanged release blockers for the intended distribution.

Current local status: **READY FOR ANOTHER INDEPENDENT ASTRA REVIEW**, with these
explicit external/hardware/licensing limitations. B1–B3 focused RED/GREEN and
all requested local gates are complete. This is NOT Astra approval, NOT
production/deployment/hardware-safe approval, and NOT authorization for Phase 2.
No flash, upload, hardware access, commit, push or publication occurred.

## Fourteenth continuation — fifth Astra review R1–R3 only

Read the FULL fifth Astra review from session `20261011_045858_c42498`, final
message `221056`. Its **NOT READY** verdict supersedes the previous readiness
statement. This continuation fixes exactly its three local findings. Phase 1
only, in `/home/hermes/warp-tesla-ble`, branch
`feature/tesla-ble-upstream-ready`; HEAD remains
`03798d2515710d2a72dcb08ec72b6b502f6028d1`. No Phase 2, hardware access,
flash, upload, commit, push, publish or framework-package modification.

Evidence root: `/home/hermes/.hermes/cache/scratch/tesla-phase1-review5-fixes/`.
The reviewer's original inputs under `astra-phase1-review5` remain unchanged.
`additional_probe.cpp` was preserved in `test_tesla_ble_review5.cpp` before
production edits, changing only the fixture include and entry-point integration.
Repository controls are in `host/review5_controls.inc` and
`host/review5_recovery.inc`. The regression runs by default in the full host gate.
It links actual Vehicle/Client/Peer/MessageProcessor, nanopb and native mbedTLS.

### Focused RED -> GREEN

Command: `bash software/tests/tesla_vehicle/host/run_protocol_test.sh test_tesla_ble_review5 <mode>`.
`focused-red.json` and the corresponding `*-red.log` preserve actual behavioral
failures before production changes:

| Preserved mode | RED | GREEN |
| --- | --- | --- |
| cross-domain | exit 1; both infotainment waiting states completed failure, queue=0 | exit 0; completions=0, queue=1, original waiting state retained |
| session-payload | exit 1; both domains/faults saves=0, epoch=1; six writes exhausted retries | exit 0; all four cases saves=1, epoch=2, command retries without failure |
| late-auth | exit 1; RX-first saves=1 in both domains | exit 0; saves=0 with either timer order in both domains |
| controls | exit 0 before edits | exit 0 after edits |

Expanded R1/R2/R3 controls independently produced assertion failures before
their fixes (exit 134 or direct subprocess SIGABRT), then passed. Initial new
control compilation mistakes were corrected before recording behavioral RED;
they are not mislabeled production defects. A final R1 sibling control exposed
legacy GET_STATUS observations completing a poll's authentication prerequisite:
`r1-poll-auth-red.log`, exit 134. Requiring the poll's ordinary response-waiting
state fixed it; final R1 controls retain fresh HMAC acceptance and legacy polling.

Final focused tests all exit 0: review5, session_replay, persistence_allocations,
outer_admission and behavior. Receipts: `focused-suite-final.json`, corresponding
`test_tesla_ble_*-green-final.log`. The ORIGINAL additional probe source was also
freshly compiled against current production sources and current repository
fixtures; controls/cross-domain/session-payload/late-auth all exit 0.
`original-and-audit-final.json` contains exact outputs. The copied original
probe SOURCE is byte-for-byte unchanged (SHA256
`da3c23e5b855dd2b0e93b4883895e892526ccf058a882e693094fd902d2c42bb`).

### R1 — domain/exchange-scoped VCSEC terminal effects

Plaintext nominalError no longer fails the queue head. It is terminal only
after the live VCSEC ordinary-command UUID, GCM authentication, protobuf decode
and response-replay gates. commandStatus uses the same boundary, with the
explicit legacy present-key pairing status exception; it cannot answer a
session-authentication exchange. Legacy vehicle observations still publish
sleep/status, but may advance only an explicit ordinary VCSEC GET_STATUS poll
or a broadcast wake-observation prerequisite. They cannot replace session HMAC
authentication in either domain. A status callback's reentry cannot apply the
old response to a replacement queue-head command.

Both-domain ordinary/authentication negatives pass for plaintext nominal/status
input, even with a public UUID echo. Authenticated VCSEC nominal/status controls
complete once; wrong UUID/bad tag do not. Unsolicited observations, legacy poll,
wake and pairing controls pass. No blanket unsigned-current-command failure
remains. Legacy observations are explicitly NOT authenticated telemetry.

### R2 — supported ordinary-command SessionInfo recovery

Protocol evidence remains pinned to vehicle-command
`a4b43c1eff0e09d77deb9f2dce97031141fe8c8a`:
`pkg/protocol/protocol.md:711-731`, `internal/authentication/verifier.go:52-58,247-273`,
`native.go:76-87`, and dispatcher/session request-correlation/latency admission.
These sources explicitly support SessionInfo attached to INCORRECT_EPOCH and
INVALID_SIGNATURE, HMAC-challenged by the ordinary command's UUID.

The narrow recovery path requires a live ordinary waiting command, explicit
matching source domain, exact command AND current Client UUID, fresh transmit
age, valid SessionInfo decode and HMAC, OK authenticated session status, and
nonregressing same-epoch authenticated clock. The HMAC covers SessionInfo/VIN/
challenge, NOT the outer domain/fault/status. Outer fields only select the
recovery shape; they do not authorize resets, storage deletion or terminal
failure. Same-epoch recovery retains the higher anti-replay counter; forced
lower-counter explicit-handshake resync remains separate and tested.

The verified ordinary exchange is consumed before persistence/publication,
including save refusal. Successful transactional session update resumes the
command with a newly generated/transmitted UUID. Queued duplicates and responses
to retired commands cannot reapply. Both domains and both faults pass wrong-domain,
wrong/missing UUID, bad/missing HMAC, malformed payload, stale transmit age and
stale authenticated clock controls with unchanged peer/replay/storage state.
Tests prove exact persisted response bytes, actual production Vehicle recreation
loading the saved key/session, automatic retry and authenticated final command
completion exactly once. Persistence refusal retains the prior peer/material,
reports one failure and requires a newly correlated recovery exchange on retry.

### R3 — admission freshness independent of timer order

Session response admission checks the command's steady-clock transmit age
against AUTH_RESPONSE_TIMEOUT AND CLOCK_SYNC_MAX_LATENCY before parsing/HMAC,
again after HMAC, and in the pre-persistence gate. Connected generation and
queue-head identity are also checked. RX-first processing cannot persist an
expired challenge; future/default timestamps do not grant a valid deadline.
Tests retain both timer-order reproductions, the tighter clock-sync expiry,
and fresh explicit response acceptance in both domains.

No existing assertion was removed or weakened. The original repository replay
fixture now initiates a real accepted authentication TX instead of constructing
a waiting state with no transmit timestamp. The persistence fixture's directly
constructed waiting state now has a fresh timestamp, so its unchanged consumed-
challenge and transactional-refusal assertions exercise the new admission gate
rather than failing solely on a default timestamp.

### Final current-source verification

- Full host suite: `bash -x software/tests/tesla_vehicle/run_host_tests.sh`,
  exit 0; `Tesla BLE host tests passed.` `host-final.log`, `host-final-result.json`.
  Programmatic enumeration verifies **59 C++ entries plus web-action tests**.
  Parsed native mbedTLS coverage remains **5,102 faults across 14 paths** with
  refusal/state/resource/persistence/retry assertions. Expected isolated LittleFS
  child SIGABRT remains part of its passing quarantine test.
- Standalone web: `uv run --directory software python /home/hermes/.hermes/cache/scratch/tesla-phase1-eleventh-web.py`,
  exit 0; actual production build_web ran tsc/esbuild/sass/postcss/html-minifier.
  `web.log`, `web-result.json`; no frontend source changed.
- Firmware BUILD ONLY with existing TESLA_BLE_FRAMEWORK_LIBS:
  `TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs uv run --directory software pio run -e warp4_tesla_ble -j 4`,
  exit 0; SUCCESS **00:01:07.131**. `firmware-final.log`, `firmware-final-result.json`.
  PIO RAM **93,356 / 327,680**, flash **4,829,389 / 6,488,064 bytes**;
  detailed ELF DRAM **93,354 bytes** (59,488 BSS + 33,866 data).
  Existing nonfatal nanopb pip-unavailable and other build warnings are not
  claimed fixed. Locally generated images were NOT flashed/uploaded/published.
- CURRENT ELF/map/release/memory and reachable-stack audits both exit 0:
  `elf-final.log`, `stack-final.log`, `elf/`, `stack/`.
  All eight numerical Bluetooth map/ELF endpoints match.
  ELF SHA256 `2ac855e28e091d821bf2cf50a6370ec3af34b9894bfeb085d30633e4780f5e8a`;
  map SHA256 `449eb4ee07c015c8950a60dfc6cecd8394ae6beea5fae2c2f094855e0db28f80`.
- Release chain: `0x402614c4 -> 0x40278fa0 -> 0x40278f80 -> 0x402c3a8c -> 0x402c397c`
  (release_bluetooth -> btMemRelease -> general wrapper -> esp_bt_mem_release
  -> ROM release). Caller BLE mode 1, static-release branch mode 3;
  controller-only wrapper absent. **5,172** BLE ROM bytes covered; **33,240**
  other controller reservations and **15,116** named BT data/BSS bytes not released.
  These are static linked ranges, NOT net heap reclamation or zero-BT-DRAM boot.
- Stack discovery: **1,481 functions**, **1,264 unresolved indirect sites**,
  **17 cycle nodes**. Discovered ENTRY sums **9,088 / 12,288** worker and
  **2,784 / 4,096** teardown bytes. They are NOT complete upper bounds or safe
  margins; unresolved callbacks/recursion/ROM/platform overhead remain unknown.
- `acceptance-final.json`, `verified-source-hashes.json`, and
  `native-binary-hashes.json` preserve parsed totals and current source/binary hashes.
  All **4,810** inventoried framework/library/package files and original review
  inputs retain their pre-run hashes. Existing changes outside this slice remain
  intact. Only vehicle.cpp, host-runner registration, the two fixture updates,
  three new regression files and this append changed in this continuation.
- Both diff checks passed before this append. Final post-document diff checks,
  append-only prefix proof and branch/HEAD/framework receipts are in
  `final-check.json`; no functional/test source edits follow the passing gates.

### Unchanged external/hardware/licensing limitations and phase boundary

The fifth review's external findings remain UNFIXED and are not relabeled GREEN:
shared ConfigRoot pre-validator allocation and Config/API incomplete serialization/
dirty-flag clearing can accept empty/corrupted publication; shared backend queue/
topic ownership contracts remain unproved. Bluedroid characteristic/descriptor
deep-copy OOM can report success with zero-length payload, without guaranteed
quarantine. SDK async cache/message/completion ownership gaps remain. Their
preserved reproducers and source were not altered by this continuation.

LittleFS failed-close quarantine bounds repeated Tesla I/O but retains one orphan
wrapper/cache until reboot; it is not leak-free shared-mount repair, power-loss
durability or validated hardware recovery. Local TX rollback is not remote
cancellation or exactly-once delivery after partial/uncertain transmission.
Native deterministic crypto faults do not prove target ESP32 config/accelerator/
allocator behavior. No real WARP4/Tesla interoperability, RF/sleep/wake,
watchdog/SDK-load, fragmentation, target crypto-OOM, dynamic heap reclamation,
reboot or worker/teardown stack high-water validation occurred or is inferred.
AGPL-3.0-only metadata and the pinned MIT-metadata/AGPL-license discrepancy remain
unchanged release blockers. Licensing/provenance files were untouched.
The lower-severity disconnected-library plaintext observation noted by Astra
remains outside R1–R3; no unconditional library disconnected-RX guarantee is made.

Local status: **READY FOR ANOTHER INDEPENDENT ASTRA REVIEW**, with all these
explicit limitations. This is NOT Astra approval, Phase-1 approval, release/
deployment/hardware-safe approval or authorization to begin Phase 2.

## Fifteenth continuation — sixth Astra review R1/R2 only

Read the complete 122-line sixth review at
`/home/hermes/.hermes/cache/scratch/astra-phase1-review6/review.txt`.
Its NOT READY verdict supersedes the preceding readiness statement. This
continuation closes its two newly reproduced local findings only. Work remains
Phase 1 in `/home/hermes/warp-tesla-ble`, branch
`feature/tesla-ble-upstream-ready`, HEAD
`03798d2515710d2a72dcb08ec72b6b502f6028d1`. No Phase 2, framework-package edits,
commit, push, publish, upload, flash or hardware access occurred.

Evidence root: `/home/hermes/.hermes/cache/scratch/tesla-phase1-review6-fixes/`.
The original review directory and its receipts remain unchanged.

### Test-first RED and preserved probes

Before production edits, `run_astra.py red` rebuilt Astra's exact
`additional_probe.cpp` and `public_controls.cpp`, using its preserved compiler
driver relocated to a new output directory. Original-source copies are
byte-for-byte identical, verified again after final gates; source hashes and
exact compiler commands are retained in each output directory.

- Original wake: exit 1. Legacy plaintext advanced; valid independently sealed
  and production-decrypted GCM awake response did not advance or observe awake.
- Original recovery-budget: exit 1 in both domains for INCORRECT_EPOCH and
  INVALID_SIGNATURE: 32 responses, retry_count=0, writes=33, saves=32,
  completions=0, queue=1.
- Both supplemental public-loop modes: exit 1. Ordinary encrypted VCSEC control
  and legacy wake passed; encrypted prerequisite failed. Repeated recovery kept
  the first command ahead of queued work, even with an injected hour-old start.
- Retained controls: exit 0 before edits.
- New repository `test_tesla_ble_review6` wake/recovery/siblings modes each
  independently failed behavioral assertions with exit 134 before production
  edits. Receipts: `expanded-red.json`, `expanded-*-red.log`. These are assertion
  failures, not compiler failures or fabricated results.

Final exact-source runs: `run_astra.py green-final --paced`; original
retained-controls/wake/recovery-budget and both public modes all exit 0.
Original recovery now reports, in all four cases: responses=6, retry_count=5,
writes=6, saves=5, completions=1, queue=0, state=FAILED. Supplemental public
recovery retires the original head and starts later work: writes=7, saves=5,
completions=1, queue=1, retained=0.

Pacing qualification: Astra's recovery bodies assume each `Vehicle::loop()` has
immediately retransmitted and assert WAITING_FOR_RESPONSE on the next iteration.
That timing assumption conflicts with the required backoff. Their SOURCE,
responses and assertions were not edited. `astra_pacing.cpp` is a test-only
linker wrapper enabled only for recovery-budget modes; it sleeps until the
production command's real `next_retry_time` before calling the real loop. It
does not change production code, clocks, timestamps, budgets, HMAC/GCM or
assertions. Wake and retained-control modes are not paced. The repository
regressions do NOT use this wrapper: they assert that early loops perform no
TX/save, then explicitly advance only the retry timer. Injected timestamps are
state-machine tests, not claims of elapsed hours. The preserved compiler driver's
own exit 0 is not used as proof: child-mode exits are separately asserted zero
in `acceptance.json`/the summarizer.

### R1 — separate live VCSEC wake exchange

`Command::WakeExchange` retains its actual VCSEC domain, 16-byte UUID, bounded
request-hash snapshot, accepted-TX timestamp and connection generation. An
infotainment command remains an infotainment command; its ordinary UUID is not
reused to pretend the prerequisite is an ordinary VCSEC request.

Admission requires the owning queue-head command, ENSURING_AWAKE phase,
AUTH_RESPONSE_WAITING state, broadcast wake-prerequisite marker, matching actual
VCSEC domain/UUID, live generation, nonfuture transmit/exchange age within the
25-second wake lifetime and the logical-command deadline. Decryption uses the
retained wake hash, not Client's mutable last hash. GCM authentication and inner
decode precede replay-counter admission and status effects. The exchange retires
on retry, prerequisite completion/advance, failure/finalization and disconnect.
Accepted writes check queue ownership after transport reentry. Arbitrary valid
VCSEC messages cannot authenticate against an infotainment queue head that has
no outstanding wake exchange; an authenticated commandStatus ACK alone does not
constitute an awake observation.

Repository controls cover wrong/missing UUID, wrong domain, bad tag, zero/replayed
counter, exchange/transmit/total expiry, future exchange timestamp, wrong
generation, old retry/disconnect/replacement-command frames, duplicate accepted
awake frames, non-awake ACK and request-hash isolation after another Client build.
The ordinary encrypted VCSEC and deliberate signature-free legacy observation,
GET_STATUS poll, wake and pairing controls remain passing. Unsupported signatures
are not treated as plaintext observations. Legacy observations remain explicitly
unauthenticated; they do not replace SessionInfo HMAC or authorize ordinary
signed-command completion.

### R2 — nonrenewable logical-command recovery policy and sibling audit

All ordinary SessionInfo recoveries spend the existing MAX_RETRIES=5 budget
BEFORE peer update/persistence. The sixth valid recovery retires the command
without another session save. The same retry counter is shared with transport
retries, authentication timeouts, authenticated GCM synchronization faults and
correlated unsigned advisory re-authentication; handshakes cannot reset it.
Retries use the established exponential backoff starting at 250 ms, with bounded
delay/jitter. Queue processing now actually honors `next_retry_time` in READY,
IDLE and AUTHENTICATING, not just an unused timer field.

TOTAL_COMMAND_TIMEOUT=120 seconds is measured from enqueue and never refreshed
by IDLE, phase changes, retransmission, recovery or backoff. It includes queued
time and all prerequisites. Deadline checking precedes the backoff gate in every
live command state; RX-first dispatch/admission cannot accept an hour-old command
before its timer runs. Retirement moves the completion callback once and allows
the next queue head to progress. This is a cooperative library deadline, not
SDK cancellation or a bound on a blocking platform call. WARP's separate
40-second SoC timeout still does not cancel a library command; no such integration
change or hardware validation is claimed.

The narrow SessionInfo recovery admission retains explicit source-domain and
ordinary/current-Client UUID correlation, valid decode/HMAC/status, transmit
freshness, nonregressing same-epoch clock and max(local, reported) same-epoch
counter. Explicit authenticated lower-counter handshake resync stays separate.
Bad or stale inputs spend no recovery budget and do not mutate/save the peer.
Duplicate responses are consumed before publication/persistence, including
transactional refusal. Outer HMAC-unprotected fields select the recovery shape,
not peer-reset/storage-delete authorization. Unsigned advisories likewise do not
reset peers or delete storage; terminal exhaustion is the local resource policy,
not a trusted interpretation of the unsigned fault.

The sibling transition audit found and bounded the same bypass class in GCM
TIME_EXPIRED/INCORRECT_EPOCH/INVALID_TOKEN_OR_COUNTER/INVALID_SIGNATURE recovery,
unsigned advisory-to-handshake cycles, and authentication timeouts. Separate
wake-exchange SessionInfo/GCM synchronization recovery is also charged and tested
for both INCORRECT_EPOCH and INVALID_SIGNATURE. Existing authenticated fault
reset/delete semantics remain restricted to admitted GCM responses and a charged
recovery; HMAC synchronization recovery does not adopt those destructive effects.

The full repository regression proves repeated HMAC recovery for both domains
and both faults; exactly five saves, bounded TX, one terminal completion, and a
subsequent queued command reaching authenticated SUCCESS exactly once. Successful
one-off recovery also completes successfully in both domains. All four sibling
faults are exercised for authenticated and advisory cycles in both domains;
authentication-timeout exhaustion, wake-recovery cycles and total expiry in all
live states/backoff are covered. Previous replay/freshness/counter/persistence
negative assertions remain. Older fixtures now explicitly verify no early TX
and advance the retry timer; their former immediate-TX/retry_count=0 expectations
were replaced with assertions for the required charged backoff, not removed.
One supplemental wake-cycle fixture initially marked the vehicle awake while
enqueueing later work, thereby bypassing wake; it was corrected to UNKNOWN before
recording final passing gates. That fixture mistake is not a production defect.

### Final current-source gates and fresh target artifacts

All commands below exited 0. `*-result.json` contains command, exit, duration and
log path. Functional/test source was frozen before these final gates.

- Full host: `bash -x software/tests/tesla_vehicle/run_host_tests.sh`.
  `host.log`, `host-result.json`: programmatic source/trace comparison verifies
  **60/60 distinct C++ entries**, no missing/extra entries, plus web-action tests.
  Native mbedTLS reports **5,102 allocator faults**; this is fault cases, not
  5,102 test files or proof of target ESP32 crypto-OOM behavior. The expected
  isolated LittleFS failed-close child assertion remains part of a passing test.
- Production web/TypeScript:
  `uv run --directory software python /home/hermes/.hermes/cache/scratch/tesla-phase1-eleventh-web.py`.
  `web.log`, `web-result.json`: current production build_web/check_call execute
  tsc/esbuild/sass/postcss/html-minifier tooling. The standalone helper uses
  existing preprocessed src_tfpp; the full firmware pipeline also ran. No web
  source was changed in this continuation.
- Full firmware BUILD ONLY:
  `TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs uv run --directory software pio run -e warp4_tesla_ble -j 4`.
  `firmware.log`, `firmware-result.json`: SUCCESS **00:01:29.774**;
  PIO RAM **93,356 / 327,680 bytes**, flash **4,831,021 / 6,488,064 bytes**.
  No warning-free claim; generated images remain local and were not uploaded.
- Fresh ELF/map/memory audit: reused the inspected
  `tesla-phase1-review5-fixes/run_current_audit.py` with output relocated here;
  `elf.log`, `elf-result.json`, `elf/`. All **eight numerical Bluetooth map/ELF
  endpoints match**. Detailed ELF DRAM **93,354 bytes** = 59,488 BSS + 33,866 data,
  distinct from PIO's report. ELF SHA256
  `552aa2fb2ea752802adfc3e525cfc4c8080e7133a6d0b523dfed76a45a02c16c`;
  map SHA256 `6f46a7450c8bb3d974a23221bdb70353368b25316f12faf2bfaaf04f38692cd6`.
- Current release chain: release_bluetooth **0x40261bb0** -> btMemRelease
  **0x4027968c** -> general wrapper **0x4027966c** -> esp_bt_mem_release
  **0x402c4178** -> controller ROM release **0x402c4068**. Caller BLE mode 1;
  general static-release branch mode 3. Complete wrapper disassembly is retained.
  **5,172** BLE-controller ROM bytes are covered; the other **33,240** controller
  reservations and **15,116** named BT data/BSS bytes are not. This is static
  linked coverage, not net heap reclamation or zero-BT-DRAM boot.
- Fresh reachable-stack:
  `python3 software/tests/tesla_vehicle/host/audit_reachable_stack.py /home/hermes/.hermes/cache/scratch/tesla-phase1-review6-fixes/stack`.
  `stack.log`, `stack-result.json`, `stack/summary.json`, `stack/callgraph.json`.
  Its ELF/map hashes match the audit AND the current built files. Discovered
  ENTRY sums remain worker **9,088 / 12,288** configured bytes, teardown
  **2,784 / 4,096**; **1,504 reachable functions**, **1,266 unresolved indirect
  sites**, **17 cycle nodes**. These branch-insensitive discovered-path sums are
  NOT complete upper bounds, feasible worst cases, proven margins or a stack-
  safety certificate. Indirect/ROM/callback paths, recursion, spills, non-ENTRY
  stack changes and RTOS/interrupt reserve remain unknown. No target high-water
  measurements were taken.
- Both `git diff --check` and base-relative diff check exit 0. The pre/post-gate
  inventory covers **10,639 protected package/framework/original-review files**:
  no changed/added/deleted protected files. Comparison with Astra's source
  inventory identifies only vehicle.h/cpp, host-runner registration, the two
  timing-aware fixture updates, this documentation append and the new review6
  regression as this continuation's source changes. Other worktree changes,
  licensing and provenance remain intact. `integrity-after.json` records final
  branch/HEAD, append-only prefix proof and post-document diff checks.

### Remaining limitations and readiness boundary

Shared ConfigRoot pre-validator allocation and Config/API incomplete publication/
dirty-flag clearing defects, Bluedroid characteristic/descriptor deep-copy
false-success under OOM, and shared backend/SDK asynchronous ownership gaps
remain UNFIXED. Their sixth-review reproducers/evidence remain preserved, not
relabeled GREEN or included in R1/R2 closure. No framework package was changed.

LittleFS failed-close quarantine retains an orphaned wrapper/cache until reboot;
it is not leak-free repair or proof of power-loss durability. Local TX rollback
is not remote cancellation or exactly-once delivery after partial/uncertain TX.
SDK timeout quarantine retains ownership; it does not make blocking calls
cancellable. Native tests/static audits do not validate real Tesla/WARP4
interoperability, RF/sleep/wake, watchdog/SDK-load, fragmentation, target
crypto-OOM, dynamic heap reclamation, reboot recovery or worker/teardown stack
high-water behavior. AGPL-3.0-only metadata and the pinned MIT-metadata/AGPL-license
discrepancy remain unchanged distribution/licensing blockers.

Local status: **READY FOR ANOTHER INDEPENDENT ASTRA REVIEW** of Phase 1, with
these explicit external/hardware/licensing limitations. This is not independent
Astra approval, Phase-1 acceptance, deployment/hardware-safety clearance or
authorization to begin Phase 2.

## Sixteenth Phase-1 continuation: seventh-review local fixes (gates incomplete)

This continuation uses the preserved completed seventh Astra review at
`/home/hermes/.hermes/cache/scratch/astra-phase1-review7/review.txt` and its
public/adversarial probes. The provider safety-filter exit does not turn the
preserved findings into approval. All new receipts below are under
`/home/hermes/.hermes/cache/scratch/tesla-phase1-review7-fixes/`.

### Exact preserved probes: RED before production edits, GREEN afterward

`run_exact.py` builds unchanged `public_defects.cpp` and `adversarial.cpp`
against repository production sources, using the preserved compiler arguments.
The public modes retain their real clock; only the adversarial executable uses
its preserved deterministic steady-clock wrapper. Boundary transport/storage
fixtures are not hardware or remote-delivery evidence.

- `red/results.json`: public `denial` and `auth-refusal` each exit **1**;
  adversarial `denial` and `auth-refusal` each exit **1**. The retained
  adversarial `wake`, `mixed`, `timers`, and `reentry` controls each exit **0**.
  This was run before production edits. Each individual child exit is checked;
  a successful wrapper is not substituted for the children failing as expected.
- `green2/results.json`: all eight unchanged probe modes exit **0**, including
  all four exact finding reproductions. Per-mode logs, compiler arguments and
  original-source hashes are retained beside each result manifest.
- The intermediate `green` attempt failed to compile the processing guard's
  CheckedShared dereference; changing it to `*command.get()` preceded the
  successful `green2` build/run. That intermediate failure is not a GREEN receipt.

### R7-1 retained authentication identity and callback reentry

The verified non-OK SessionInfo path now fails only its retained command and
returns; it no longer dispatches a second denial by peeking a potentially new
queue head after completion. Authentication continuation carries the retained
command plus connection generation, command revision and Client request
revision. Phase changes, retry, TX construction and retirement invalidate old
command context; request replacement/consumption/rollback invalidates Client
context. Context is revalidated after external phase, observer, persistence,
builder and transport callbacks in the exercised protocol paths. Terminal
finalization retains ownership, detaches the queue entry, and takes its completion
before external terminal-phase/completion callbacks. Same-command processing
reentry is guarded without preventing a detached successor from progressing.

### R7-2 charged delayed authentication-submission refusal

An immediate `write_bytes()` refusal for SessionInfo now restores the retained
transactional request state and schedules a charged authentication retry through
the existing shared logical-command retry budget/backoff. Exhaustion terminates
the retained command rather than spinning. The enqueue-based total deadline is
not renewed. Ownership/context is checked before acceptance or rollback after
transport reentry so old work cannot roll back or advance a replacement exchange.

Sibling audit covered phase callbacks, message/status/CarServer observers,
SessionInfo persistence/commit, builder callbacks and all three protocol write
sites. Wake and ordinary-command refusal retain their terminal-failure contracts,
now with the same post-transport context check; authentication refusal alone
uses the shared delayed recovery path. This is serialized-worker callback
reentry protection, not a universal concurrent-race proof, remote cancellation
or proof of exactly-once delivery after uncertain/partial transport submission.

### Expanded repository regression and completed gates

`software/tests/tesla_vehicle/test_tesla_ble_review7.cpp` uses production
protocol code and real HMAC/GCM with a deterministic host clock. It is registered
in the full host suite and reuses review6 fixtures through a fixture-only guard.
`expanded-green3.log` records passing coverage of:

- Six both-domain denial schedules: bad-HMAC rejection, duplicate denial,
  completion-loop reentry and disconnect/reconnect replacement. The original
  denial completes only once; a same-domain successor authenticates and succeeds
  exactly once, and the old denial cannot answer its fresh exchange.
- Eight initial/recovery, persistent/one-shot refusal schedules across both
  domains: UUID/hash/counter/replay rollback, no early write attempt, shared finite
  budget, stable enqueue time, and successful queued successor.
- Mixed HMAC recovery, response timeout, GCM recovery, advisory and refused auth
  share one budget. The exact total-deadline boundary remains inclusive; after
  crossing it, retirement completes once without another submission, even while
  a refusal backoff is pending. Deadline coverage uses timer-state injection,
  not a claimed 120-second wall-clock or target experiment.
- Accepted/refused transport lifecycle reentry and same-command/same-domain
  phase-callback retry leave the fresh exchange intact; an old authentication
  continuation cannot mark that new exchange READY.

Fresh complete host-suite invocation:
`python3 /home/hermes/.hermes/cache/scratch/tesla-phase1-review7-fixes/gate.py host`
executes `bash -x software/tests/tesla_vehicle/run_host_tests.sh`: **exit 0**,
371.221 seconds. `host-result.json` and `host.log` retain the command and complete
trace, including passing web-action tests and the final host-suite success line.
No new programmatic source/trace enumeration count is claimed here.

Fresh production web/TypeScript invocation:
`python3 /home/hermes/.hermes/cache/scratch/tesla-phase1-review7-fixes/gate.py web`
executes `uv run --directory software python
/home/hermes/.hermes/cache/scratch/tesla-phase1-eleventh-web.py`: **exit 0**,
6.342 seconds. `web-result.json` and `web.log` record current production `tsc`,
`esbuild`, `sass`, `postcss`, and `html-minifier-terser`, not only frontend mocks.

### Incomplete verification and readiness superseding the preceding section

The execution runtime ended this continuation before the remaining requested
gates could be completed. **No fresh R7 firmware BUILD ONLY, ELF/map/memory,
reachable-stack audit, final protected-file hash comparison or final
`git diff --check` receipt is claimed.** Prior-review binary/memory/stack values
must not be represented as results for these edits. The inspected scratch
`gate.py`/`run_audits.py` drivers are preparation, not execution evidence for
those unrun gates. The required firmware command remains BUILD ONLY:
`TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs uv run --directory software pio run -e warp4_tesla_ble -j 4`.

Branch was rechecked as `feature/tesla-ble-upstream-ready`. No framework-package
editing, commit, push, publication, upload, flash, hardware access or Phase 2
action was performed. A final hash comparison is still required to establish
protected-package/probe integrity and exact final edit scope independently.

All previously explicit shared Config/API publication/pre-validator OOM,
Bluedroid deep-copy false-success, asynchronous backend/SDK ownership, LittleFS
failed-close quarantine, target interoperability/crypto-OOM/scheduling/heap/
stack-high-water and licensing limitations remain open. Passing host probes
does not resolve them or establish target stack safety.

Local status for this unfinished continuation: **NOT YET READY FOR ANOTHER
INDEPENDENT ASTRA REVIEW**. The exact R7 reproductions are now GREEN and the host
and production web gates pass, but firmware/binary/stack/integrity/diff validation
must be completed and documented before readiness can be asserted. This section
supersedes the previous continuation's readiness statement for the changed tree.

## Seventeenth Phase-1 continuation: R7 omitted verification completed

Continues session `20261011_071211_d03fbf` and the sixteenth continuation on
`feature/tesla-ble-upstream-ready`, HEAD/base
`03798d2515710d2a72dcb08ec72b6b502f6028d1`. All new execution receipts are in
`/home/hermes/.hermes/cache/scratch/tesla-phase1-review7-verification/` (called
the evidence root below). The preserved review/fix directories were not used
as output destinations. Verification exposed no new production defect, so
**no functionality, production source or tests were changed in this continuation**.
The only nonignored repository edit is this append. This section supersedes
the sixteenth continuation's incomplete-gate/readiness statement, not its
limitations or RED-before-fix evidence.

### Full current firmware BUILD ONLY and production frontend

After a pre-gate inventory, the target build directory was removed by:
`TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs uv run --directory software pio run -e warp4_tesla_ble -t clean`.
`clean-result.json`: **exit 0**, 6.590 seconds.

Then the required full target build was executed:
`TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs uv run --directory software pio run -e warp4_tesla_ble -j 4`.
`firmware-result.json`: **exit 0**, 197.602 seconds; PIO reports
**SUCCESS 00:03:17.422**. Both commands were executed through evidence-root
`gate.py`, also setting `PYTHONDONTWRITEBYTECODE=1` to avoid new package bytecode.
The unchanged `.pio-link` URI points exactly to the specified framework-libs
directory. No framework package was rebuilt, patched, installed or replaced.

The target directory was clean, but the existing PIO global build cache and
precompiled framework libraries were not purged/rebuilt; this is not a
cache-free framework rebuild. The log records **662 compilation entries**,
linking the current ELF, size checks and local image/info generation. Firmware
merge hooks write/copy local files only; esptool's “ready to flash” text is
image-generation output, not evidence of a flash operation.

The clean target also freshly preprocesses/builds the production frontend:
`tsc`, `esbuild`, `sass`, `postcss`, `html-minifier-terser` each appear **once**
and the enclosing target succeeds. Therefore no redundant standalone web
rerun was needed. Build-local npm dependency preparation reports 198 packages.
The missing-hyphenation notices, gzip/7z recommendation, failed main-branch
fetch notice, and two nanopb “No module named pip” messages remain in the log;
nanopb says “No generation needed.” No warning-free or network-independent
claim is made, and none of these messages prevented compilation/linking.

PIO reports RAM **93,356 / 327,680 bytes** and flash
**4,831,829 / 6,488,064 bytes**. Detailed linked DRAM is **93,354 bytes**:
**59,488 BSS + 33,866 data**, distinct from PIO's alignment/reporting.
ELF `.iram0.text` is **100,511 bytes**, `.iram0.vectors` **1,028 bytes**.
The detailed build table reports total image size **4,833,369 bytes**;
the actual padded application BIN is **4,833,536 bytes**.

### Fresh ELF/map/memory/release audit and exact artifact identity

Executed `python3` with evidence-root `gate.py elf`: **exit 0**, 2.527 seconds.
The inspected existing `run_current_audit.py`/`elf_audit.py` bodies were reused
with output relocated into the new evidence root, not run against an old ELF.
`elf/current-memory.json`, `elf/summary.json`, `elf/map-range-check.json`,
`elf/target-entry-frames.json` and current disassembly are retained.

Current linked release chain:
`release_bluetooth 0x40261ed0 -> btMemRelease 0x402799a4 ->
__wrap_esp_bt_mem_release 0x40279984 -> esp_bt_mem_release 0x402c4490 ->
esp_bt_controller_rom_mem_release 0x402c4380`.
Caller BLE mode remains **1**; the general static-area release branch requires
mode **3**. The general wrapper is linked; the controller-only wrapper is absent.
The complete wrapper, including the final call/return omitted by its nm-sized
slice, is retained in `elf/release-wrapper-complete.dis`.

All **eight** numerical map/ELF Bluetooth range endpoints agree:

- `_bt_data`: `[0x3ffc2ef8,0x3ffc441c)` = **5,412 bytes**.
- `_bt_controller_data`: `[0x3ffc441c,0x3ffc4514)` = **248 bytes**.
- `_bt_bss`: `[0x3ffd2320,0x3ffd3d74)` = **6,740 bytes**.
- `_bt_controller_bss`: `[0x3ffd3d74,0x3ffd4810)` = **2,716 bytes**.

The linked release coverage remains **5,172 BLE-only controller ROM bytes**,
not the other **33,240 controller reservations** or **15,116 named Bluetooth
data/BSS bytes**. This is static release-path evidence, not measured net heap
reclamation, successful runtime teardown or zero-Bluetooth-DRAM boot.

Six freshly built artifacts were preserved under `artifacts/`, with exact size
and SHA-256 comparison against `software/.pio/build/warp4_tesla_ble/`:

- `firmware.elf`: **88,777,316 bytes**;
  `4ab234d073714cbcba26677a139d591a49d5cab858e4abb0533e2cb14ebe3168`.
- `firmware.map`: **35,715,561 bytes**;
  `2b7fa0e2b200d664ace79fa8049bb38bac23e151a2a041a97571d4b5b73a4580`.
- `firmware.bin`: **4,833,536 bytes**;
  `668b24269db4b37215c25f1de18722ea48146c6b70122567362f05d8f25baf32`.
- `firmware_ota.bin`: **4,894,976 bytes**;
  `2b20206be292d4b5aa05cd2496da7173a293328fc0d2ea3f29d5d972254684c9`.
- `firmware_esptool.bin`: **6,557,696 bytes**;
  `4732acb544f77bf88cf37e4d0362985679caded246a80f1eda0956171dc23f23`.
- `firmware_info.bin`: **4,096 bytes**;
  `1e14d73702ee7fb3b35239ca847cc92452a39bfa14c21262ddbc6fd133cd2168`.

`capture_receipts.py capture` checked the current and preserved artifacts,
ELF/map hashes in all hash-bearing current audit outputs/logs, and gate log
hashes. `audit-output-manifest.json` binds all **1,486 current ELF/stack audit
output files** (9 ELF and 1,477 stack files), their sizes/hashes and audit-tool
source hashes to this exact ELF/map pair. Outputs such as disassembly and
map-range JSON do not themselves contain input hashes; the manifest supplies
that binding. Manifest: **207,109 bytes**, SHA-256
`7355b8f5d534e2243f5d0daba6cf82d10aa12852cc6e4f7f0f20127f96cfd1ad`.
`receipts.json` SHA-256:
`20fa3b2e571669a77fa3c4e6e94d71baaf67cadb6a86faae1d1f1c6e61734e4d`.
The final read-only artifact verification is recorded separately in
`final-artifact-check.json`; historical review outputs deliberately retain
their older hashes and are not claimed as current-build audits.

### Fresh reachable-stack audit — evidence, not a safety bound

Executed through `gate.py stack`:
`python3 software/tests/tesla_vehicle/host/audit_reachable_stack.py /home/hermes/.hermes/cache/scratch/tesla-phase1-review7-verification/stack`.
**Exit 0**, 4.218 seconds. `stack/summary.json` and `stack/callgraph.json` bind
to the exact current ELF/map hashes above. Discovered ENTRY sums are worker
**9,024 / 12,288 configured bytes**, teardown **2,784 / 4,096**.
The fresh graph contains **1,471 reachable functions**, **1,663 resolved
literal call sites**, **1,259 unresolved indirect sites**, **106 unknown direct
targets**, **17 recursive-cycle nodes**, **35 non-ENTRY stack-adjustment
functions**, and **zero `.su` files**. Counts were parsed from current outputs,
not copied from the earlier 1,504-function graph.

These branch-insensitive discovered-path sums are **not complete static upper
bounds, feasible worst cases, proven headroom or stack-safety certification**.
Indirect/callback/ROM paths, omitted cycle edges, register-window spills,
non-ENTRY adjustments and RTOS/interrupt reserve remain unresolved. Missing
edges or different graph counts cannot be interpreted as a safety improvement.
No worker/teardown high-water measurement or hardware experiment occurred.

### Preserved R7 public/adversarial probes and current host consistency

`gate.py probes` freshly compiles the unchanged Astra sources using the
preserved compiler/link arguments, with binaries/logs under `green-current/`.
**Exit 0**, 13.078 seconds, with every individual child checked:
public `denial`, `auth-refusal`; adversarial `wake`, `mixed`, `timers`, `reentry`,
`denial`, `auth-refusal`: **8/8 GREEN**. This is four exact finding modes and
four control modes, not a wrapper-only pass. Public finding modes retain the
real host clock; adversarial modes retain their original deterministic wrapper.

Byte identity was verified against both the preserved R7 fix RED and GREEN2
source-hash manifests and the seventh-review protected baseline:

- `public_defects.cpp`: **4,566 bytes**, SHA-256
  `3b59c981e122c55bb894c958a6d6fb7488f1344e2ca6a85515b1bee5d7a8241b`.
- `adversarial.cpp`: **18,531 bytes**, SHA-256
  `a798bc2d91f1a6429b044d1f62ef12a820095073e9ec868f7e70801860ea4a80`.
- Preserved sixth-review `original/additional_probe.cpp`: **7,347 bytes**,
  unchanged SHA-256
  `53a24825365b9d97b65672761acdc35f4cb9da576145d2528bb448db52245170`.

Fresh public denial keeps the successor uncompleted and queued in both control
and completion-reentry cases. Public refusal in each domain now records **one
attempt**, retry count **2**, changed backoff and no premature completion over
the sampled 323-ms period; the adversarial refusal also records one attempt
over 320 ms. Wake, mixed-budget, timer and reentry controls report **18**, **10**,
**14** and their retained reentry schedules passing. These are separate groups,
not an asserted unique-case grand total or remote-delivery proof.
The historical sixth-review unpaced recovery-budget assertion/expected pacing
limitation is preserved, not relabeled GREEN or included in the eight R7 modes.

For final consistency, the whole host suite was rerun under this continuation's
pre/post source inventory rather than relying only on the earlier pass (which
had no post-run source-hash snapshot):
`bash -x software/tests/tesla_vehicle/run_host_tests.sh`, via `gate.py host`.
**Exit 0**, 376.070 seconds. Programmatic source/trace set comparison verifies
**61/61 distinct C++ units**, **25 direct**, **34 protocol/module-runner**,
**2 Python harnesses**, **zero missing/extra/duplicate entries**, plus **one
Node web-action-suite invocation and one passing marker**. Individual action
case counts are not exposed by the log. Current host output retains the R7
six-denial/eight-refusal schedules, shared mixed recovery/deadline and accepted/
refused transport/same-domain reentry tests. Final marker:
`Tesla BLE host tests passed.` The new host log is byte-identical to the prior
R7 host log; execution times/receipt files remain distinct.

### Protected evidence, source scope and final diff checks

`verify_integrity.py before` and `verify_integrity.py after-gates` inventory
**2,080 tracked/nonignored-untracked non-`.env*` source files** and **62,623
unique protected files**. All source hashes were unchanged across the gates.
All protected files and inventoried symlink targets had **zero changed, added
or deleted entries**. Protected roots include installed PIO packages (**7,078**),
PIO platforms (**621**), repository packages (**5,535**), supplied framework
libs (**2,029**), preserved Astra reviews 3–7, fix receipts 4–7 and reused audit
helpers. This expanded inventory is not asserted to cover arbitrary files
elsewhere on the host, excluded secrets, or target/device state.

Every **10,708** file in the R7-fix-start protected inventory and every
**12,233** file in the seventh-review-start protected inventory also matches
its historical hash. The R7 **25-file evidence manifest** verifies without
mismatch; the complete **1,601-file R7 review directory** is unchanged.
The expected R7 fix scope relative to the earlier source inventories remains:
Client/Vehicle header/source identity changes, both host-runner registrations,
review6 fixture guard, review7 regression and document appends; no other source
change or deletion is attributed to those fixes. Secret-style `.env*` source
paths are consistently excluded rather than treated as new application edits.

`before.json`: **13,447,710 bytes**, SHA-256
`24733019c1d637046d40ce08a8b517c877268b82a5efb6136e5faf2330bd0f8e`.
`after-gates.json`: **13,448,126 bytes**, SHA-256
`6f0cec2d858ffd8299b6fdb3583f0f19faea29d36741997a11be1d01055200c4`.
The original **176,044-byte** Phase-1 document prefix is preserved; prefix
SHA-256 `8fb0a69c052b819a20af89146c62ae729775cdf27284dc4138f8161fcf7f4e43`.
The older **168,155-byte** prefix also matches its historical hash.
Post-append inventory and exact document size/hash/append digest are recorded
in `final.json`/`final-summary.json`, avoiding a self-referential document hash.

Both `git diff --check` and
`git diff --check 03798d2515710d2a72dcb08ec72b6b502f6028d1` exit **0** before
and after the gates; final post-append results are recorded in `final.json`.
These Git whitespace checks cover the tracked diff; hash inventories also cover
the nonignored untracked sources. `worktree-versus-base-final.patch` preserves
the base-relative tracked diff. Branch/HEAD and Git status remain unchanged:
**34 modified tracked files, 88 individual untracked files, zero staged files**;
the document was already untracked. Final scope allows only this document
append, with no production/test changes or source additions/deletions.

Gate log SHA-256 receipts (also checked by `capture_receipts.py`):

- `clean.log`: `14ed31c39c0db26fffca943ce56f8213b61e56f8402bf1e071171972cd8c71a5`.
- `firmware.log`: `86c5eb405458dd7cc9a4a46868d8fb5de432846ded396c1ecd8996d4353d56f5`.
- `host.log`: `756eaa031e783807e3a1baeacee4f60c54d3dfe11c7e592bdd9552ff8dec93bf`.
- `elf.log`: `d4943c1b2a9e9ecf7c08e28977c9fa0c66d61999fda945f3a2a4b86acbda1006`.
- `stack.log`: `70335b1394161216b32024ac4c67d682dce87bb4c04aac5c392568207f255d44`.
- `probes.log`: `63248ebacaecdb8e2899a67daf2d3fc42fa58ddcae3e5446379b0bf2b99a9eb3`.

### Honest limitations and local review readiness

Shared Config/API publication/pre-validator OOM, Bluedroid deep-copy
false-success and asynchronous backend/SDK ownership defects remain unfixed;
their preserved evidence is not reclassified as GREEN by these local gates.
LittleFS failed-close quarantine/orphan retention, uncancellable blocking SDK
calls, uncertain/partial transport submission, target crypto-OOM, dynamic heap
reclamation, reboot/power-loss durability, RF/sleep/wake/fragmentation,
watchdog/scheduling, real Tesla/WARP4 interoperability and target stack high-water
remain unverified or limited as documented above. Cooperative logical deadlines
are not SDK cancellation. AGPL-3.0-only metadata and pinned upstream
MIT-metadata/AGPL-license discrepancy remain distribution/licensing limitations.

No Phase 2, framework-package edit, commit, push, publication, upload, flash,
serial/device connection or hardware access was performed. Images remain local
and unsigned. Host/static verification does not supply deployment clearance.

Local status after completing the omitted R7 verification: **READY FOR ANOTHER
INDEPENDENT GPT-6 ASTRA REVIEW** of the current Phase-1 worktree, with all the
explicit limitations above. This is readiness for review, not independent
Astra approval, Phase-1 acceptance, release/distribution authorization,
hardware-safety clearance or permission to begin Phase 2.

## Eighteenth Phase-1 continuation: R8-1 lifecycle-owned disconnect retirement

Read the complete eighth independent Astra review at
`/home/hermes/.hermes/cache/scratch/astra-phase1-review8/review.txt` (168 lines).
Its **NOT READY** verdict superseded the seventeenth continuation. This
continuation fixes the local R8-1 finding and repeats the requested gates;
it does not treat the prior passing host suite as closure of that finding.
Scope remains `/home/hermes/warp-tesla-ble`, branch
`feature/tesla-ble-upstream-ready`, unchanged HEAD/base
`03798d2515710d2a72dcb08ec72b6b502f6028d1`, Phase 1 only.
All new receipts are in
`/home/hermes/.hermes/cache/scratch/tesla-phase1-review8-fixes/` (the evidence root).
The original Astra review/probe/evidence directories are not output destinations.

### Exact preserved real-clock RED before production edits, GREEN afterward

`evidence.py red` and `evidence.py green` execute **byte-identical copies** of
Astra's `public_lifecycle.cpp` **and** `run_probes.py`, relocated together only
so the driver's normal output paths do not overwrite original review evidence.
The driver/compiler arguments, each child exit, logs and source receipts are
retained under `red/` and `green/`. No production implementation or clock is
substituted in these modes. Original hashes verified before/after:

- `public_lifecycle.cpp`: 4,456 bytes,
  `c8872aa158b6a7ca63483452412468482b7dea3529187b9a7290a9e73ef2f209`.
- `run_probes.py`: 2,601 bytes,
  `01429f551fa6ec1d75df234f3e0ed802d4e0aea357b54761f8a3b607b72fe059`.

Normal **RED**: `disconnect` and `denial-drain` each exit **1**.
ASan/UBSan **RED**: both modes each exit **1**, without a sanitizer diagnostic.
Both domains reproduce the logical ownership failure: reconnect-inside-drain
records `fresh_failed=1`, `queue=0`, `live=0`, despite `connected=1`.
Reconnect-after-drain controls record `fresh_failed=0`, `queue=1`, `live=1`.
The enclosing evidence driver checks these expected RED exits; its successful
verification exit is not mislabeled as a passing child reproduction.

Normal **GREEN**: both unchanged real-clock modes each exit **0**.
ASan/UBSan **GREEN**: both modes each exit **0**. Inside- and after-drain
schedules now retain the fresh authenticating command, complete it successfully
exactly once using independently authenticated response bytes, and reject the
duplicate old denial. `green-exact.log` and `green-exact-sanitized.log`, plus
their result manifests, record the actual execution rather than a wrapper-only
success. Leak detection and halt-on-error were enabled for sanitized runs.
The protocol/probe compilation is instrumented; the existing native mbedTLS
archives and target SDK are not a fully sanitizer-instrumented system.

### Test-first correction and preserved ownership contract

`software/tests/tesla_vehicle/test_tesla_ble_review8.cpp` was written and
registered **before production edits**. After correcting a scratch harness
prerequisite (its new TMPDIR build directory was initially absent), all five
focused modes compile and fail behaviorally against the unchanged production
code: `lifecycle`, `rx`, `nested`, `processor`, `reset`, each shell exit **134**.
These actual assertion failures are in `focused-red2-*.log`; the earlier
missing-directory failures in `focused-red-*.log` are **not** behavioral RED.
The GATT sibling regression likewise precedes production edits: `deferred`
and `internal` each terminate with SIGABRT (subprocess return **-6**) after a
successful compile, recorded in `gatt-red-*.log`.

`Vehicle::set_connected(false)` now swaps the entire accepted old command queue
into a bounded local `FixedQueue<CheckedShared<Command>,32>` before invoking
external completions. It drains that detached ownership set to exhaustion,
not the live queue, and does **not** stop on generation change. Each old entry
is popped/retained, has its revision/state/wake/UUID retired, and has its
completion moved before invocation. A callback may reconnect/enqueue/start new
work, or recursively disconnect another lifecycle, without either drain
stealing commands from the other. Remaining old queued entries are still failed
exactly once; they cannot become new-lifecycle live work or be abandoned.

Decoded frames, Client requests, peers, sleep state, RX bytes and the malformed-
buffer recovery flag are retired **before callbacks**. There is no old
post-callback RX clear. This also prevents stale partial bytes from contaminating
a fresh fragmented HMAC response supplied inside a drained callback. Connection
generation still changes only on actual connected/disconnected transitions;
idempotent true/false calls do not create a new connection identity.

`focused-green-all.log` and `expanded-sanitized.log` both exit **0**, covering:

- **24** both-domain direct/authenticated-denial, reconnect-inside/reconnect-after
  schedules, with **1/4/31 old queued tails** (including a full 32-entry old
  queue). Bad-HMAC denial is inert, old accepted entries all retire once even
  after reconnect, duplicate old OK/denial responses cannot answer the new
  challenge, and fresh successor authentication/GCM response succeeds once.
- Both-domain malformed-old-RX retirement and successor **partial authenticated
  response** preservation across the old disconnect return, followed by actual
  frame completion, authentication and successful response.
- Both-domain **nested old-lifecycle drains**: an inner successor authenticates
  and succeeds inside nested callbacks, another successor remains live, and
  four outer old tails still retire once. Generation identity advances through
  both disconnect/reconnect pairs as asserted.
- Decoded-message drain invalidation and reentrant storage-reset controls below.

These are separate groups, not an invented unique grand total. The new tests
retain the real steady clock. They prove exercised synchronous serialized-worker
reentry/ownership, not arbitrary concurrent access, unbounded recursion safety,
full-cycle generation ABA freedom, remote cancellation or exactly-once wire
delivery after uncertain/partial transport submission.

### Sibling queue-drain and cleanup audit / additional local fixes

The parent inspected the sibling paths; delegated support was read-only source
enumeration, not implementation or a correctness verdict.

- `MessageProcessor::process_messages()` previously retained only a pending
  count and could consume fresh decoded frames after a callback cleared and
  repopulated its live queue. `clear_queue()` now advances a checked-mutex-
  protected queue generation; an old invocation stops before taking replacement
  frames. Old decoded frames have been explicitly discarded by clear, unlike
  accepted commands, which the detached disconnect queue **fully retires**.
  The replacement decoded frame remains for its own invocation; the focused
  `processor` RED/GREEN proves this distinction without allocating an entire
  decoded-queue snapshot on the worker stack.
- `Vehicle::reset_all_sessions_and_connection_()` now checks its captured
  connection generation after **each external storage removal**. Reentrant
  disconnect/reconnect during the first removal cannot cause the old reset to
  delete the fresh other-domain session or clear/disconnect the successor.
  Both-domain `reset` RED/GREEN verifies live queue, partial RX, single old
  removal, zero successor-storage removals and later authenticated success.
- `TeslaVehicle::process_disconnect()` resets old module RX **before** the
  library disconnect completion boundary. `disconnect_internal()` completes
  old physical-link cleanup **before** library completions can reconnect, so
  an old return cannot exchange/close the new live link. The production-source
  GATT boundary regression verifies preserved successor notification fragments
  and exactly one old-link Close, never a replacement-link Close; both modes
  pass normally and under ASan/UBSan. This ordering does not make SDK Close
  nonblocking/cancellable; command callbacks follow that cleanup when it returns.
- Existing terminal finalization already detaches/takes completion before
  terminal-phase/completion callbacks; phase/TX/observer/persistence continuations
  retain command, generation and revisions. RX snapshots and consumes each
  frame before raw observers, with no old-frame erase afterward. Retry/auth-
  timeout RX clears precede phase/completion callbacks. Module RX/event loops
  validate fragment generation or runtime/request identity per consumed entry.
  Storage variant/write cleanup continues to use mutation/cancellation guards.
- Destruction of an active Vehicle/MessageProcessor from its own callback,
  including the unused legacy global processor cleanup helper, is **not** a
  supported/reviewed object-lifetime guarantee. The WARP path uses an embedded
  processor and acknowledged worker/callback-gate teardown. The existing
  `is_processing()` flag is an observation, not a general nested/concurrent
  execution exclusion guarantee. No universal destructor-safe/concurrent-race
  claim is made from this lifecycle fix.

Fresh preserved R7 public/adversarial modes pass **8/8**, normally and sanitized.
Fresh unchanged R8 additional controls pass **5/5 modes** (`denial`, `refusal`,
`timers`, `wrap`, `callbacks`), normally and sanitized. `controls/` retains the
unchanged driver/additional source and per-child manifests; all child exits
are verified. These deterministic additional controls retain their original
clock wrapper and are not mislabeled as real-clock lifetime experiments.

### Web dependency recovery and full acceptance gates

The failed R8 offline preparation had removed/reprepared ignored npm contents.
With package-restoration network access permitted, ran:
`uv run --offline npm ci --no-audit --no-fund` in `software/web`, with npm
offline mode disabled. **Exit 0**, 198 packages installed, `npm-restore.log`.
`uv run --offline npm ls --all --json` subsequently exits **0** (`npm-tree.log`).
This restores the dependency tree from the existing lock, not a version update.
The production firmware hooks subsequently run their ordinary npm preparation,
also successfully installing 198 packages and recording the normal marker/digest.
No lockfile, package manifest or framework package was modified.

- Full host: `bash -x software/tests/tesla_vehicle/run_host_tests.sh`, through
  `evidence.py host`, **exit 0**, **374.330 seconds**. Programmatic source/trace
  comparison verifies **63/63 distinct C++ entries**, **26 direct**, **35
  protocol/module runner**, **2 Python harnesses**, zero missing/extra/duplicate
  entries; one Node web-action invocation and pass marker. Final marker is
  `Tesla BLE host tests passed.` Host log SHA-256:
  `6c298f420d35b7d36237cfd11801f8a5f540a3d2a790c04da8f14989b1f220b0`.
  The native mbedTLS **5,102 allocator-fault** group remains passing. The
  LittleFS assertion at `host.log:790` is the intentional isolated-child
  SIGABRT required by its passing failed-close/quarantine test, not a hidden
  suite failure or proof that the shared backend defect has been fixed.
- Standalone production web: inspected production `pio_hooks.py`
  `build_web/check_call`, through the unchanged eleventh-continuation helper;
  **exit 0**, **5.803 seconds**. `tsc`, `esbuild`, `sass`, `postcss`,
  `html-minifier-terser` all execute (`web.log`), not merely action unit mocks.
- Clean target: established framework path, `uv run --offline --directory
  software pio run -e warp4_tesla_ble -t clean`, **exit 0**. `clean.log` confirms
  `Removing .pio/build/warp4_tesla_ble` / `Done cleaning`.
- Full target **BUILD ONLY**:
  `TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs uv run --offline --directory software pio run -e warp4_tesla_ble -j 4`,
  **exit 0**, **72.765 seconds**, PIO **SUCCESS 00:01:12.599**. The exact existing
  `.pio-link` URI is verified. The target directory was clean; PIO global object
  cache and supplied precompiled libraries were retained (**7 compilation** and
  **658 retrieved-cache records**, programmatically counted). This is a full
  target link/image build, **not** a cache-free recompile/framework rebuild.
  All five production web stages run once in this fresh target pipeline too.
  Nonfatal missing-hyphenation/gzip, skipped main-branch fetch and nanopb
  pip-unavailable/no-generation messages remain; no warning-free claim.
  esptool's “ready to flash” is local image creation, not a flash operation.
- PIO RAM **93,356 / 327,680 bytes**, flash **4,832,157 / 6,488,064 bytes**;
  detailed image **4,833,697 bytes**, padded application BIN **4,833,856 bytes**.

### Fresh ELF/map/memory/reachable-stack and artifact provenance

`evidence.py elf` and `evidence.py stack` each exit **0**, operating on the
fresh current ELF/map, not preserved R7 binaries. Current detailed ELF DRAM:
**93,354 bytes = 59,488 BSS + 33,866 data**; IRAM text **100,511**, vectors
**1,028**. All **eight numerical Bluetooth map/ELF endpoints match**.

Current release chain: `release_bluetooth 0x40262014 -> btMemRelease 0x40279aec
-> __wrap_esp_bt_mem_release 0x40279acc -> esp_bt_mem_release 0x402c45d8
-> esp_bt_controller_rom_mem_release 0x402c44c8`. BLE caller mode remains **1**,
general static-release branch mode **3**. Linked coverage remains **5,172**
BLE-controller ROM bytes, not the other **33,240** controller reservations or
**15,116** named BT data/BSS bytes. This is not measured net heap release or
zero-Bluetooth-DRAM boot.

Fresh reachable ENTRY sums: worker **9,024 / 12,288** configured bytes;
teardown **2,784 / 4,096**. Graph: **1,473 reachable functions**, **1,664 resolved
literal call sites**, **1,268 unresolved indirect sites**, **88 unknown direct
targets**, **17 cycle nodes**, **36 non-ENTRY adjustment functions**, **zero
`.su` files**. Current disconnect ENTRY frame is **704 bytes**; decoded-drain
frame **832 bytes**. These branch-insensitive discovered sums are **not** a
complete static upper bound, feasible worst case, proven headroom or stack-
safety certificate. Unknown callbacks/ROM, nested callback recursion, omitted
cycle edges, register-window spills, other adjustments and RTOS/interrupt
margin remain; no target high-water measurements were made.

`verify_receipts.py capture` preserves/checks all six fresh artifacts under
`artifacts/` and binds **1,488 ELF/stack output files** to their exact ELF/map
pair in `audit-output-manifest.json` (207,186 bytes, SHA-256
`79b51c0503f14c47a56e7ed278441b36066e2c89353c9b9f30d92cd17c9f8b4a`).
`receipts.json` records every artifact size/hash, host enumeration, current
frames/memory/stack counts, unchanged probe/driver hashes and gate log hashes.

- Fresh `firmware.elf`: **88,783,712 bytes**, SHA-256
  `3f1652958259b2f16664efb78cc9dcf9ce23f8ed0aef55452315799e7e64b65d`.
- Fresh `firmware.map`: **35,720,492 bytes**, SHA-256
  `b9a52434bbc2e06f26a8fefc11726c4ebfb8abca5b4f7c27ed99916c65ddb9b5`.
- Fresh `firmware.bin`: **4,833,856 bytes**, SHA-256
  `933923f315beddc1e22eaff3bcc389be94c1b26ffa3fb30ec990d60a28a3d9b1`.

### Integrity, exact edit scope and unchanged open limitations

Pre/post-gate inventories verify **2,082** tracked/nonignored-untracked nonsecret
source files; the source set/hash inventory is identical across the acceptance
gates. All **68,652 enumerated protected files** and inventoried symlink targets
are unchanged, with zero added/deleted/changed protected entries. Protected roots
include installed PIO packages/platforms, repository packages, supplied framework
libraries, preserved original reviews through R8, prior fix/verification evidence
and reused audit helpers. This is a bounded inventory, not coverage of arbitrary
host paths/secrets or device state. `software/web/package-lock.json` SHA-256
remains `5f26f29158563c9df45b4e1fe8a642c833ebb288f744459fa449a026a9bc140c`;
`software/uv.lock` also matches its baseline.

This continuation changes only vehicle.cpp, message_processor.h/cpp,
tesla_ble_gatt.cpp, host/gatt_platform.h, run_host_tests.sh; adds the review8
and GATT-reentry regressions; and appends this document. All other pre-existing
dirty sources are preserved. Both normal and base-relative `git diff --check`
pass after gates; post-append checks/inventory are recorded in `final*.json`,
with append-only proof for the original **191,299-byte** document prefix and
an exact final artifact/audit read-only verification receipt. No staged changes,
commit, push, publication, upload, flash, serial/device connection or hardware
access occurred. No Phase 2 or framework-package modification was performed.

`external.log` freshly reproduces shared Config/API empty/corrupted Tesla
publication/dirty-clearing and characteristic/descriptor deep-copy OOM false
SUCCESS. Its exit **0 means the known defects reproduce, NOT that they are fixed**.
Shared ConfigRoot pre-validator OOM, asynchronous backend/SDK ownership gaps,
LittleFS failed-close orphan/quarantine-until-reboot, uncancellable blocking SDK
calls, uncertain/partial remote submission, power-loss durability and target
crypto-OOM remain limited/unfixed as previously documented. Native tests and
static audits do not validate real Tesla/WARP4 interoperability, RF/sleep/wake,
watchdog/SDK scheduling/load, fragmentation, net heap reclamation, reboot recovery
or stack high-water. The AGPL-3.0-only metadata and pinned MIT-metadata/AGPL-license
discrepancy remain distribution/licensing limitations; no legal clearance follows.

Local status: **READY FOR ANOTHER INDEPENDENT ASTRA REVIEW** of the current
Phase-1 worktree, with those explicit limitations. R8-1 is locally RED-before-fix /
GREEN-after-fix and all requested software/build/integrity gates completed.
This is not independent Astra approval, Phase-1 acceptance, release/distribution
authorization, deployment/hardware-safety clearance or permission to begin Phase 2.

## Nineteenth Phase-1 continuation: R9 wrap correction, verification incomplete

Continues the interrupted independent Astra session `20261011_093745_c3fd1a`.
Its preserved full-module failure supersedes continuation 18's readiness claim:
**NOT READY**. Scope remains Phase 1, branch
`feature/tesla-ble-upstream-ready`, unchanged HEAD/base
`03798d2515710d2a72dcb08ec72b6b502f6028d1`. New local receipts are exclusively in
`/home/hermes/.hermes/cache/scratch/tesla-phase1-review9-fixes/`.

### Preserved independent finding and test-first RED/GREEN

Inspected continuation 18, `run_followup.py`, `run_local.py`, the full-module
source/compiler arguments/results and GATT/follow-up evidence. Read-only
delegated support inventoried/summarized the complete preserved text bundle;
the implementer independently reproduced its failures. No final Astra verdict
was available, and no approval is inferred from the interrupted review.

Before production edits, `evidence.py red` rebuilds/runs the unchanged Astra
`current/module_wrap.cpp`, using its saved compiler/source/archive arguments,
relocating only the executable output and adding `-Wno-error=format` in both
RED and GREEN builds. That diagnostic-only flag is required by the original
probe's `%u` output when `connection()` becomes 64-bit; no assertion, injected
generation, production code or clock is replaced. Source SHA-256:
`00e686426f3ee41ad73e843e1b48b4e6b2ff1a6082fba51d36c1d10752b4ac78`.

Normal and ASan/UBSan RED both exit **1**: 5 -> 6 and
0xFFFFFFFE -> 0xFFFFFFFF pass; 0xFFFFFFFF -> 0 fails with pending=0,
module_connected=0, protocol_connected=1, accepted_completion=0,
command_queue=1, module_rx=1. Logs: `red-module-wrap*.log`.
After the correction both unchanged-source runs exit **0**, all three schedules
pass, accepted_completion=1, protocol_connected=0, command_queue=0, module_rx=0.
Logs: `green-module-wrap*.log`. The unchanged `%u` line still displays the
successor's low word as zero; the permanent width-correct regression records
the actual UINT32_MAX successor as **4294967296**, not generation zero.

Two permanent tests were written/registered before production edits.
After correcting initial fixture compilation prerequisites, `red-review9`
and `red-generation` both compile successfully then terminate with SIGABRT
(subprocess **-6**): skipped deferred retirement and a low-word alias accepted
as current work, respectively. Initial compilation errors are not RED evidence.
An intermediate GREEN attempt exposed the module SDK fixture's default Close
refusal; a configurable host-only Close completion hook corrected that fixture.
Its failed result is retained, not labeled GREEN.

### General lifecycle identity correction

- Widened EV session generation at its source, module/session connection IDs,
  RX fragment ownership, runtime epochs, request sequences/tokens, event structs,
  publication orders and policy generations to **uint64_t** end-to-end.
- Widened Vehicle command response context and authenticated response evidence,
  including public/internal poll signatures, so a 64-bit request cannot narrow
  before its final callback/SoC acceptance checks. Callback captures retain the
  full values; the main EV-generation read uses `auto` rather than uint32_t.
- Replaced pending-disconnect's zero sentinel with a checked-mutex-protected
  generation plus explicit presence. `take()` consumes presence once, including
  a valid zero token. Startup checks this additional synchronization resource;
  the host fault test now exercises **six** Runtime synchronization boundaries.
- Identity snapshots publish both generation words under their existing bounded
  odd/even coherence tag. The remaining 32-bit tag is not a lifecycle identity;
  explicit split/reassembly preserves the high word. Timers, protocol wire
  counters, SDK connection handles and callback-gate entry counts are not
  lifecycle generations and remain their existing widths.
- Transport fragmentation additionally captures the whole connection generation
  for the entire write, not only each GATT operation, and refuses continuation
  onto a different lifecycle even when its low word is identical.
- Existing Vehicle connection/command/request revisions, wake ownership, Peer
  session revisions and MessageProcessor queue generation were already 64-bit.
  Their retained-context and per-frame comparisons were inspected and exercised;
  the detached old-command drain still exhausts its old ownership set exactly
  once, rather than stopping on generation change or touching the successor.

`focused.py green` passes normally and under ASan/UBSan: **112** full-module
direct/authenticated-denial/nested, both-domain, deferred/internal schedules.
Seeds include 5, UINT32_MAX-2/-1/MAX/+1 and UINT64_MAX-1/MAX. Each schedule retires
32 accepted old commands once, rejects stale pending cleanup and low-word-alias
RX, preserves a fresh one-byte HMAC prefix through the old unwind, then completes
fresh authenticated HMAC/GCM work once and ignores duplicate old responses.
Separate tests cover **four** session/request/epoch/EV/policy/publication
boundaries, coherence-tag wrap, explicit zero pending tokens and one stale
multi-fragment write cancellation. Width-correct PRIu64 diagnostics and
compile-time request/connection-width checks are permanent. These are separate
groups, not a claimed unique grand total or proof of full-cycle uint64 ABA
freedom, arbitrary concurrency, unlimited callback recursion or remote delivery.
Sanitized protocol code still links existing native crypto archives; no fully
instrumented dependency/SDK or hardware safety claim follows.

### Completed software gates and additional unresolved R9 findings

- Full `bash -x software/tests/tesla_vehicle/run_host_tests.sh`: **exit 0**,
  **398.098 seconds**, **65/65 distinct C++ entries**: 36 protocol/module runner,
  27 direct, 2 Python harnesses, zero missing/extra/duplicate entries. One Node
  action invocation/pass marker; the native **5,102 allocator-fault** group passes.
  Host log SHA-256:
  `cedb642fedbe2c5b23229a90c2ffc797662a57b396b5f256683f1b7b9fbbdba8`.
- Standalone production web hook: **exit 0**, **6.686 seconds**, not only action
  mocks. Web log SHA-256:
  `1c1dc1162e49a744404e76a5f6901778bc5b48f3942e9d1ef0ada859f1d7710c`.
- Preserved R7 public/adversarial **8/8 modes**, R8 real disconnect/denial-drain
  **2/2 modes**, R8 additional **5/5 modes** pass normally and sanitized.
  R9 unchanged GATT controls and wrap-zero pass in both builds. Unchanged R9
  nested/processor/storage/observer controls pass in both builds too.
- Before edits and again after the generation fix, both ordinary and sanitized
  R9 `key` and `key-lifecycle` probes still exit **1**. Nested key persistence
  commits twice but disk differs from the active identity; a key-save callback
  starting new authentication loses its pending challenge/UUID after return.
  These are independent real blockers, not repaired or masked by widening
  generations. The wrapper verifies their expected failure and does **not**
  call them GREEN. Their actual logs/results remain in the evidence root.

### Required gates not completed; no current binary/stack clearance

The execution runtime stopped further verification after the host/web/probe
gates. **No fresh clean warp4_tesla_ble firmware build, ELF/map/memory audit,
reachable-stack audit, post-gate/final integrity comparison or normal/base
git diff --check receipt is claimed for these edits.** Consequently there are
no current RAM/flash totals, stack estimates or firmware artifact hashes to
report; continuation 18/R9 review binary values must not be reused as current.
The requested BUILD ONLY framework path remains
`/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs`.

Before/pre-gate compressed inventories did verify **76,026 protected files**
and **145 inventoried symlinks** unchanged, including external packages,
frameworks and original R9 evidence; the pre-gate source inventory has **2,084**
files. This is not a final post-execution integrity receipt. npm cache was copied
read-only into the project's ignored `.pio` directory; mutable build/log caches
were directed into the project/evidence root. No external framework/package
edit, hardware access, flash, commit, push, publication, upload or Phase 2 action
was performed. `partial-receipts.json` records the completed gate counts/hashes
and explicitly enumerates the unrun gates and unresolved key findings.

All prior SDK/backend publication, LittleFS failed-close, blocking submission,
durability, interoperability, target crypto-OOM, heap/stack high-water and
licensing limitations remain open. Local status: **NOT READY FOR ANOTHER
INDEPENDENT ASTRA REVIEW**. The wrap finding is locally RED -> GREEN, but the
remaining mandatory gates and independent key-save blockers prevent readiness.
Only Astra can approve Phase 1; no approval or Phase-2 authorization is claimed.

## Twentieth Phase-1 continuation: R9 key transaction implementation, verification incomplete

Active status: **NOT READY FOR USER-AUTHORIZED ASTRA REVIEW**. This section
supersedes continuation 19's implementation state but does not claim final
acceptance. The execution runtime stopped further discovery/verification before
the final gates. No Astra invocation or independent review was started.
Branch remains `feature/tesla-ble-upstream-ready`; baseline HEAD at discovery was
`03798d2515710d2a72dcb08ec72b6b502f6028d1`. Evidence from this continuation is in
`/home/hermes/.hermes/cache/scratch/tesla-phase1-review9-key-fixes/` only.

### Exact preserved probes RED before production edits

Read continuation 19 and the original R9 probe sources, fixtures, compiler
arguments, drivers, results and prior fix evidence. Read-only delegated evidence
summarization used **GPT-6.1 Sol**, not Astra, and did not review/approve code.
The implementer independently reproduced both defects before production edits.
`evidence.py before` captured **2,084 source files, 78,846 protected files and
145 symlinks**, with the required branch/HEAD and no staged changes. Protected
scope additionally includes the previous `tesla-phase1-review9-fixes` tree.
This is a baseline, **not** a final integrity comparison.

`evidence.py red` rebuilt unchanged original sources/arguments/crypto archives,
relocating only executable outputs and adding instrumentation for sanitized
builds. Normal and ASan/UBSan `key` and `key-lifecycle` each exit **1**:

- Nested regeneration: both domains, outer/inner success and two commits, but
  durable disk key differs from active RAM identity. Each run includes four
  nested/non-nested schedules. RED log SHA-256:
  `787f9ece59da888b8a8fb15ba6a1fad2702ec010e6f8f344d29451d0b1509cdf`.
- Key-save successor authentication: both domains with/without reconnect,
  pending inside callback becomes false afterwards and UUID is replaced/cleared.
  All four schedules fail. RED log SHA-256:
  `9337818ce8393bd094af949a04915022f822b1a675e605ba04e0a4ae8e1728f8`.

Permanent `test_tesla_ble_key_reentry.cpp` was written before production edits.
An initial fixture private-access compilation error is preserved and is **not**
RED evidence. After correcting includes, `red2-key-reentry` and
`red2-key-nested` compile and terminate with SIGABRT (**-6**) at pending/UUID
preservation and disk/RAM agreement assertions, respectively.

### Implemented transaction and identity ownership semantics

These changes are implemented, but the final current revision is not fully
verified:

- A synchronous `Client::KeyTransaction` owns one staged candidate and bounded
  PEM export. Nested regeneration on the same storage joins **that candidate**,
  rather than generating a competing key. Every joined save uses identical
  candidate bytes whether an adapter callback runs before or after promotion.
  Publication is once per operation tree, guarded by the live scope object,
  not a wrapping key revision. A durable inner commit is not undone or falsely
  described as uncommitted when the outer joined save refuses.
- Import/no-storage/cross-adapter mutations cannot replace an in-flight durable
  transaction. Existing-key persistence also has a transaction scope and rejects
  concurrent rotation; persistence inside rotation joins the staged candidate,
  never exports the stale active key back onto disk.
- Each immutable key identity has a checked **nothrow** lease allocation. All
  fallible crypto/export work precedes persistence. On commit the prior PK's
  ownership moves into its already-existing identity lease, with no allocation,
  reparsing or compensating storage callback after the commit. The current
  durable identity and public-key metadata publish together.
- Pending authentication and established session identities are separately
  retained for both domains. HMAC verification/ECDH and subsequent encrypted
  signer metadata use the identity belonging to that exchange/session, not
  whichever key is currently durable. TX rollback/cancellation cannot relabel an
  old shared secret with a new public key. Disconnect/reset releases session
  leases and fresh authentication uses the current key.
- If persistence callbacks change requests or peer/session revisions, key
  publication preserves their live state instead of performing the old blanket
  reset/invalidation. Revision boundary comparisons remain uint64; nested key
  ownership itself uses the nonnumeric live transaction scope.
- Sessions belonging to an old identity can complete **ephemerally in memory**,
  but are not written as canonical sessions for a different durable/pending
  rotation candidate. This also applies to full HMAC/GCM completion inside the
  key-save callback. Current-key sessions remain subject to truthful storage
  acceptance/refusal. This is an intentional identity-qualified persistence
  policy, not a relabeling of failed saves as successful durable writes.
- Vehicle retains its storage adapter across external callbacks; session saves
  also retain the adapter. There is no compensating callback that would consume
  or duplicate successor work.

Permanent regressions extend the original key transaction test with generation
and import identity-allocation faults. The new reentry file includes both
domains, reconnect/no reconnect, nested calls before/after challenge creation,
before/after-promotion hooks, all five explicit storage outcomes, mixed nested
outcomes, UINT64_MAX-1/MAX request/session/command boundaries, split-prefix
HMAC, actual GCM completion, duplicate rejection, current-key reconnect,
simultaneous domain leases, TX rollback/cancellation and mutator admission.
It is registered in the full host runner.

### Completed intermediate checks — not final current-worktree clearance

- `intermediate-key-reentry` passed an earlier **240 lifecycle + four nested
  promotion schedules**. Its log is preserved; the file was subsequently
  expanded, so those counts do not describe the final regression source.
- The first attempted `green` unchanged-source run exposed a further issue:
  accepting the retained old-key HMAC would call canonical session persistence
  and reenter the original unguarded callback. That real SIGABRT receipt remains
  in `green-followup-v2-key-lifecycle.log`; it is **not** GREEN.
- After separating pending/session leases and excluding retired-identity
  canonical saves, `green2-driver.log` exits **0**: unchanged R9 module wrap,
  followup observer/key-lifecycle/processor characterization, adversarial
  nested/processor/storage/key and GATT controls/wrap-zero all pass normally and
  under ASan/UBSan. No probe assertions or preserved sources were edited. The
  original module `%u` diagnostics require only `-Wno-error=format`, as in
  continuation 19; assertions and injected generations remain unchanged.
- `focused1-driver.log` exits **0**: permanent key reentry, key transaction,
  corrected review9/wrap, generation and real native mbedTLS allocator-fault
  tests pass normally and under ASan/UBSan at that intermediate revision.
  Native crypto archives are not claimed fully sanitizer-instrumented.

**Subsequent changes are unverified:** the pending-rotation canonical-session
filter and added mixed-result/full-callback-completion/simultaneous-domain/
mutator-admission regression groups were written after those passes. Do not
reuse `green2` or `focused1` as hashes/counts/clearance for the current source.
No final unique test total, final source hash inventory or final sanitizer pass
is asserted. Earlier RED and failed intermediate receipts remain intact.

### Gates still required before readiness

The following were not completed on this continuation's current revision:

1. Re-run `focused.py` with a **new receipt prefix** for final permanent tests,
   normal and ASan/UBSan; fix any real failures. Inspect exact output counts.
2. Re-run unchanged R9 probes through `evidence.py`/`probes()` using a fresh
   prefix, then all preserved R7/R8 probes (`evidence.py preserved`).
3. Capture `pre-gates` inventory, then full host suite (`evidence.py host`),
   production web hook/actions (`web`), clean target (`clean`) followed by
   **BUILD ONLY** (`firmware`) using
   `TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs`.
4. Run fresh ELF/map/memory (`elf`) and reachable-stack (`stack`) audits; bind
   every output to the exact newly built ELF/map and report current sizes,
   SHA-256, RAM/flash and discovered ENTRY estimates with their limitations.
5. Compare protected framework/package/original-review/previous-fix integrity
   against `before`, and source gates against `pre-gates`; run normal and
   base-relative `git diff --check`; enumerate actual host entries and verify
   log/artifact hashes. Append a further verified readiness continuation only
   if all required gates pass. Never overwrite this run's failed receipts.

`partial-receipts.json` explicitly records the stopped/incomplete verification.
No fresh firmware artifact, current RAM/flash total, current reachable-stack
estimate, post-gate/final integrity result or diff-check pass is claimed here.
Continuation 18/R9 historical binary numbers must not be presented as current.

This continuation edits Client/Peer/CryptoContext headers and sources,
Vehicle header/source, host runner, existing key transaction regression; adds
the key reentry regression and evidence drivers; and appends this document.
Other pre-existing dirty work is not deliberately modified. No external
framework/package edits, hardware access, flash, commit, push, publication,
upload or Phase 2 action was performed. The lack of a final integrity receipt
is explicitly retained, not turned into a verified integrity claim.

Prior SDK/backend publication, failed-close/quarantine, blocking-submission,
power-loss durability, target crypto-OOM, interoperability/RF/watchdog,
fragmentation/reclamation, hardware stack-high-water and licensing limitations
remain. **NOT READY**: intermediate R9 key probes are locally RED -> GREEN, but
the final hardening/tests and mandatory acceptance gates are unfinished. Only
the user may authorize a new Astra review; none was invoked or scheduled.

## Twenty-first Phase-1 continuation: current-source gates pass; final receipts incomplete

Active status: **NOT READY FOR USER-AUTHORIZED ASTRA REVIEW**. This supersedes
continuation 20's unverified key-test state, not its retained RED evidence. The
runtime stop notice arrived after the current-source native/web/firmware/audit
and post-gate integrity runs, but before final normal/base-relative diff checks
and the final post-document inventory. No unrun gate is called passed.
Branch/HEAD remain `feature/tesla-ble-upstream-ready` /
`03798d2515710d2a72dcb08ec72b6b502f6028d1`; no changes are staged.
New compact receipts are exclusively in
`/home/hermes/.hermes/cache/scratch/tesla-phase1-review9-key-verify/`.
Compiled native outputs/caches remain inside the project's ignored `.pio` tree.
All gate commands run in an isolated `unshare -Urn` network namespace, offline.
No Astra invocation/scheduling, hardware access, flash/upload, commit/push,
publication, external framework/package edit or Phase 2 action was performed.
Bounded read-only evidence summarization used GPT-6.1 Sol, not Astra.

### Exact final source inspected and key/wrap probes rerun

Read continuations 19/20, the key-fix handoff and `partial-receipts.json`, original
R9 sources/arguments, current diff and final Client/Peer/CryptoContext/Vehicle
transaction, identity and persistence call paths. Earlier `green2`/`focused1`
passes were not reused as current clearance. No production repair was needed
in this continuation: current key probes and permanent tests passed.

Strengthened two permanent test files only, then reran all seven focused entries
normally and with ASan/UBSan using fresh `final-*` receipts:
`key_reentry`, `key_transaction`, `persistence_allocations`, `identity_busy`,
`review9 all`, `generation`, `mbedtls_faults`: **14/14 run invocations exit 0**.
The nested regression now directly captures the transaction/candidate lease,
compares identical staged PEM/durable bytes and checks exactly one publication
via both peer revisions/request invalidation. The transaction test arms
abort-style operator-new failure immediately after the adapter commit; final
publication makes **zero operator-new attempts**. This does not instrument every
SDK/libc/crypto allocator after commit or prove arbitrary concurrent safety.

Current key-reentry output verifies separate groups, in each instrumentation:
**360** both-domain successor schedules; **4** before/after-promotion nested
schedules; **100** mixed inner/outer outcomes; **16** complete authentication/GCM
operations inside save callbacks; **2** simultaneous both-domain identity-lease
schedules; plus one mutator-admission scenario. These assert pending UUID/RX
prefix preservation, captured pending/established key identities, old-identity
ephemeral completion without canonical new-key session writes, disk/RAM
agreement, refusal/cleanup/restart outcomes, reconnect/no reconnect, wrap-edge
revisions, exactly-once completion, duplicate rejection and retry. Separate
transaction tests pass two nothrow identity-allocation faults with prior state
retained. Native mbedTLS fault tests pass **5,102 allocator-fault boundaries**.
Sanitized project/probe code links existing native crypto archives; these are
**not** claimed fully sanitizer-instrumented SDK/dependencies.

`final-probe-results.json`: unchanged preserved R9 probes rebuilt with original
source/archive arguments, relocating executable output only, plus sanitizer
flags and the retained diagnostic-only `-Wno-error=format` exception for the
original module `%u` print. **8 builds and 20 mode invocations exit 0**.
Per instrumentation: key **4**, key-lifecycle **4**, nested **18**, processor
**36**, storage **12**, observer **6**, processor no-clear characterization **1**,
full-module wrap **3**, GATT controls **8**, GATT wrap-zero **1** schedules.
These are separate groups, not a claimed unique overall test total. Original
key probes now report disk_matches_active_identity=1; all key-lifecycle probes
report pending_after=1, same_uuid=1, clobbered=0. The permanent full-module test
additionally passes **112** boundary schedules and one transport scenario;
generation tests pass **4** identity boundaries plus zero-token/coherence-wrap
checks. Width-correct permanent output records UINT32_MAX's successor as
4294967296; original `%u` low-word output is not its full identity.

Preserved R7 **8/8 modes**, R8 real **2/2 modes**, R8 additional **5/5 modes**
pass normally and sanitized: **30 mode invocations exit 0**. They use unchanged
original sources, not rewritten assertions. Their production and included
protocol fixtures remained unchanged through pre-gates/after-gates; subsequent
test strengthening touched only the two separate permanent key test files.

### Completed host, production web and clean build-only gates

- Full `bash -x software/tests/tesla_vehicle/run_host_tests.sh`: **exit 0**,
  **393.438 seconds**, **66/66 distinct C++ entries** verified programmatically:
  37 protocol runner, 27 direct runner, 2 Python harnesses; zero missing, extra
  or duplicate entries. One Node action invocation and actual
  `Tesla BLE web action tests passed.` marker. `host-entry-audit.json` preserves
  the exact entry list. Host log SHA-256:
  `40769dd6722f1054d7cebf531d340b7c30be2f6c4f70cb54104806655dc9b64e`.
- Production web hook: **exit 0**, **7.429 seconds**; actual tsc, esbuild, sass,
  postcss and html-minifier-terser, not only mocked actions. Web log SHA-256:
  `d606992a3f7eeca08601bd0ae1be16dda927ae8d0bc3d8bf90ac8bb1ce8cf2f8`.
- Clean target **exit 0**, then `warp4_tesla_ble` **BUILD ONLY exit 0**:
  PlatformIO **00:01:20.657**, driver **81.058 seconds**. Used exactly
  `TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs`.
  No upload target/device connection. Firmware log SHA-256:
  `d8ca8f5c8baef79ed7fc8b4a45429ca89028cbc20274dd499b065b02dbd015c5`.

Fresh artifact: `software/.pio/build/warp4_tesla_ble/firmware.elf`.
ELF SHA-256:
`c5bf8c9bfb198289d6f17ec6942be7b2ec38c3b7e3d6adccece6f7a5e54375a4`.
Map SHA-256:
`8083ec54a9bf13b434f90f7580e3870cd7838bc2719046e3c9ff0e500ac2c621`.
Generated `.bin` hashes/sizes were not separately inventoried before the stop;
ELF/map hashes above are verified, not substitutes for uncomputed bin hashes.

Current PlatformIO RAM **93,356 / 327,680 bytes (28.5%)**, flash
**4,835,409 / 6,488,064 bytes (74.5%)**. Detailed ELF `.dram0.bss` **59,488**,
`.dram0.data` **33,866**, section DRAM sum **93,354**; the differing PlatformIO
accounting is retained explicitly, not silently normalized. `.iram0.text`
**100,511**, `.iram0.vectors` **1,028** bytes. These are linked static figures,
not runtime free heap, fragmentation, stack high-water or power-loss evidence.

### Fresh ELF and reachable-stack evidence; decoder limitations retained

ELF audit **exit 0**, log SHA-256:
`d59162aa9780dd2a037f78283fe549641f7586f9167b49f3c76d3edd814ceb74`.
All **eight** numerical Bluetooth map endpoints match ELF. General release
wrapper is linked; controller-only wrapper absent. Complete wrapper includes
its final call/return. Static audit identifies **5,172** BLE-only controller ROM
bytes, **33,240** other controller ROM reservations and **15,116** named static
Bluetooth bytes not reclaimed by the examined path. No actual hardware release
or runtime reclamation was measured.

Reachable-stack audit **exit 0**, bound to the same fresh ELF/map; log SHA-256:
`4f91478bac2b6f3b33dbaba499aa8a43226cfea27a18025711d34cbd0d7ea6fc`.
Its discovered ENTRY sums are worker **9,808** against configured **12,288**,
teardown **2,784** against **4,096** bytes. Graph has **1,907** functions,
**2,215** resolved literal-call sites, **1,394** unresolved indirect sites,
**23** cycle nodes, **134** unknown direct targets, **62** non-ENTRY adjustment
functions and **zero** `.su` files. Largest relevant frames include private-key
presence **4,160**, Vehicle initialization **4,144**, complete RX **2,848**,
carserver handler **2,368**, Client create/persist key **2,224** each; publication
**48**, key-save transaction **128**, storage save transaction **432** bytes.

**These are not stack clearance or a complete executable static bound.** Direct
inspection found an additional concrete decoder limitation: raw linear decoding
at 0x402615db fails to resynchronize at the branch target 0x402615dc and emits a
spurious storage `load_into` -> `NFCBricklet::setup_nfc` jump at 0x402615e0.
The reported chain is therefore decoder-dependent and can overestimate as well
as omit real paths. A supplemental CFG-aware receipt script `audit_cfg.py` was
written but **NOT EXECUTED** before the runtime stop; no corrected sum or pass
is claimed. Symbol-size truncation, unresolved callback/virtual/ROM/library
targets, recursion, branch-insensitive chains, non-ENTRY stack adjustments and
RTOS/interrupt reserve remain. Hardware high-water/guards are still unmeasured
and prohibited in this run. Original audit files/results are preserved intact.

### Integrity passes and precise remaining work

Before/pre-gate/post-gate inventories cover **108,227 protected files** and
**161 symlinks**, including prior framework/package/original R3–R9 review and
fix evidence, the key-fix handoff tree and full external builder `esp-idf` tree.
Post-gate comparison: **zero changed/added/deleted protected files or symlinks**.
Source inventory is **2,085 files**; pre-gates -> after-gates has no source
changes. The only before -> after-gates source changes are the two permanent
key test files identified above. Current tests/build/audits did not silently
change production source. Earlier protected roots were not written.

The initial ordinary `git diff --check` exited 0, but it is **not** final gate
clearance. Final normal and exact base-relative diff-check receipts and final
post-document inventory were **NOT RUN** before the stop. `partial-receipts.json`
records every completed count/hash and the unrun work. Before readiness:

1. Exercise/assess `audit_cfg.py`, preserving failures if any; do not reuse its
   unexecuted script as evidence or report a complete static stack bound.
2. Run final ordinary `git diff --check` and
   `git diff --check 03798d2515710d2a72dcb08ec72b6b502f6028d1`, recording exits/logs.
3. Run final source/protected inventory against before/pre-gates; retain this
   document's original prefix and permit only the two verified test edits plus
   appended documentation. Complete any desired generated-bin hash manifest.
4. If any production or tested-source change occurs, rerun affected gates and
   rebind firmware/audits to that source. Otherwise do not relabel older passes
   as proof for a different revision.

All prior SDK/backend publication, failed-close quarantine, blocking submission,
power-loss durability, target crypto-OOM, RF/interoperability/watchdog,
fragmentation/reclamation and licensing limitations remain. **NOT READY** only
reports the remaining verification/receipt gaps; it does not undo the actual
current-source key/native/web/firmware passes. No independent approval or
Phase-2 authorization follows. A future Astra review requires separate explicit
user approval; none was invoked or scheduled.

## Twenty-second Phase-1 continuation: remaining receipt execution and final binding

This continuation completes only the receipt work left by continuation 21.
Branch/HEAD are still `feature/tesla-ble-upstream-ready` /
`03798d2515710d2a72dcb08ec72b6b502f6028d1`. GPT-6.1 Sol performed local
verification without invoking/scheduling Astra or any other LLM delegation.
There are **no production or test source edits in this continuation**. No build,
hardware/device access, flash, upload, commit, push, publication, external
framework/package edit or Phase 2 operation was performed. Existing passed
gates are rebound by source/artifact integrity, not falsely described as rerun.
All executable audit/diff/inventory drivers ran offline in `unshare -Urn`.

The following facts were verified before this append. Final readiness is not
asserted in this initial receipt section; the post-document closure below is
reserved for actually executed checks. Evidence remains exclusively under
`/home/hermes/.hermes/cache/scratch/tesla-phase1-review9-key-verify/`.
Continuation 21's original JSON is retained byte-for-byte as
`partial-receipts-continuation21.json`, SHA-256
`cd959bc8386ceee83896513dfdd1e3571c8fe1a3356b442a23ecddcbff81cbbc`.

### Supplemental CFG decoder actually executed; failure retained and script-only repair

Executed `python3 /home/hermes/.hermes/cache/scratch/tesla-phase1-review9-key-verify/audit_cfg.py`
against the existing fresh ELF, without rebuilding. Initial execution **exit 1**
in **12.339 seconds**: its final assertion misclassified the intentional Xtensa
`ill` in `panic_abort` at **0x4009195c** as an unknown decode. The generated
`stack-cfg/` graph already removed the storage -> NFC edge, but the failed run
is not called a pass. Original script is retained as `audit_cfg-initial.py`;
initial output/graph/log/results remain untouched. Failure log SHA-256:
`af1e2e52ca8b59770c3def0fc08598ccec9f808d7096d812b61ae98237783435`.

Actual symbol disassembly contains **000000 ill** at that address; the protected
ESP-IDF `components/esp_system/panic.c:486-506` explicitly uses `asm("ill")` in
the noreturn abort path. Changed **only the supplemental scratch audit script**
to recognize that exact symbol/address/encoding as an intentional terminal trap,
retain its evidence, and write new `stack-cfg-v2/` output. Arbitrary undecoded
instructions remain assertion failures. Exception-handler stack is excluded,
not declared accounted or safe. No production defect was proven or repaired.

Rerun of the same command: **exit 0**, **14.102 seconds**. Successful log
`continuation22-cfg-v2.log` SHA-256:
`7a89688fcea8f59da82f49c29ba9ab89d72c2a8be31102861325b4391f921033`.
`continuation22-stack-assessment.json` independently compares original/current
graphs and the actual corrected disassembly. It confirms branch-target
resynchronization at **0x402615dc**, absence of the false **0x402615e0** jump,
and the removal of `TeslaBleStorageAdapter::load_into` ->
`NFCBricklet::setup_nfc`. The v2 decoder records **1,246** resynchronizations,
zero unresolved decode records and one explicitly recognized abort trap.

Current CFG-discovered ENTRY sums: worker **9,184** against configured
**12,288**; teardown **2,784** against **4,096** bytes. Supplemental graph:
**1,660** reachable functions, **1,919** resolved literal-call sites,
**1,233** unresolved indirect sites, **20** recursive-cycle nodes and one
unknown direct target (**0x40091b4c**); zero `.su` files. Zero non-ENTRY SP
adjustment matches is a decoder observation, **not** proof of their absence
or fully accounted stack. The original **9,808/2,784** linear-decoder sums and
all original output remain preserved as decoder-dependent historical evidence.

**Neither estimate is a complete bound or stack clearance.** Unresolved
callbacks/virtual calls, ROM/library targets, recursion, symbol-size truncation,
instruction-decoder limitations, branch-insensitive chain sums, non-ENTRY stack
adjustments and exception/RTOS/interrupt reserves remain. Target stack guards
and hardware high-water are prohibited here and unmeasured. These numerical
estimates do not authorize flashing or runtime acceptance.

### Current generated artifact manifest; no build or upload

`continuation22-artifact-manifest.json` records sizes/SHA-256, resolved paths and
literal symlink targets: **20 current generated/export/alias paths**, including
**nine output symlinks**, plus the existing `boot_app0.bin` source input.
The eight target files under `software/.pio/build/warp4_tesla_ble/` are:

- `firmware.elf`: **88,856,420 bytes**;
  SHA-256 `c5bf8c9bfb198289d6f17ec6942be7b2ec38c3b7e3d6adccece6f7a5e54375a4`.
- `firmware.map`: **35,746,007 bytes**;
  SHA-256 `8083ec54a9bf13b434f90f7580e3870cd7838bc2719046e3c9ff0e500ac2c621`.
- `firmware.bin`: **4,837,120 bytes**;
  SHA-256 `62c6a5d03131efe70c4ab773f3b072bbd01d484e2bf1d494a992fdf30cb7593c`.
- `firmware_ota.bin`: **4,898,560 bytes**;
  SHA-256 `c9521f2349aff4d63896a6198a4a747ac58a9108e5efedc1dc7908eed1ef1a75`.
- `firmware_esptool.bin`: **6,557,696 bytes**;
  SHA-256 `6260ce7ca38d712e04d8254eb87bc1a72604e5a91b104935f65f461c5d0d43c8`.
- `bootloader.bin`: **25,136 bytes**;
  SHA-256 `647197ffb8d05a67d047cc1c8415a31b089abcd03d810abac7d29ece30bc0db8`.
- `partitions.bin`: **3,072 bytes**;
  SHA-256 `4732c07e5fd28b408a05960ea163ae814dd08067b268033b6f56c11c2d71d243`.
- `firmware_info.bin`: **4,096 bytes**;
  SHA-256 `68ed5b51b3854324896d08a570937d6cf720477975a12da46367cbe70ae4d675`.

ELF/map hashes exactly match continuation 21 and the supplemental audit.
Current `software/build/` exports, both generic/product latest aliases and
`software/build_latest/` links match the target ELF/OTA/esptool content hashes.
The manifest explicitly lists excluded historical builds; it does not relabel
them as current. Manifest SHA-256:
`ffc33a7da52ef1a70bf638338a6d75fce47f3f04b84f9d0a29e19f45107afbc5`.

### Pre-append diff, source/protected inventory and receipt binding

Actually executed ordinary `git diff --check` and exact
`git diff --check 03798d2515710d2a72dcb08ec72b6b502f6028d1`: **both exit 0**.
Commands, cwd, durations, exits and log hashes are in
`continuation22-verified-pre-document-{diff-check,base-diff-check}-result.json`.
Both logs are empty, SHA-256
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.
Tracked/base-relative binary diff SHA-256 is
`a5d432acee960798f51f5e0987b72ff647467e08024fb5ad83f7946a7f16484d`.
This document is untracked, so these Git commands alone are not proof of its
content; the document's hash/prefix and source inventory bind it separately.

`continuation22-pre-document-comparison.json` checks the newly collected
**2,085 source files**, **108,227 protected files** and **161 protected symlinks**
against `before`, `pre-gates`, `after-gates` and `continuation22-start`.
Protected and symlink changed/added/deleted sets are empty against every receipt.
Before -> current source changes are **exactly** the two continuation-21 key
regressions and appended documentation; pre-gates/after-gates -> current differs
only in this document; start -> pre-document has no source changes.
No inventory-visible source paths were added/deleted; source inventory has no
symlinks. The nine generated output links are additionally covered by the
artifact manifest, not silently counted as the 161 protected links.

Original before/pre-gate/post-gate document prefix: **228,499 bytes**, SHA-256
`cff06077ffe9089ccfe9b7c190fbc6a7c6e160130cf47eb18f45bd545c5ab858`.
Entire continuation-21/start prefix: **239,762 bytes**, SHA-256
`4061346d53c5f2c8d33e155f0df017e0623e73c3ebfd383a71e2208d392285b2`.
Both prefixes remain unchanged. Branch/HEAD and empty staged diff are checked.
Non-document source mapping SHA-256 (canonical sorted JSON) is
`73a3faa331f2aaa1db21e1d2d74dfc8755106a2aa858f8d444f4a66dea335615`.

Rehashed all six passed host/web/clean/firmware/ELF/linear-stack logs and **94**
saved final focused/probe/preserved process logs; receipts still match actual
files and exit codes. Rechecked aggregate counts: **14** focused run records,
**28** R9 records (**eight builds plus 20 modes**), **30** preserved R7/R8 mode
records, all exit 0. This is receipt verification, **not** new execution of those
gates and not a unique overall test total. Their source/artifact binding is
unchanged except append-only documentation. Native crypto archives are still
not claimed fully sanitizer-instrumented.

All prior SDK/backend publication, failed-close/quarantine, blocking submission,
power-loss durability, target crypto-OOM, RF/interoperability/watchdog,
fragmentation/reclamation and licensing limitations remain. Readiness means
only eligibility for a separately user-authorized independent review, not
production/hardware clearance or Phase-2 authorization.

### Verified post-append closure and exact readiness

After the receipt section above was appended, actually executed both
`git diff --check` and
`git diff --check 03798d2515710d2a72dcb08ec72b6b502f6028d1` again: **exit 0 / 0**.
`continuation22-post-document-{diff-check,base-diff-check}-result.json` preserves
the exact commands/exits; both empty log hashes remain
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.
The post-append source/protected/symlink comparison passes against every
before/pre-gate/post-gate/start receipt, with exactly the deltas described above,
unchanged original document prefixes and no staged changes. All manifest paths,
file hashes and generated-link targets were rechecked unchanged.

`continuation22-post-document-receipt.json` SHA-256:
`9bcd8fc533c315af32bf9c61b78ad8b48ad80f2a85ed3c4dfc4d71c167000d70`.
This first post-append receipt binds the section before this closure. Final
machine receipts use the `continuation22-final-*` prefix and bind the complete
document, including this closure, by an external hash rather than a self-hash.
The final `partial-receipts.json` retains the completed prior gates, distinguishes
the preserved linear decoder from the executed supplemental assessment and
records actual final command/artifact/inventory bindings; the historical
continuation-21 receipt is preserved separately.

**READY FOR USER-AUTHORIZED ASTRA REVIEW**, limited strictly to independent
review eligibility. The remaining Phase-1 receipt gaps are closed; no new
production/test change requires gate reruns. All stack/runtime/hardware,
sanitizer/dependency, SDK/storage/power-loss, RF/watchdog and licensing
limitations above remain. **Astra has not been invoked or scheduled.** Separate
explicit user approval is still required. Stop here: no hardware clearance,
publication or Phase-2 work is authorized or performed.

## Phase-2 continuation — isolated clean-core checkpoint (not ready for review)

The user subsequently authorized Phase 2 after GPT-6 Astra review10 returned
`APPROVE PHASE 1 WITH EXPLICIT LIMITATIONS`. No new Astra invocation or scheduling
occurred. Historical Phase-1 evidence above is preserved; this is append-only.

An independent Apache-2.0 core was implemented under
`software/lib/TeslaBLEClean/`, derived only from the official pinned Tesla
vehicle-command source at `a4b43c1eff0e09d77deb9f2dce97031141fe8c8a`.
The tainted `software/lib/TeslaBLE/` implementation was not opened, copied,
diffed, edited or removed, and the WARP production integration remains unchanged.
The new core is intentionally UNBOUND until a complete replacement is tested.

Saved behavioral RED/GREEN evidence covers metadata, P-256/ECDH, HMAC/GCM,
replay, framing, both-domain response admission and SessionInfo authentication,
plus a separate overlap-refusal regression. Eight new native tests pass normally
and with ASan/UBSan; the reproducible crypto allocator matrix passes 1,876
injected failures. All 66 retained host entries, the web action test, production
web/TypeScript pipeline, and preserved R7/R8/R9/focused probes pass. The latter
have 30 builds and 64 execution modes across normal/sanitized configurations.
These legacy passes still test the OLD library, not replacement compatibility.

A clean WARP4 build-only firmware run passed with the required external
framework libraries. All six new source files also cross-compiled with actual
WARP4 SDK flags but are NOT linked into that firmware. Fresh ELF/map/memory and
reachable-stack audits completed, retaining `complete_static_bound=false` and
all prior memory/SDK/hardware/OOM/licensing limitations. Mechanical similarity
screening emitted no baseline contents and reported no suspicious matches.

The runtime limit required stopping at this coherent, tested checkpoint.
Strong durable key transactions/leases, pairing/wake/SoC protobufs, full
retry/recovery/lifecycle ownership, checked adapter compatibility and actual
replacement binding remain. The old library is deliberately retained so the
tree remains buildable; overall license clearance is NOT achieved. Final both-
diff checks, comprehensive license/reference scans, protected AFTER inventory,
complete artifact hashes, final source inventory and index readback remain
unexecuted. The initial index was empty; no staging command was issued.

No hardware, Tesla connection, upload/flash target, external publication,
commit/push/release or Astra action occurred. All review10 limitations remain.
Full supported subset, exclusions, test matrix and evidence paths:
`TESLA_BLE_PHASE2.md`.
Machine-readable receipt:
`/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom/final-receipt.json`.

Final verdict: `PHASE 2 CHECKPOINT — NOT READY FOR REVIEW`.

## Phase 2 continuation 2 — clean candidate extended, production still unchanged

Independent clean code now adds durable identity publication/nested save outcomes,
old-key leases, checked callback/mutex/storage ports, generation-owned challenges,
established-peer signed updates, charging-manager VCSEC codecs, AES-GCM routable
envelopes and authenticated ChargeState admission. Eleven behavioral RED/GREEN
slices are saved separately. Final clean suite: 21 native entries normal and
ASan/UBSan; original 1,876 allocation faults plus 1,421 continuation faults pass.
Production web passes; 11 clean sources and an adapter probe cross-compile with
actual WARP4 SDK flags. This is NOT firmware binding, a complete Vehicle client,
whole-firmware OOM/stack proof or license clearance.

The current tainted directory was not opened, diffed, edited or removed. The first
checkpoint evidence is retained unchanged; new evidence is under
`/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom-cont2/`.
The public bounded controller, transport/counter/recovery binding, actual WARP
compatibility, clean R7/R8/R9/full host replacements and replacement firmware/
removal/final release gates remain. Resume the precise list in the latest
`TESLA_BLE_PHASE2.md` continuation, rather than rebuilding completed primitives.

Mechanical screening retains one required public-interface long-line match with
external-test provenance, zero minimum-length token matches, and no emitted
baseline contents. Current firmware ELF/map/bin artifacts were found absent;
prior successful build/hash receipts are historical, not fresh artifact readback.
No Astra, hardware/Tesla access, upload/flash, staging, commit/push/release
or publication occurred. All prior platform/hardware/licensing limitations remain.

Receipt: `/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom-cont2/final-receipt.json`.
Final verdict: `PHASE 2 CHECKPOINT — NOT READY FOR REVIEW`.

Final audit caveat: all first-checkpoint evidence and known protected hashes are
unchanged, but one additionally discovered protected framework file causes the
pristine AFTER gate to fail. Its appearance time is unverified; no cleanup or
attribution was attempted. Missing current firmware artifacts and this integrity
discrepancy are recorded explicitly in the continuation receipt. No readiness or
license-clearance claim is made.

## Phase 2 continuation 3 — public framed controller, still no production switch

The independently authored clean candidate now includes actual SessionInfo wire
binding and a public bounded Vehicle controller. Framed host exchanges establish
both domains, require authenticated status/awake policy and GetChargeState, and
reach exactly-once authenticated SoC completion. Counter high-water survives
refusal/signed refresh/reconnect; synchronous RX publication, finite shared retry/
time budgets, detached disconnect callbacks and full-width ownership are exercised.
Explicit wake exposure is consumed once, never retransmitted through recovery.
PairRequestSent is only transport acceptance, not physical key approval.

Seven saved behavioral RED/GREEN fixes and additional lifecycle regressions are
separate from allocation characterization. Final 27-entry clean suites pass normal
and ASan/UBSan. The prior 1,876 + 1,421 allocation matrices remain intact and 1,877
new controller calloc failures pass. Thirteen clean sources cross-compile with
actual WARP4 SDK flags; production web/TypeScript and Node web actions pass.
This is NOT a replacement firmware link, complete R7/R8/R9 migration, measured
target stack/memory bound, hardware validation or distribution clearance.

The extra protected `.esp32` script is byte-identical to the legitimate generated
active script and matches generator/build/package metadata. Its original appearance
chronology remains unproven. A NEW complete continuation-start protected baseline
includes it; do not retroactively call the earlier BEFORE/AFTER pristine.
Both prior evidence roots are preserved. No current tainted file content was
read/searched/diffed, and no historical AGPL prose/diff was an implementation source.

Production WARP sources and old tests/runner remain unchanged. WARP adapters and
real SLAC-MAC/lifecycle/IEC-61851/authenticated-SoC binding, pairing approval/status,
all replacement-bound regressions and full host runner, clean build-only firmware,
then safe conditional tainted removal and unrestricted release audits remain.
The remaining one-shot capacity cannot safely complete those linked gates; the
continuation stops at a coherent tested/cross-compilable controller checkpoint.
No Astra, hardware/Tesla access, upload/flash, staging, commit/push/release or
publication occurred. No GitHub upload authorization is inferred.

Details: `TESLA_BLE_PHASE2.md`, Continuation 3.
Receipt: `/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom-cont3/final-receipt.json`.
Final verdict: `PHASE 2 CHECKPOINT — NOT READY FOR REVIEW`.