# Current-upstream WARP4 Tesla port verification

All VINs, BLE/SLAC addresses, keys and protocol vectors in this test directory
are synthetic test fixtures, never device evidence. Manufacturer-shaped VINs
ending in serial `000001`/`000002`, repeated-character VINs, sequential address
bytes, `11:22:33:44:55:*` and `AA:BB:CC:DD:EE:FF` are illustrative only. They
must not be used as configuration for a real vehicle.

Run from the repository root:

    python3 software/tests/tesla_vehicle/run_upstream_tests.py

This executes current production behavior, frontend, privacy, secure-build,
resource and relevant synthetic protocol wrappers. It reports every excluded
legacy test explicitly. The canonical testboard image/packaging/diagnostic
checks are not a real-WARP4 acceptance suite; their tools and device-specific
archive assumptions are intentionally not ported. See `LEGACY_ONLY` in the
runner. Their old failed checks must not be presented as passes.

For the C++ core and preserved module helper suite, set source paths to the
SDK produced by `software/lib-builder/tesla_sdk.py`:

    export TESLA_BLE_HOST_FRAMEWORK_SOURCE="$PWD/software/.pio/tesla-sdk/builder"
    export TESLA_BLE_HOST_IDF_SOURCE="$PWD/software/.pio/tesla-sdk/esp-idf-secure"
    export TESLA_BLE_HOST_MBEDTLS_SOURCE="$TESLA_BLE_HOST_IDF_SOURCE/components/mbedtls/mbedtls"
    bash software/tests/tesla_vehicle/run_host_tests.sh
    bash software/tests/tesla_vehicle/run_host_tests.sh sanitize

Multi-profile/VIN/API wrappers also accept `sanitize`. ASan/UBSan instrumentation
of a wrapper does not imply instrumentation of the separately built Mbed TLS
archive. Native scheduling, mocked callbacks and real source-extracted VFS
semantics do not establish controller behavior, RF reliability, charging
coexistence, runtime memory margins or power-loss durability.

Build commands and limitations are documented in `software/TESLA_UPSTREAM_PORT.md`.
Do not run the legacy hardware acceptance checklist as part of this source task.

## Two-writer snapshot regression

    python3 software/tests/tesla_vehicle/test_identity_two_writers.py
    python3 software/tests/tesla_vehicle/test_identity_two_writers.py sanitize
    setarch "$(uname -m)" -R python3 software/tests/tesla_vehicle/test_identity_two_writers.py tsan

The three tests exercise mutex-allocation refusal (unavailable identity and
failed runtime synchronization readiness), inventory task publication sites and
bounded lock-free readers, and execute deterministic two-writer scheduling plus
200,000 publications against four concurrent readers per backend (native mutex
and compiled ESP branch with a native cross-core critical-section model).
Temporary copies of the production headers add only scheduling hooks after
generation_low and at real native mutex/critical-model contention; the snapshot
algorithm is not replaced. Before the fix, A is
paused after generation_low, B completes, then A completes: the reader accepts
0x100000002 / index 0 / MAC 0x112233445500, neither input. With serialization,
B must report real mutex contention before A is released, then B completes; a
reader while A is paused must return unavailable. Stress accepts only A/B or
unavailable, never a composite. The wrapper is automatically included by
run_upstream_tests.py.

TSAN initially could not start with host ASLR (`unexpected memory mapping`);
setarch -R allowed real instrumented execution. No kernel/sysctl setting was
changed. ASan/UBSan does not establish race freedom. TSAN covers these executed
host schedules, not every schedule, FreeRTOS internals, hardware, or runtime
coexistence. Mutex failure injection itself is a separate normal native harness.

## Late protocol Tesla exclusion

    python3 software/tests/tesla_vehicle/test_late_protocol_tesla.py
    python3 software/tests/tesla_vehicle/test_late_protocol_tesla.py sanitize
    python3 software/tests/tesla_vehicle/test_plc_soc_safety.py
    python3 software/tests/tesla_vehicle/test_plc_soc_safety.py sanitize

The late-protocol regression compiles the production Common class, vendor
methods, complete application-protocol negotiation handler and decode dispatcher.
DIN/ISO2 probes execute the real CPD central-EV publication blocks. The actual
libcbv2g generated types and C EXI encode/decode sources are compiled, including
sanitizer instrumentation. Config slot plumbing is a host boundary, but
Config::add and its abort helper are extracted from production; maxElements is
read from the real Common API declaration. The mock cannot silently grow past
that maximum. Transport, IEC scheduling and OS dependencies remain host
boundaries; this is not a full DIN/ISO integration test. Rejected Tesla must never
select a downstream decoder, call a downstream probe, publish SoC/source, or
select negotiated ISO charging. The existing IEC transition is observed, not
reimplemented. SLAC routing and its sole autocharge authorization guard are
compiled by the companion PLC test.

The original twelve scenarios run 64 combinations each with testing options
OFF and ON:
Tesla plus DIN, Tesla plus ISO2, reversed namespace order, Tesla-only,
unknown-only, unknown plus a standard, BMW plus a standard, same unknown MAC,
same BMW MAC after stronger Tesla recognition, changed unknown MAC, changed BMW
MAC, and physical disconnect. The disconnect case executes the production
State-A cleanup prefix with PLC enabled and disabled. The six setting bits are
autocharge, read_soc, charge_via_iso15118, ignore_soc_compatibility,
nonegotiation_autocharge and nonegotiation_pending. Unknown/BMW retain existing
negotiation and central-EV publication behavior, including existing experimental
rejection settings. No additional threads are introduced; TSAN is inapplicable
to this single-threaded state-machine harness (the separate identity test above
covers the concurrent BLE publication boundary).

## Five-entry application-handshake capacity regression

The same late-protocol wrapper now has two unittest methods. Its compiled
state-machine test runs 22 scenarios x 64 settings x two testing-option builds
(2,816 combinations per normal/sanitized invocation), plus two expected-death
Config boundary probes. Added scenarios are Tesla in each of the five positions
(every request also contains DIN, ISO2 and ISO20 AC), five-entry non-Tesla
ISO2-priority/DIN-only/ISO20-only/no-match requests, and a four-entry request.
One- and two-entry scenarios remain covered. Publication count, namespace order,
response/schema, fallback, decoder choice and PLC SoC/source effects are asserted.
No namespace is silently dropped.

A separate real-wire boundary test encodes 1, 4, 5 and 6 entries. ONLY its
isolated fixture encoder receives a temporary six-slot type, to produce an
oversize wire request without out-of-bounds fixture writes. The untouched
production five-slot decoder must accept 1/4/5 and reject 6 with
EXI_ERROR__ARRAY_OUT_OF_BOUNDS (-110), preserving surrounding canaries. Repository
library sources/headers are never modified. The production Config::add probe
accepts exactly five and aborts at six; its expected SIGABRT is not a sanitizer
finding. Disable core dumps if desired with `ulimit -c 0`.

Requires GCC/G++, the libcbv2g checkout under
`software/.pio/libdeps/warp4_tesla_ble/libcbv2g`, and TMPDIR. No four-slot or
unbounded fallback fixture exists. The API bound is read from production, so
restoring the old literal 4 reproduces the capacity abort instead of falsely
passing. Production derives its bound from the actual decoded C array and
asserts agreement with the generated decoder macro and the uint8_t scan range.

Strict pre-fix RED, corrected GREEN/refinement runs, the neighboring-capacity
audit, full validation and clean non-installable build receipts are retained in
ignored `software/.pio/tesla-host-secure/sap5/`. This is source/host evidence,
not approval for installation or hardware behavior.

The first real-codec sanitizer integration run found an additional zero-length
bitstream read when the previous follow-up probe supplied only a transport
header. That failure is preserved, not counted as a pass. The final probe sends
a complete encoded appHand response and both normal/sanitized regressions pass.
That capacity follow-up did not fix or suppress the separate empty-EXI finding;
its original `empty-payload-finding.json` and failed logs remain historical evidence.
The subsequent Common input-guard follow-up below now rejects this input in
firmware before codec entry. No target exploitability or target memory impact
is claimed.

Fresh acceptance reruns of the retained fix are saved separately under
`software/.pio/tesla-host-secure/sap5/reverification/`: `results.json` records
all normal/sanitized suites and `clean-build-receipt.json` records target-directory
absence, clean ELF-only review build, TypeScript, SDK verification and exact
product hashes. Original RED and failed intermediate runs remain untouched.
The fresh wire logs contain all four vectors; count `REAL EXI` anywhere in a
line, since unittest progress output may prefix the first vector with a dot.

## Common empty encoded-payload guard

Use the same late-protocol normal and `sanitize` commands above; the complete
wrapper runner includes this regression automatically. The actual packed
8-byte V2GTP header and payload enum are extracted from production `sdp.h`,
not replaced by the former four-byte host header. Full encoded protocol
requests now enter through production `Common::decode`, not only its handler.

Each testing-options build additionally runs 324 malformed frames: four decoder
routes (AppHand/DIN/ISO2/ISO20), three accepted payload types (including ISO20 AC),
received sizes 0 through 8, and declared sizes 0, 1 and UINT32_MAX. Thus empty
payloads are rejected even when their declaration falsely promises data. A
separate null-appHand-buffer case asserts no lazy allocation. These checks assert
no bitstream setup, allocation, decoder dispatch, API mutation, protocol/vendor
change, document clearing, deadline change, IEC transition or PLC SoC publication.
Charging/BLE APIs are not supplied to this Common harness: adding calls fails
compilation rather than hiding effects. The existing PLC and full BLE-module
wrappers retain the authorization/manual-action charging-side-effect tripwires.

A minimal complete appHand Failed_NoNegotiation response is encoded by the real
codec and accepted through Common. All previous 44 scenario/build schedules
(2,816 setting combinations), two expected Config boundary deaths and four real
1/4/5/6-entry codec vectors remain. Normal and ASan/UBSan compile the real codec
C sources; downstream DIN/ISO2/ISO20 handlers remain probes, not full codecs.

Strict pre-production RED and post-fix GREEN plus complete validation/build
receipts are ignored under `software/.pio/tesla-host-secure/empty-payload/`.
The production change only strengthens the existing received-size guard from
`< sizeof(header)` to `<= sizeof(header)`. It does not alter length-declaration
handling for non-empty frames, TCP reassembly, codecs, or downstream policies.
SDP already checks the exact request size and declared payload length before
access; DIN/ISO2/ISO20 bitstream handlers are reached through this shared guard.
This closes the observed zero-size codec entry, not every malformed-EXI or
transport-framing robustness question, and is not installation approval.
