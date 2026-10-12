# Phase 2 — clean-room replacement implementation

Final verdict: READY FOR USER-AUTHORIZED ASTRA RE-REVIEW

Current-status note: the original checkpoint narratives below are retained as
history. The first independent Astra review withheld acceptance for B1/B2 despite
Continuation 6's passing suites. The B1/B2 fix section at the end supersedes that
blanket acceptance and records fresh post-fix evidence. This verdict authorizes
nothing: a further Astra review still requires the user's separate authorization.
All earlier evidence roots/receipts remain immutable; hardware, distribution and
legal approval are not claimed.

Historical first-checkpoint narrative follows; production binding status changed
in Continuation 4. This is NOT the completed license-cleared replacement.
The following original account is historical, not current: the approved Phase-1 integration was unchanged and still
uses `software/lib/TeslaBLE/`, with its existing license limitations. That tainted
directory was not opened, edited, removed or used as an implementation source.
It must not be removed until a complete replacement is bound to the integration
and passes all required behavior gates. No distribution/license clearance is
claimed. No Astra invocation or scheduling occurred.

## Scope and provenance established before production implementation

Evidence root:
`/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom/`

The initial inventory, official source manifest and opaque baseline archive were
saved before implementation. The official Tesla vehicle-command source was
verified clean at commit `a4b43c1eff0e09d77deb9f2dce97031141fe8c8a`.

Protocol logic came only from that official Apache-2.0 reference. Exact source
files/sections and compatible dependency API documentation are recorded in:
`software/lib/TeslaBLEClean/PROVENANCE.md`.

The new library is independently authored in namespace `tesla_clean`, carries
Apache-2.0 SPDX identifiers, and includes LICENSE, NOTICE and library metadata.
No legacy generated code or compatibility headers were copied. Interface needs
were inventoried from WARP call sites and public behavioral tests outside the
tainted directory. Historical Phase-1 documentation and review10 were opened
for status/limitations only; their implementation prose was not a source for
new code. The tainted implementation itself and historical diffs were not read.

The preserved baseline archive is for mechanical comparison only. The audit
never emitted baseline text or snippets. It compared 50 baseline C/C++ files
against 13 new core C/C++ files using exact token sequences (minimum 40 tokens),
identifier-normalized sequences (minimum 100 tokens), and exact long lines
(minimum 60 characters). It reported zero suspicious matches. This screening is
not a legal opinion or proof of independent authorship. The baseline is still
retained at this checkpoint, rather than falsely described as removed.

## Implemented and independently tested subset

New source: `software/lib/TeslaBLEClean/`.
New tests: `software/tests/tesla_vehicle/cleanroom/`.
The existing full host runner now also invokes the clean-core suite.

- Ordered, bounded metadata TLV serialization; big-endian uint32 fields,
  terminal byte and SHA256. An official Tesla known-answer vector is retained.
- P-256 key generation, scalar import, unencrypted PEM import/export and checked
  static ECDH. Failed imports/generation retain the old in-memory identity.
- Tesla-compatible shared-key derivation: SHA1 of the fixed-width 32-byte shared
  x-coordinate, truncated to 16 bytes. The official leading-zero ECDH vector
  passes. This reference uses labeled HMAC-SHA256 subkeys, NOT HKDF; introducing
  HKDF here would change the wire protocol.
- Session-info labeled HMAC-SHA256 and out-of-place AES-128-GCM, tested against
  independently generated Python cryptography fixtures. Authentication failure
  erases plaintext/output. RNG/capacity/null and overlapping-buffer refusal
  are explicit.
- Bounded SessionInfo protobuf decoding and authentication before output/key
  publication. Authenticated key denial is distinct from an established session.
  Wrong tag/blob/challenge/VIN/point and malformed/truncated protobufs reject.
- Per-session replay admission, with a 32-position out-of-order window, duplicate
  and too-old rejection, large-gap handling and no wire-counter rollover.
- VCSEC and Infotainment encrypted-response admission: correct received-counter
  AAD, UUID/domain/request hash/flags/fault binding, full-width generation and
  caller-supplied deadline checks, once-only completion and replay rejection even
  with an eligible newly armed request. Cancel/rearm does not erase replay history.
- Bounded BLE length-prefix framing, fragmented/concatenated input, 1024-byte
  payload cap, partial-frame timeout, detached reentry ownership and full-width
  generation checks between transmitted fragments. Transport refusal/retirement
  prevents subsequent old fragments from reaching a successor link.

These are core primitives, not a public Vehicle client. API/lifetime constraints
and intentionally unsupported cases are in the library README. In particular,
SessionInfo authentication still requires a future controller to bind a current
outer UUID/domain/generation challenge. ResponseGuard borrows an immutable,
long-lived session crypto object. There is no thread-safety or identity-lease
claim. There is no SoC publication API or charge-start operation in this core.

## Strict TDD evidence

Tests were authored before their production behavior. Each main slice first ran
against an explicit refusal scaffold and failed a behavioral assertion, exit
134. The scaffolds were then replaced with minimum clean code; none remains.
Metadatum absence also has an initial missing-header RED, exit 1. The overlapping
crypto-buffer follow-up has its own behavioral RED and GREEN.

| Behavior | RED evidence label | GREEN evidence label |
| --- | --- | --- |
| Metadata | metadata-behavior-red | metadata-green-api3 |
| Key/ECDH | key-red | key-green |
| Session HMAC/GCM | session-crypto-red | session-crypto-green |
| Replay | replay-red | replay-green |
| BLE framing | framing-red | framing-green |
| Both-domain response admission | response-red | response-green |
| SessionInfo authentication | session-info-red | session-info-green-linkage |
| Crypto overlap refusal | crypto-alias-red | crypto-alias-green |

Each label has a `LABEL.log` and `LABEL-result.json` with exact command, exit
status and log hash under the evidence root. Exploratory compiler failures are
retained: the matched SDK uses mbedTLS 3 hash API names; its constant-time header
requires explicit C linkage. Neither failure was represented as GREEN.

Two test-authoring mistakes were corrected before successful acceptance, not
weakened to accommodate production: the official metadata transcript length was
calculated as 46 bytes; proto3 omitted default scalars were correctly distinguished
from failed authentication of incomplete signed transcripts. No Phase-1 test
was removed, weakened or replaced. Internal-coupled Phase-1 tests remain bound to
the unchanged old library and are NOT evidence that the new core is compatible.

Allocation testing is additional characterization, not a fabricated production
RED/GREEN slice. The first randomly seeded allocation enumeration failed because
allocation counts varied between fixtures. A deterministic PUBLIC test entropy
source made the enumeration reproducible; production RNG requirements were not
changed. The passing matrix injects 1,876 calloc failures across scalar import,
generation, PEM import, ECDH, HMAC, GCM encrypt/decrypt. It checks refusal, retained
identity/cleared output, same-object retry, no tracked crypto resource leaks and
zero feature-owned throwing-new calls with exceptions disabled. This is not a
whole-firmware or target OOM guarantee.

## Executed gates — all listed executions GREEN

- Clean core: 8 distinct native test entries normally and with ASan/UBSan.
  Leak detection/halt-on-error were enabled; no findings. Existing mbedTLS host
  archives were not themselves rebuilt with instrumentation.
- Complete host suite: all 66 expected legacy entries executed once, plus the
  clean-core suite and existing Node web-action test. Entry audit reconciles
  exact sets/counts. A first order-only audit assertion differed because shell
  locale collation is not Python lexical order; sorted exact reconciliation
  showed no missing, extra or duplicated entries.
- Preserved R7/R8/R9 and focused Phase-1 groups: 30 successful native builds and
  64 successful execution modes across normal/sanitized configurations. Original
  probe sources were not edited. These prove retention of the OLD implementation's
  tested behavior; they do not certify the new replacement. Existing legacy
  printf warnings remain in their logs, not treated as correctness evidence.
- Production web/TypeScript pipeline passed: tsc, esbuild, sass, postcss and HTML
  minimization through the existing local production helper.
- Clean build-only WARP4 firmware passed, with
  `TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs`.
  Exact targets were `pio run -e warp4_tesla_ble -t clean`, then
  `pio run -e warp4_tesla_ble -j 4`, under offline/network-isolated execution.
- Current WARP4 compile database was generated with the build-only `compiledb`
  target. All 6 new source files cross-compiled with actual WARP4 SDK flags and
  `-Werror`. They are NOT linked into the firmware or executed on a target.
- New-core SPDX/import/operation negative test passed: unchanged WARP Vehicle
  call allowlist, routine NO_WAKE_SKIP branch, no charge-start calls, no tainted
  library imports. This is a scoped source contract, not whole-tree clearance.
- Mechanical similarity audit passed with zero suspicious matches.
- Fresh ELF/map/memory and ordinary plus CFG-resynchronized reachable-stack
  auditors completed successfully against the preserved Phase-1 firmware.

Fresh firmware headline: RAM 93,356 / 327,680 bytes; flash 4,835,409 / 6,488,064
bytes. Detailed ELF DRAM is separately 93,354 bytes, not silently equated with
that headline. ELF SHA256:
`79f209ccaf1860c4cf57866ecc0df33600a45460c295feef3fad551e7be6fdec`.
Map SHA256:
`8083ec54a9bf13b434f90f7580e3870cd7838bc2719046e3c9ff0e500ac2c621`.
Other artifact hashes have not been finalized at this runtime checkpoint.

CFG discovered ENTRY chain sums: worker 9,184 bytes (configured 12,288), teardown
2,784 (configured 4,096); 1,660 reachable functions, 1,919 resolved literal calls,
1,233 unresolved indirect sites and unknown direct target 0x40091b4c.
`complete_static_bound` remains false. These are neither complete upper bounds
nor measured spare stack. The core is unbound, so these are NOT new-core stack
bounds. Bluetooth static/release limitations from Phase 1 remain unchanged.

## Remaining work and gates (NOT complete)

The runtime limit was reached at this coherent tested checkpoint. Do not claim
Phase 2 ready for review or replace/delete the old library yet.

1. Strong durable key transactions, nested publication and independent identity
   leases for pending/established exchanges; storage commit/cleanup/quarantine
   contracts and checked no-exceptions callback/mutex adapters.
2. Generation-owned challenge lifecycle and authenticated session update/recovery.
3. Actual VCSEC charging-manager pairing/status and explicitly authorized wake,
   outgoing signed/encrypted protobuf envelope encoding and transactional counters.
4. Infotainment GetChargeState request/response protobufs and battery-level parser,
   end-to-end authenticated SoC correlation and no-wake policy through the actual
   WARP module.
5. Bounded command queue, refusal/recovery/retry budgets, total/transmit deadlines,
   disconnect/nested reentry and full R7/R8/R9 behavior binding to the replacement.
6. Replace internal-coupled tests only if necessary, with independent equivalent
   behavior coverage and an explicit rationale. No such replacement occurred here.
7. Bind the complete implementation, then remove the tainted directory and license
   baggage; rerun similarity against the removed baseline and all release gates.
8. Final both-diff checks, comprehensive source/license/forbidden-reference scans,
   protected framework/package AFTER integrity inventory, complete artifact hashes,
   final source inventory and final index readback were NOT completed in this run.
   Initial index was empty; no staging operation was issued. Do not mistake the
   initial inventory or scoped negative test for these outstanding final gates.

## Preserved limitations and safety

SLAC MAC remains vehicle identity; BLE addresses remain transport only. WARP
IEC-61851/SLAC/autocharge is authoritative. The unchanged normal polling policy
uses NO_WAKE_SKIP; existing opt-in one-wake/manual behavior is not represented as
an absolute guarantee that a real vehicle cannot wake through connection.
Runtime enable/disable and quarantine ownership remain the Phase-1 behavior.

All review10 limitations remain: shared Config/API OOM boundary, SDK deep-copy
assumptions, LittleFS close uncertainty/quarantine and no power-loss guarantee,
noncancellable SDK work and reboot-required shutdown, no hardware/interoperability/
RF/sleep/wake/heap/high-water validation, incomplete static stack analysis, WARP4
and pinned SDK scope only, residual static Bluetooth memory, unresolved overall
licensing, finite wrap/reentry evidence and partial host/sanitizer models.

No hardware access, Tesla connection, upload/flash target, external publication,
commit/push/release, or Astra invocation occurred. Commands used local offline
network namespaces. Generated build/compile artifacts are not upload evidence.

## Evidence paths

All below are under
`/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom/`:

- scope-inventory.json; official-source-manifest.json
- before.json.gz; before-summary.json; tainted-baseline.json; tainted-baseline.tar.gz
- All RED/GREEN `*-result.json` and `*.log` labels in the TDD table
- allocation-characterization.log; allocation-deterministic.log/result.json
- clean-core-normal.log/result.json; clean-core-sanitized.log/result.json
- full-host.log/result.json; test-entry-audit.json
- retained-probe-results.json; retained-probe-summary.json; retained-native/
- production-web.log/result.json
- firmware-clean.log/result.json; firmware-build.log/result.json
- target-compiledb.log/result.json; target-core-results.json; target-core/
- similarity-audit.json; similarity.log/result.json
- elf/; elf-map-memory.log/result.json
- stack/; stack-cfg-v2/; reachable-stack.log/result.json; cfg-reachable-stack.log/result.json
- final-receipt.json (explicitly records checkpoint status and unexecuted gates)

The worktree is buildable as the preserved Phase-1 firmware; the clean candidate
core is host-tested and cross-compilable but is not the firmware implementation.

## Continuation 2 — verified identity/lifecycle/protocol checkpoint, still UNBOUND

Final verdict: PHASE 2 CHECKPOINT — NOT READY FOR REVIEW

Evidence root:
`/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom-cont2/`.
The first checkpoint and official pin were read/verified first. The first
checkpoint's files, protected SDK/packages and old production integration are
subject to exact BEFORE/AFTER readback in this continuation. No current file
under `software/lib/TeslaBLE/` was read, searched, diffed or used as a source.
The old directory is deliberately retained. No distribution clearance is claimed.

### Implemented extensions, not a complete Vehicle replacement

- `identity.hpp/cpp`: checked independent identity allocations, durable storage
  commit classification, identical candidate/PEM across nested same-storage
  rotation, one publication even when inner/outer outcomes differ, explicit
  cleanup outcome and restart quarantine. Refusal preserves prior identity.
  Publication references/bytes are prepared before save, with no identity
  allocation after persistence. Leases preserve old private/public material for
  both pending and established exchanges. Different storage/mutation operations
  cannot join the transaction; nesting is capped at eight. The backend must
  actually honor its durable contract; this is not a new LittleFS implementation
  or hardware power-loss guarantee.
- `support.hpp`, `storage.hpp`: checked callback allocation/replacement with
  pinned invocation across nested clear/replacement, move-only captures and safe
  optional empties; throwing capture construction/destruction is unavailable.
  Checked mutex creation/lock refusal/retry/balanced destruction; bounded key
  storage load/save/remove port, erased failed loads, explicit commit/refusal/
  cleanup results and invalid-enum/restart quarantine. These are clean ports,
  not yet the WARP adapter ABI. User callback bodies/capture creation and shared
  Config/API remain outside the allocation guarantee. Single-worker ownership.
- `authentication.hpp/cpp`: pending challenge and established identity leases
  are distinct. Outer domain/UUID/full-width generation, original total/response
  deadlines and HMAC are checked before establishment. A refused recovery send
  restores the prior challenge. Synchronous RX during send is staged and cannot
  publish a session before transport acceptance. Nested reset/replacement survives
  old send unwind. Authentication recovery shares three attempts and 100 ms
  backoff per operation; refused attempts consume that budget. Denial consumes
  only its authenticated current challenge, once. Signed updates/denial trust
  the established peer's HMAC/public point, not a newly proposed peer; epoch/
  clock updates never decrement the outgoing base counter.
- `messages.hpp/cpp`, `protobuf.hpp`: bounded independently authored ordinary
  protobuf subset, personalized AES-GCM outgoing routable envelopes, routing
  address/UUID and signer public identity; encrypted-response envelope decoder.
  Outgoing encode is atomic with respect to counter publication, including RNG,
  capacity and overflow refusal. The independent Python KAT matches exact wire
  bytes and GCM request identifier. Counters are NOT yet a transport/queue
  transaction; session-update high-water preservation must be bound by the future
  controller. Only AES-GCM is implemented, not an unnecessary HTTP/HMAC client.
- `vcsec.hpp/cpp`: charging-manager role and PRESENT_KEY pairing KAT, status
  request, sleep-state and pairing OK/WAIT/error subset. Wake bytes are emitted
  only for explicit authorization. This codec is not a whole no-wake policy or
  connected pairing controller. Raw pairing/status never authorizes SoC.
- `charge_state.hpp/cpp`: GetChargeState-only read request, explicit successful
  CarServer action status, present battery_level integer 0-100; missing, malformed,
  duplicate, negative, out-of-range, conflicting and error messages refuse.
  ResponseGuard-to-battery admission correlates authenticated UUID/hash/domain/
  received counter/flags/fault/full-width generation and deadlines. A decoded
  encrypted envelope alone cannot publish SoC. There is still no WARP callback
  publication path or charge-start operation.

Borrowed session-crypto pointers must be retired before reset/rekey. Local
challenge serials refuse exhaustion rather than wrap. UUID collision checking
covers current/last values only; secure nonreentrant RNG is mandatory. Finite
clock horizons and the original platform/hardware limitations remain. The README
records these API/lifetime restrictions explicitly.

### TDD and final clean-candidate verification

Each new behavior slice has a saved behavioral assertion RED (exit 134) before
its GREEN (exit 0). The first outgoing-message RED was a compiler failure caused
by a test-vector identifier that was a C++ keyword; it is retained separately,
and the behavioral RED was executed before production implementation.

| Behavior | RED label | GREEN label |
| --- | --- | --- |
| Checked callback lifetime/refusal | checked_callback-red | checked_callback-green |
| Checked mutex | checked_mutex-red | checked_mutex-green |
| Durable identity/nested publication/leases | identity-red | identity-green |
| Checked storage/quarantine | storage-port-red | storage-port-green |
| Generation-owned challenges/recovery | challenge-red | challenge-green |
| Established-peer signed update/denial trust | session-update-red | session-update-green |
| No-exceptions capture restriction | callback-capture-red | callback-capture-green |
| ChargeState parsing/authenticated correlation | charge-state-red | charge-state-green |
| Outgoing/received routable envelopes | messages-behavior-red | messages-green |
| Charging-manager pairing/status/explicit wake codec | vcsec-red | vcsec-green |
| Proto3 flags defaults remain authenticated | message-defaults-red | message-defaults-green |

All labels have command/exit/log hash receipts. The proto3 follow-up corrected a
test-authoring mistake: removing a complete final optional scalar field is a
valid protobuf message, not malformed truncation. Every prefix is still covered;
the additional RED/GREEN test requires zero-default flags to decode AND fail GCM
admission when those flags do not match the signed transcript. The original valid
reply must still succeed afterwards. No Phase-1 test was edited or weakened.

Final accepted clean suite: 21 distinct native entries normal and ASan/UBSan,
with exceptions/RTTI disabled and leak/halt-on-error enabled. Existing mbedTLS
archives are still not instrumented. The original 1,876-fault primitive matrix
remains intact; the continuation adds 1,421 deterministic calloc failures across
five identity/authentication/update/outgoing-encode operations, checking retained
state, no tracked crypto leaks, no throwing-new calls and same-object retry.
Identity/callback allocation-refusal ports and the 50 mixed nested-save cases
also pass. A separate five-outcome composition test starts an actual pending
domain handshake inside persistence while another domain is established, then
verifies real HMAC admission and old-identity agreement after commit/refusal.
Allocation matrices are characterization/regression, not fabricated TDD REDs.

Production web/TypeScript pipeline was freshly executed and passed. Eleven clean
source files plus one adapter-instantiation probe cross-compiled with actual
WARP4 SDK flags, `-Werror`, and `-fstack-usage`; all 12 objects passed. Compiler
dependency readback checks for old-library imports. The largest compiler frame is
3,296 bytes for outgoing build; challenge begin is 2,256. These are local static
frames, NOT reachable call-chain bounds, configured-worker spare stack or target
high-water measurements. The new core is NOT firmware-linked or target-executed.

Mechanical screening compares 50 opaque archived baseline files against 26 clean
C/C++ files. It reports zero minimum-length exact/normalized token matches and
one long-line match at clean `identity.hpp:6`: the required public storage-outcome
declaration independently inferred from external tests. Hash/position/count-only
report and provenance classification are preserved; the code was not changed to
hide the match. No baseline contents were emitted, and this is not legal clearance.

### Precisely remaining — resume here, do not redo verified primitives

1. Build the public bounded command/Vehicle controller and actual SessionInfo
   request/response wire binding. Join the identity/challenge/codec/framing layers,
   preserving outgoing counter high-water and prior correlation on refused sends.
   Unify retry/recovery budgets, total/transmit/response deadlines, disconnect and
   nested callback cancellation across command queue and both domains.
2. Complete policy-controlled VCSEC pairing/status/wake and actual Infotainment
   GetChargeState flow through that controller. Prove fragmented RX/TX, exactly-
   once completion, generation ownership and authenticated SoC all the way to the
   WARP callback; standalone codec/core admission is not that proof.
3. Independently author the WARP external compatibility surface, storage/mutex/
   callback adapters and runtime lifecycle binding. Preserve authoritative SLAC
   MAC, routine NO_WAKE_SKIP, IEC-61851 charging authority, runtime enable/disable
   and absence of a BLE charge-start path. The integration is unchanged here.
4. Add independently behaviorally equivalent clean R7/R8/R9 and all applicable
   internal-coupled host regressions, with explicit replacement rationale. Update
   the full host runner so no compiled source/import comes from the old library.
   No old test was replaced in this continuation.
5. Run replacement-bound normal/sanitized/fault and full host gates, production
   web, then clean build-only WARP4 firmware with the required external framework
   libraries. Only AFTER host and firmware binding pass may the tainted directory
   and distributable AGPL baggage be removed. Keep the opaque archive only in
   scratch. No fresh firmware build was attempted while still unbound: that would
   compile the old implementation, not prove replacement readiness.
6. After actual binding/removal, run unrestricted both-diff and whole-tree license/
   forbidden-reference gates, fresh ELF/map/memory and reachable-stack evidence,
   full protected inventories/artifact hashes/source/index/branch readback. This
   continuation's scoped audits and preserved firmware hashes do not replace them.

The continuation capacity ended at completed, tested slices before a safely
verifiable controller/binding could be finished. All production WARP sources are
preserved. Final audit discovered that the current firmware ELF/map/bin files
are absent; they could not be freshly hashed or assessed. The first checkpoint's
saved successful build/hash receipts remain historical evidence only, not current
artifact readback or fresh replacement firmware evidence. The initial final-audit
failure is retained, and the corrected audit records missing artifacts explicitly
rather than inventing hashes or rebuilding the tainted implementation. Final continuation audit
statuses, source/SDK/checkpoint integrity and artifact hashes are in
`cont2/final-receipt.json`. Scoped diff/source scans explicitly exclude the
current tainted directory to obey the firewall; old references and unresolved
licensing are not silently reported as cleared.

No Astra was invoked/scheduled, and no hardware/Tesla connection, upload/flash,
commit, push, release, publication or external state mutation was performed.
Tests/build-only compiler/web calls ran in offline network namespaces. No staging
command was issued. Review requires separate user authorization after the missing
implementation and gates are genuinely complete.

### Final audit readback — explicit outstanding integrity gate

The final AFTER inventory confirms that all 6,155 first-checkpoint evidence files
are unchanged, the index remains empty and branch/HEAD are unchanged. All known
protected file hashes are unchanged, but complete root discovery finds one
additional protected file:
`/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs/esp32/pioarduino-build.py.esp32`.
Consequently the pristine protected-AFTER assertion FAILED and its receipt/log
are retained; this is NOT an integrity GREEN. The initial continuation BEFORE
rehashed the known first-checkpoint inventory, rather than discovering additions,
so this run cannot establish when that file appeared. No deletion/restoration or
attribution was attempted. Reconcile this metadata-only discrepancy before
claiming protected framework integrity in a completed replacement release gate.

Both scoped worktree/index diff checks passed with the tainted path explicitly
excluded. Broad allowed-source scans retain old import/license references and
do not clear distribution. The first firmware-audit attempt failed because ELF/
map/bin are absent; its subsequent receipt records those absences explicitly.
Final clean code/tests are unchanged after accepted host/target runs. These final
documentation-only notes postdate the saved AFTER source inventory; the receipt
and artifact manifest separately hash the final documents. The runtime limit
arrived during final audits, with no new implementation started.

## Continuation 3 — public framed Vehicle checkpoint, production still UNBOUND

Final verdict: PHASE 2 CHECKPOINT — NOT READY FOR REVIEW

Evidence root:
`/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom-cont3/`.
The Phase-2 document and continuation-2 final receipt were the first files read.
Both earlier evidence roots/receipts are preserved, never overwritten. A new
complete protected-root baseline was captured before implementation: 124,188
protected files, including the additional `.esp32` script, and 8,587 files across
the two earlier evidence roots. This does NOT repair their historical chronology.

### Implemented and actually exercised

- `session_wire.hpp/cpp`: bounded SessionInfo request/reply envelope, public key,
  outer UUID challenge, domain/address routing and raw body/HMAC wrapper. Exact
  request bytes match a separate Python ordinary-protobuf KAT. Both-domain replies
  reject malformed/truncated/duplicate/oneof/routing/UUID errors. Decoding alone
  grants no authentication. Full response UUIDs are deliberately required even for
  VCSEC; the official dispatcher's omitted-UUID variant is not supported here.
- `vehicle.hpp/cpp`: independently authored public `tesla_clean::Vehicle`, four
  total FIFO command slots, four bounded deferred RX frames, per-domain leased
  authentication/replay and generation-owned framed TX/RX. A real framed host path
  now establishes VCSEC, obtains authenticated awake status, establishes
  Infotainment, sends GetChargeState and produces an authenticated, correlated,
  exactly-once SoC completion. It is NOT the WARP publication callback yet.
- Successful encodes reserve exposed counter high-water before transport, retain
  it across signed refresh/refusal/reconnect, and publish UUID/request-hash admission
  only after entire-frame acceptance. Synchronous RX cannot grant a session/SoC
  early; refusal/overflow discards it. Failed crypto retains prior state and permits
  same-object retry. Secure RNG/nonreentrant clock/link-check contracts remain.
- One shared eight-attempt budget covers a command's authentication, requests,
  retries and recovery, in addition to the authentication layer's three-attempt
  cap. Retry backoff is 100 ms; queued time consumes the original total deadline.
  Total/response/transmit horizons are finite and cannot be restarted by retries;
  each fragment checks generation/ownership and transmit/total time before/after
  submission. Exact deadline arrival expires. This is a local policy, not a copied
  legacy state machine or an assertion of identical internal timing semantics.
- Disconnect detaches all old completions before invoking them, retires borrowers
  before auth reset, preserves callback-created successor work/partial RX, and
  refuses retired/reused/wrapped full-width generations. Public completion contexts
  may be reused, but private authentication-operation identities never wrap/reuse.
- Routine `NoWakeSkip` polling requires authenticated VCSEC status; asleep completes
  without wake or Infotainment. Opt-in/manual wake authorization is consumed before
  possible wire exposure; refusal/timeout/signed refresh goes through status, not
  a second wake. Pairing sends charging-manager PRESENT_KEY only on explicit
  authorization and reports `PairRequestSent`, NOT key approval or authentication.
  No charge-start operation exists. Physical pairing/status publication is still missing.
- An abandoned frame prefix is not silently followed by another frame: the backend
  must explicitly prove peer framing reset/cleanliness through `recover_frame` or
  the controller locally retires the old link/completions. The mock resets its
  receiver explicitly. This is NOT evidence for real BLE framing reset or physical
  SDK disconnect; those must be bound by the future WARP adapter.

### TDD and reproducible accepted gates

All behavioral REDs below are saved assertion failures (exit 134), before their
production fix, with immutable command/exit/log-hash receipts. No scaffold remains.

| Slice | RED label | GREEN label |
| --- | --- | --- |
| SessionInfo wire/KAT | session-wire-red | session-wire-green |
| Public framed Vehicle/SoC/no-wake/queue | vehicle-flow-red | vehicle-flow-green |
| Potentially exposed refused wake | exposed-wake-red | vehicle-policy-green |
| Reused public context/private budget | exposed-wake-green-context-red | vehicle-policy-green |
| Reconnect counter high-water | reconnect-highwater-red | vehicle-recovery-accepted-green |
| Refused partial-frame reset requirement | refused-framing-red | vehicle-recovery-accepted-green |
| Wake refresh without retransmission | wake-refresh-red | wake-refresh-green |

The label `vehicle-recovery-green` is a retained FAILED exploratory execution,
not a GREEN gate. It exposed a mock scenario that refused the first chunk before
its intended synchronous end-of-frame response could exist. The test setup was
corrected to refuse after that response at the final write; all original acceptance
assertions remain. A separate behavioral RED then required explicit peer-frame
recovery, rather than concealing the abandoned prefix or weakening the assertion.

Final clean suite: 27 distinct native entries normal and ASan/UBSan, exceptions/
RTTI disabled, leak/halt-on-error enabled; no sanitizer findings. The existing
mbedTLS host archives are still uninstrumented. The original 1,876 faults and
continuation-2 1,421 faults pass unchanged; an additional 1,877 deterministic
controller calloc failures across both-domain SessionInfo/outgoing/response
composition pass with no failed publication/counter mutation, same-object
completion, no tracked crypto leaks and no feature-owned throwing-new calls.
Allocation matrices and already-passing lifecycle schedules are characterization/
regression, not invented TDD REDs or whole-firmware OOM guarantees.

New public regressions cover R7-related refusal/backoff/shared budget/signed recovery,
stale UUID/counter/deadlines and R8-related detached old tails/denial/nested write
reentry/fresh fragmented RX. Eight direct/denial high-width schedules exercise all
four accepted old entries and queue overflow separately. These are NOT a claim
that ALL R7/R8/R9 equivalents are done: 31-tail private legacy schedules are not
ported, and the actual R9 WARP module/GATT boundary remains completely unbound.
No old test or full-host runner was edited, removed, weakened or silently replaced.
The full host runner STILL imports/compiles old sources and was NOT executed here.

Production web/TypeScript (tsc/esbuild/sass/postcss/HTML minimization) and the Node
web-action test freshly pass. All 13 clean C++ sources cross-compile with actual
observed WARP4 SDK flags, `-Werror` and compiler stack-usage outputs, with no old
library dependencies. This is NOT a clean firmware link or target execution.
Largest compiler frames: outgoing builder 3,296 bytes, Vehicle frame admission
2,832, challenge begin 2,256. They are not reachable call-chain bounds, measured
worker spare stack or target high-water evidence; `complete_static_bound=false`.

### Protected-file reconciliation and final checkpoint audits

`protected-file-reconciliation.json` records the additional file's exact hash,
stat metadata, byte-identical active sibling, generator-template prefix/suffix,
matching SDK config/lock and package/build inventory hashes. It is consistent
with a retained copy of the legitimate generated ESP32 SDK build script; the
suffix creator and original appearance chronology remain unproven. It was not
deleted, restored or modified. The newly complete continuation-3 BEFORE/AFTER
inventories, not an invented pristine earlier baseline, govern this run's integrity.

Fresh opaque similarity screening: 50 archived baseline files versus 30 clean
C/C++ files, zero minimum-length exact/normalized token matches and the same one
explained public storage declaration long-line match. Only fingerprints/locations/
counts were emitted. No code was adjusted to hide it; this is not legal clearance.

Scoped worktree/index diff checks exclude the current tainted directory. Allowed
source/license/reference diagnostics and final source/branch/HEAD/index/artifact
inventories are recorded under this evidence root. Production WARP/web/INI sources
and old tests/runner are unchanged by this continuation. The clean candidate's
accepted source/object/binary hashes are reconciled programmatically. Final
protected/prior-evidence integrity outcomes are in `after-summary.json` and the
receipt; no post-inventory code changes are planned. No current replacement
firmware ELF/map/bin exists, so no fresh hashes, memory or reachable-stack result
is invented. Historical firmware build receipts remain historical only.

### Precisely remaining — no deletion or review authorization yet

The remaining one-shot capacity is insufficient to safely finish WARP binding,
the complete replacement-bound test migration, clean firmware link, conditional
removal and all release audits. Stop here at the host-tested, target-cross-compiled
public controller checkpoint rather than leave a partially switched production tree.

1. Independently author and bind WARP-compatible checked storage/mutex/callback/
   public surface and persisted identity setup. Connect the real lifecycle, GATT
   physical refusal/retirement, authoritative SLAC MAC and authenticated SoC event
   path. Preserve runtime disable/quarantine and IEC-61851 charging authority.
2. Complete pairing approval/status workflow (raw accepted pairing bytes do not
   establish a key) and WARP manual/opt-in/routine no-wake policy composition.
   Explicitly resolve or retain/document the strict VCSEC response-UUID limitation
   before interoperability claims. Real hardware validation remains unauthorized.
3. Independently port ALL applicable R7/R8/R9 and other host regressions to the
   replacement. The existing full host runner must compile/import no old source.
   New R7/R8-related controller tests here are useful partial coverage, not that gate.
4. Run replacement-bound full host normal/sanitized/fault gates and a clean
   BUILD-ONLY `warp4_tesla_ble` link with
   `TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs`.
   No upload target. No fresh firmware build was run while still unbound, because
   that would open/compile the forbidden old implementation rather than prove this one.
5. ONLY after those gates pass, remove `software/lib/TeslaBLE/` and source AGPL
   baggage. It remains deliberately untouched; removal is NOT currently authorized
   by successful replacement gates. Overall distribution/license clearance is false.
6. After removal: both unrestricted diff checks, whole-tree distribution license/
   forbidden-reference audit, repeated opaque similarity, fresh ELF/map/memory/
   reachable-stack assessment, complete artifact hashes, protected AFTER inventory
   and final source/index/branch readback. This checkpoint's scoped scans and local
   compiler frames do not replace those release gates.

No hardware/Tesla access, flash/upload, staging, commit, push, release, publication,
or Astra invocation/scheduling occurred. No GitHub upload authorization is inferred.
All prior platform/storage/SDK/shared-Config-API/hardware/stack/licensing limitations
remain, except only the explicitly tested clean candidate additions described above.

Receipt:
`/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom-cont3/final-receipt.json`.


## Continuation 4 — production clean binding and real firmware link; coverage gate OPEN

Final verdict: PHASE 2 CHECKPOINT — NOT READY FOR REVIEW

Evidence: `/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom-cont4/`.
Receipt: `final-receipt.json` in that directory. This section supersedes the
older UNBOUND status, not the preserved historical receipts.

### Actual critical-path implementation

The WARP module, transport, storage and `warp4_tesla_ble.ini` now use only
`TeslaBLEClean`. No tainted header is included. PlatformIO explicitly ignores
TeslaBLE, and final host/firmware commands additionally hide its directory in
an empty private mount namespace. The retained directory itself is untouched.

Independent boundary utilities provide borrowing callbacks, checked native
mutex creation/RAII locks and fixed VIN storage. The existing allowed WARP
POSIX-on-LittleFS transaction adapter now implements clean KeyStorage: checked
write/sync/read-back/close, explicit committed/cleanup/refusal classification,
failed-load erasure and reboot quarantine on uncertain close. Identity import
uses a bounded erased PEM buffer; new identity publication occurs only after
persistence commits. Startup/reconstruction imports existing identity but does
not regenerate a forgotten key. A new key is durably created on explicit pairing.
No power-loss or whole-firmware OOM proof is claimed.

The real clean Vehicle ports are connected to SDK writes, full-width logical
and physical generation ownership, actual module RX queues and worker tick.
A refused framed stream never gets a mock same-link reset: WARP retires the
physical link before exposing old completions. A separately owned CLOSE slot
allows physical retirement even after WRITE cancellation; cancelled operations
still quarantine the runtime. RX overflow also causes physical retirement.
Detached completion reentry preserves a successor's partial RX and queued work.
These are production functions exercised with host SDK boundaries, not hardware.

Authenticated SoC completions reach the real module event path, and final
consumption again checks runtime/request/connection/SLAC identity and MAC before
`ev.set_soc`. BLE addresses remain transport only. IEC-61851/SLAC/autocharge
remain charging authority; there is no Tesla BLE charge-start API or path.
Routine polling uses NoWakeSkip. Explicit manual and opt-in polling share one
wake authorization per SLAC session, never reset by reconnect. Connection itself
is not a physical no-wake guarantee.

Pair acceptance/enqueue/transport send is not paired. After PRESENT_KEY send,
WARP periodically requests fresh authenticated VCSEC status, tolerates key denial
while waiting for physical approval, and marks approval only through the final
SLAC/request gate. Raw OK/status cannot approve a key. Approval has an original
60-second horizon and bounded attempts, no automatic PRESENT_KEY retransmit,
and an acknowledged outcome slot resilient to event pressure. Simultaneous pair
and poll retain command ownership instead of replacing pairing correlation.

### Saved TDD and exercised gates

Behavioral assertion RED (exit 134) precedes GREEN (exit 0) for durable identity,
pairing verification, framing-refusal physical retirement, pairing pressure,
full-width physical retirement, RX overflow, simultaneous command ownership,
and forget/reconstruction without unintended rekey. The initial adapter and
module test compiler failures are retained and distinguished from behavioral
REDs. The first physical-generation GREEN-named attempt actually failed and
remains saved; `physical-generation-accepted-green` is the passing correction.
Source-only production binding has its own RED/GREEN. Already passing nested
R7/R8 schedules, module R9/teardown/no-wake/stale-SLAC schedules and filesystem
fault enumeration are characterization, not fabricated TDD pairs.

The migrated runner executes 18 preserved module-only helper entries plus the
clean suite (31 native entries), normally and under ASan/UBSan, exceptions/RTTI
disabled. Eleven module modes include pairing/SoC, refusal, pressure, generation,
overflow, lifecycle, actual worker/runtime teardown, no-wake/manual wake,
stale-SLAC result, simultaneous commands and forget. Compiler include traces are
saved. Original crypto matrices still pass (1,876 + 1,421 + 1,877 faults); the
bound descriptor path additionally exercises 25 syscall failures. mbedTLS
archives remain uninstrumented; none is a global OOM or hardware claim.

Independent R7/R8 public-port tests add both-domain denial/stale admission,
refusal/backoff/exhaustion and nested successor retirement at the supported
1/4-slot boundaries. The clean queue has four total slots, so legacy 31-tail
capacity is inapplicable; overflow is explicitly tested. The real module/GATT
R9 boundary now executes low-word-alias retirement and detached fresh RX.
These do NOT establish complete equivalence to all original schedules.

Production web/TypeScript pipeline passed. A clean BUILD-ONLY WARP4 firmware
link passed with required local framework libraries. Final current-source build
receipts are `replacement-firmware-current-clean-result.json` and
`replacement-firmware-current-build-result.json`; no upload
or target execution occurred. Fresh hashes/compiler-stack files are inventoried
in the final receipt. The earlier heuristic direct-disassembly stack audit is
incomplete and predates the last storage/module change: it is not a current
reachable-stack bound. Final compiler frames do not repair this limitation.

### Why removal and review readiness are withheld

Execution GREEN is not full regression-coverage acceptance. The explicit
`replacement-coverage.json` accounts for the original test names, preserved
helpers and currently excluded internal-coupled entries. Complete acceptance is
false: combined R7 recovery/refusal/successor schedules, remaining R8/R9 schedule
closure and applicable module/platform allocation/synchronization/text/real
LittleFS-close harness replacements are not completely audited/migrated.
The old tests are retained, not silently counted as passing or deleted. The
old protocol runner is not invoked by the new runner.

Therefore the old library and AGPL baggage are retained. Post-removal unrestricted
both-diff and whole-tree license/forbidden-reference gates are NOT executed or
claimed. Scoped diff checks were performed before the final documentation edits;
the final inventory includes their timing, not an invented post-edit pass. Prior
source diagnostics exclude the tainted current directory and are not a new
post-removal scan. Overall distribution/license clearance remains false.

Opaque screening covers 50 archived files versus 31 clean/new boundary files.
It retains the previously explained public storage declaration and additionally
reports a 127-token identifier-normalized match in the independently written
borrowing FunctionRef utility. This is a generic type-erasure/callable signature
pattern, inferred from allowed external GATT call requirements; no baseline text
was output or used, and no code was modified to hide the match. Classification
is provenance, not legal clearance or proof of independence.

The full continuation-start protected baseline includes the `.esp32` generated
metadata copy; its earlier chronology remains unproven. Exact final protected,
prior-evidence, source, branch/HEAD/index and artifact outcomes are recorded in
the final receipt, not assumed. The first AFTER inventory found that build tooling
had deleted that generated copy and changed PlatformIO `integrity.dat`. The copy
was restored byte-for-byte from its sibling after verifying the continuation-start
SHA-256; this is a disclosed transient deletion/restoration, not continuous
preservation. `protected-build-side-effects.json` records it. The final protected
gate remains NOT GREEN because `integrity.dat` differs from the start baseline;
no claim of full protected-byte preservation is made. Prior evidence roots remain
unchanged. The run's finite budget ended with production
binding and the firmware gate executed, before coverage closure and removal.

No Astra invocation/scheduling, GitHub upload, staging, commit, push, release,
hardware/Tesla access or flash/upload occurred. All test/build execution was
network-isolated. Continue from the coverage gate, not standalone primitives.

## Continuation 5 — expanded replacement coverage; final acceptance remains OPEN

Final verdict: PHASE 2 CHECKPOINT — NOT READY FOR REVIEW

Evidence and receipt:
`/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom-cont5/final-receipt.json`.
This checkpoint continues the accepted production TeslaBLEClean binding; it
does not repeat the earlier implementation or claim the excluded legacy tests passed.

All 48 excluded external test entries now have a per-entry behavioral
classification, external assertion locations/fingerprints, private/changed-surface
rationale and proposed replacement-test mapping in `replacement-coverage.json`.
Final coverage acceptance is FALSE. The initial ledger generation prematurely
marked acceptance true despite the latest failing acceptance execution; the
checkpoint explicitly corrects that flag and records the discrepancy. Mapping is
not execution or proof of every applicable contract. No excluded legacy test was
compiled or claimed passed, and no tainted implementation was opened or derived from.

New independent characterization covers 14 combined both-domain R7 schedules,
16 nested R8 drains, 82 real WARP R9 boundary schedules and 60 public key/session
save-outcome schedules around low-word wrap and terminal generation. The supported
four-command capacity is tested, not silently equated to old 31/32-tail internals.
Existing immutable leases, nested persistence, crypto faults and session lifetime
evidence are retained. Ephemeral sessions intentionally replace old private
session-cache persistence; the full VCSEC response UUID limitation remains.

Real behavioral RED (exit 134) followed by GREEN (exit 0) now covers:

- Authenticated malformed inner replies must retain pending correlation/replay
  until semantic decoding succeeds, allowing valid same-counter recovery. Both
  the public controller and sibling ChargeStateAdmission were fixed and tested.
  Receipts: `inner-admission-behavior-red` / `inner-admission-green`, and
  `sibling-inner-behavior-red` / `sibling-inner-green` (each with `-result.json`).
- First authenticated main publication before the initial coalesced key-presence
  notification must retain authenticated key possession. All three authenticated
  publication paths now handle this. Receipts: `module-chronology-first` /
  `module-chronology-green`; diagnostic failing execution is retained separately.

Already-passing characterization is not invented RED/GREEN. Tests compile the
actual ESP-platform mutex adapter against a faulting SDK boundary, actual target/
native WString/Config and validator/command bodies, and the retained external real
LittleFS-close backend. Storage adds 1,564 syscall faults, 280 cancellation cases,
short/corrupt IO and complete-delete faults across all four keys and lengths
1/6/65/129/4096. Actual WARP runtime/worker/composition/chronology tests cover
queue/task/allocation/stall/teardown/storage-load/storage-only failures.

`accepted-host-normal-result.json` and `accepted-host-sanitized-result.json`
passed the then-current 37-native-entry suite plus 18 preserved helpers, including
the original 1,876 + 1,421 + 1,877 deterministic crypto allocation faults. A later
38th entry (`test_key_schedules.cpp`) passed standalone. These are NOT final
latest-suite GREENs: the subsequently added real module `acceptance` mode aborted
(`module-acceptance-first-result.json`, exit 134). Its cause is not diagnosed at
this checkpoint. The failing test is retained and active, not weakened or hidden.
Resolve it, then rerun the full latest normal/sanitized suite before accepting
the ledger. Production web/TypeScript passed; clean build-only firmware after the
production fixes also passed (`accepted-firmware-clean/build-result.json`).

PlatformIO metadata reproduction uses the actual installed producer in disposable
metadata directories, with no protected restoration/deletion. `integrity.dat`
remained exactly SHA-256
`f1ec5a79ae1f3df04d095590e6198c943332a68597e983e08f7017db66696d34`
before/after the first continuation-5 clean build. The canonical dependency set is
reproducible and contains TeslaBLEClean, not TeslaBLE. The producer serializes a
Python set: three hash seeds yield different byte ordering for the same set, so
cross-process byte-order determinism is impossible. Historical pre-cont4 bytes
were not archived, only hashed; historical exact ordering cannot be recovered.
Further final package/protected/source inventory reconciliation is still required.
Firmware uses a disposable overlay over the required SDK output path so generated
build-copy deletion cannot mutate the original protected framework root.

NO removal occurred: `software/lib/TeslaBLE/` and the 48 excluded external sources
remain pending accepted coverage. Consequently post-removal unrestricted diffs,
whole-tree release/license/import/charge scans, opaque similarity, fresh final
ELF/map/stack and post-removal protected/source/index inventories were not run or
claimed. Earlier build hashes/inventories are not substituted for these gates.
No Astra, upload, commit, push, release, staging, hardware/Tesla access or flash.

## Continuation 6 — blocker diagnosed, contract acceptance locked, removal verified

Final verdict: PHASE 2 IMPLEMENTATION READY FOR USER-AUTHORIZED ASTRA REVIEW

Evidence root and final receipt:
`/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom-cont6/final-receipt.json`.
This is engineering implementation readiness for a separately authorized review,
NOT an Astra invocation, release/distribution authorization or legal clearance.

### Critical first task: genuine production defect, not a weakened harness

The original continuation-5 assertion RED and receipt were copied immutably with
their fingerprints, then reproduced unchanged (`acceptance-reproduced-red`, exit
134). The very first queue filler used an intentionally empty completion.
Clean `Vehicle::enqueue` rejected it. Allowed external behavioral contracts
explicitly permit absent/null callbacks (`test_tesla_ble_operational_enqueue.cpp`
lines 25-27 and `test_tesla_ble_module_acceptance.cpp` line 26); these excluded
sources supplied requirements only, were not compiled and are not called passed.

A separate independent public regression covering status, poll, authorized wake
and pairing first failed behaviorally (`optional-completion-red`, exit 134).
Only the shared clean admission guard changed: intentional absence is optional.
Checked callback invocation already makes absence a no-op. Every production WARP
enqueue call still checks callback preparation explicitly, so allocation refusal
does not silently become optional admission. Admission remains bounded and does
not allocate. The new test checks overflow, disconnect, expiry, reconnect,
authenticated completion and the explicit failed-preparation gate.

Both tests then passed (`optional-completion-green` and `acceptance-green`, exit
0). Original real module acceptance assertions were not weakened: refused
poll/pair must publish no false pending/in-flight state and consume no wake; the
subsequent accepted manual poll uses its retained one wake and authenticates SoC.
The original failing evidence remains present. No deletion happened while latest
acceptance or the complete current suite was red.

### Coverage acceptance and actual latest executions

The 48-entry `replacement-coverage.json` is locked against current test hashes,
assertion locations, explicit applicable contracts, private/changed-surface
rationales and fresh normal/sanitized receipts. No excluded legacy test is called
passed, source-compatible or executed. Acceptance concerns supported observable
contracts, not duplication of private object layouts, 31/32-tail capacities,
arbitrary commands or cached-session persistence.

A read-only audit identified three concrete gaps and one incorrect rationale.
These were closed before locking acceptance:

- Production descriptor **load**, not merely save verification: 1,756 injected
  canonical/backup open/fstat/every-partial-read/close faults and 120 partial-read,
  EINTR and EOF schedules across four keys and lengths 1/6/65/129/4096. Errors
  erase output; healthy retries do not mutate material; close failure remains
  globally quarantined across adapter recreation.
- Real WARP GATT identity contention: the allowed split-word snapshot remains
  independent of the checked state mutex. A paused unchanged writer during
  acknowledged submission must defer, then succeed after the delay seam without
  retirement; unavailable identity before submission refuses without retirement.
  The prior ledger's “identity moved to mutex” rationale was corrected.
- Real WARP storage-quarantine consumer: a saturated event queue cannot lose
  worker ACK, unknown key presence, reboot status/diagnostic or terminal restart
  refusal; teardown preserves storage. Real LittleFS failed-close ownership is
  separately retained, not confused with this SDK-boundary model.

These additional behaviors already passed production and are characterization,
not invented RED/GREEN pairs. A missing test include and a subsequent attempt
using the stale pre-recompile binary were retained as failed diagnostic attempts,
not production behavioral REDs; corrected characterization and complete suites
passed afterwards.

`coverage-host-normal/sanitized` passed before deletion. After deletion,
`post-removal-host-normal/sanitized` passed again with ASan/UBSan leak/halt options,
exceptions and RTTI disabled. Exact entry readback reconciles:

- 18 preserved WARP helper entries;
- 39 distinct Clean native entries (including the new optional-completion test);
- 19 actual WARP module execution modes, including pairing, acceptance,
  identity-busy and storage-quarantine.

`post-removal-entry-and-coverage-audit.json` checks exact sets, duplicates, mode
order and unchanged mapped test hashes. The Node web-action test also runs.
Focused R7/R8/R9 and deterministic crypto/key/storage/LittleFS/synchronization
matrices were rerun in both configurations: 22 fresh successful executions.
The existing 1,876 + 1,421 + 1,877 crypto fault counts remain. Storage retains
1,564 save syscall faults, 280 cancellation schedules, 32 IO/precedence cases and
24 complete-delete faults in addition to the new load matrix. These are not
whole-firmware OOM/power-loss or physical SDK proofs; mbedTLS archives remain
uninstrumented by the C++ sanitizers.

### Conditional removal and post-removal release-source gates

Production Web/TypeScript (tsc/esbuild/sass/postcss/HTML production pipeline)
passed. A clean BUILD-ONLY `warp4_tesla_ble` firmware link passed using the
required local framework-library path in an offline, empty-old-tree namespace
with a disposable SDK overlay. Pre-removal map, object count, dependency files
and host include traces were Clean-only.

Only after accepted coverage, latest normal/sanitized suites, focused matrices
and firmware GREEN, `software/lib/TeslaBLE/` and all 48 excluded `.cpp` sources
were deleted by exact ledger paths without opening them for deletion. Two unused
legacy fixture headers were then removed after confirming zero references in
accepted compiler include traces. The opaque original scratch archive remains
unchanged, SHA-256
`035ae75033dbd42dce11510062f72f91e79f64a946cc4ebadcbe7b27f622e924`.

Post-removal unrestricted worktree/index whitespace diff checks and the entire
working-source AGPL/Affero/import/forbidden-Tesla-charge-start scan are recorded
in the final receipts. Historical provenance/status documents and negative
assertion literals are explicitly classified, not silently omitted. Ordinary
authoritative non-Tesla WARP charging control is retained. Git internals, generated
build/dependency caches and credentials are not distribution source; package,
framework and artifact inventories govern those separately. No old executable
import or Tesla BLE charge-start path remains in release source or linked firmware.

The first broad scan correctly failed on the ignored, stale
`software/compile_commands.json`: 637 old-import fingerprints remained from an
earlier environment. This generated database was regenerated with the offline
build-only PlatformIO `compiledb` target, not deleted or blanket-excluded.
The failing scan/receipt is preserved; the accepted scan follows regeneration
and verifies the actual current database is Clean-only.

Final artifact readback then detected that `compiledb` had removed all five
generated firmware artifacts, including ELF/map/firmware/bootloader/partitions.
The failure was retained rather than using
old artifact fingerprints. A subsequent `final-firmware-clean/build` regenerated
all artifacts after compiledb; ELF/memory and both stack audits were refreshed
again. A first relocated ELF-audit attempt hit the immutable-output guard and is
retained separately; corrected relocation wrote fresh evidence under
`elf-final-accepted/`. Final artifact hashes and stack receipts below refer only
to this final rebuild, not the earlier pre-compiledb firmware.

Fresh opaque similarity covers 50 archived files and 31 clean/boundary C/C++
files. The two findings are unchanged: one public storage-outcome declaration
long line and one 127-token normalized generic borrowing-callback signature
pattern. Their fingerprints/locations and previously established public/generic
provenance are retained; no baseline excerpt was emitted and no code was altered
to hide matches. This is mechanical screening, not proof of legal clearance.

### Fresh post-removal firmware, memory and reachable-stack evidence

The final post-removal clean firmware rebuild passed, with Clean-only map/dependencies
verified again. Artifact hashes are in `final-binding-and-artifacts.json`
and the final receipt. Fresh ELF:
`26d621a4700a5e5c57f5cb5fd7d05f032fab1868bacf63de52818d65099c62c4`.
Fresh map:
`d403b081b797bf206a318ea5a0a72db264782b96357efc1cf3d68c8fc6d685b7`.
PlatformIO RAM is 92,244 / 327,680 bytes and flash 4,771,785 / 6,488,064 bytes.
ELF `.dram0.data` + `.dram0.bss` + `.noinit` also totals 92,244. The broader
0x3ff-address-region sum is 92,260 because it additionally includes 16 bytes of
RTC-fast reserved memory; those figures are not silently equated.

The generic ELF auditor no longer assumes any removed implementation callback
edge. Fresh ordinary and CFG-reachable instruction audits ran on this exact ELF.
CFG discovers 1,334 reachable functions, 1,644 straight-line literal call sites,
1,170 unresolved indirect sites, 918 instruction resynchronizations, seven
unresolved trap/indirect-jump sites and direct target `0x40091b4c`.
Discovered acyclic ENTRY sums: worker 10,176 bytes (configured 12,288), teardown
2,832 (configured 4,096). The linear decoder gives teardown 2,992 instead; its
padding/control-flow ambiguity is retained, not treated as the better bound.
`complete_static_bound=false`: unresolved callbacks/jump tables/ROM, recursion,
symbol extents/non-ENTRY adjustments and interrupt/RTOS reserve remain. None of
these sums is an upper bound, measured spare stack or hardware high-water proof.
Existing `.su` files are not claimed fresh or a complete compiler-frame inventory.

### Deterministic integrity reconciliation and final inventory

Complete BEFORE/AFTER readback finds all 124,188 protected files and symlinks
unchanged, including package/framework source membership and hashes. Earlier
checkpoint evidence and separate continuation-4/5 files are unchanged. Final
branch/HEAD/index readback confirms the original branch and HEAD with empty
index; no staging operation occurred.

`integrity-final-reconciliation.json` compares canonical dependency membership,
package/framework/source fingerprints and explicitly intentional clean-source
changes. Live `integrity.dat` bytes remained exactly SHA-256
`f1ec5a79ae1f3df04d095590e6198c943332a68597e983e08f7017db66696d34`.
The actual PlatformIO producer was reproduced with three hash seeds in disposable
metadata: identical canonical membership, three different byte orders. Byte-order
nondeterminism is reported explicitly; protected metadata was not changed to
force equality. Historical pre-cont4 order cannot be reconstructed from a hash,
and prior generated-copy chronology is not retroactively “repaired”. SDK overlay
changes never mutate the original protected framework root in this continuation.

Final source, Git, document, artifact and evidence-manifest hashes are in cont6.
An initial protected AFTER readback preceded the final Phase-2 document and
compile-database regeneration. A final complete AFTER readback and the final
source/document inventory include these changes; the protected package/framework
membership and hashes still remain equal. No documentation change is represented
as a clean-production-source byte-equality claim.

No Astra invocation/scheduling, GitHub upload, staging, commit, push, release,
hardware/Tesla access, flash/upload or publication occurred. Hardware/no-wake
interoperability, target heap/stack validation and legal/distribution clearance
remain outside this implementation-readiness verdict. User authorization is
required before Astra review; it has not been inferred or requested from a tool.

## First independent Astra review — B1/B2 fixes and post-fix verification

Final verdict: READY FOR USER-AUTHORIZED ASTRA RE-REVIEW

Evidence root and machine-readable receipt:
`/home/hermes/.hermes/cache/scratch/tesla-phase2-astra1-fix/final-receipt.json`.
Authoritative report:
`/home/hermes/.hermes/cache/scratch/astra-phase2-review1/review.txt`.
The report's NOT READY verdict and the Continuation-6 receipt are unchanged.
Only GPT-6.1 Sol implemented and verified these fixes. No Astra or other agent
was invoked or scheduled. No hardware/Tesla access, flash/upload/erase/serial,
stage/commit/push/merge/release, GitHub update or publication was performed.

### True RED and exact-probe GREEN

Before production changes, exact `protocol_probe_confirm.cpp` and
`run_protocol_confirm.py` were copied into permanent tests under
`software/tests/tesla_vehicle/cleanroom/astra/`. Probe SHA-256:
`a54624a05903a12772d4fca44eb6814b5d464dbf363cd1f52c231bfa49ce3b45`.
The ordinary-suite `test_astra_protocol.cpp` wrapper requires product success;
the archived expected-failure diagnostic driver is not the product gate.

Each of `replay-vcsec`, `replay-info`, `update-vcsec`, `update-info` independently
exited 1 normally AND with ASan/UBSan before the fixes, with empty stderr and
intact controls (`red-protocol.json`, `red-protocol-sanitized.json`). The evidence
driver's zero exit means expected REDs were collected, NOT product success.
The same eight individual modes now exit 0 with empty stderr
(`green-protocol.json`, `green-protocol-sanitized.json`). Both replay cases
complete two authenticated requests using response counter 1; old-request packets
remain rejected. Both update cases consume one SessionInfo (counter 100, clock
2000), retry with outgoing counter 101 and complete successfully, not Failed.
Bad-tag and foreign-UUID controls remain rejecting. Receipts retain exact
compiler argv, mode commands, exit codes, outputs and source hashes.

### Production fixes and official-source inspection

B1: ResponseGuard starts a new replay window only for a distinct UUID or
authenticated request hash. Cancel/rearm of the SAME scope preserves its used
counter rejection, even if caller context changes. Old packets fail the active
successor's UUID/hash/GCM correlation. Vehicle and sibling ChargeStateAdmission
share the fix. Outgoing high-water/reservation is unchanged. This is bounded
current-request state, not an unlimited UUID cache; callers must use secure fresh
UUIDs and must not resurrect completed older identities after replacing a scope.

B2: In WaitStatus/WaitCharge/WaitWake and any state still owning the accepted
active encrypted request, command-UUID SessionInfo is authenticated using the
established peer HMAC/public point. The guard is published only after accepted
full-frame TX. Full UUID/domain/routing/full-width generation, original total/
response windows and a hard five-second UUID-age cap apply. Captured RX time AND
current callback time are checked; refused or delayed TX callbacks cannot publish
sync early or beyond the operation budget. All fallible checks precede
allocation-free publication. Invalid tags, foreign/stale UUIDs, wrong domains/
generations, malformed bodies, same-epoch stale clocks and remembered retired
epochs leave state/correlation unchanged.

A valid new epoch can carry a lower clock. As in official signer.go:96-129, a
newer valid clock with a lower peer counter is accepted with the counter clamped;
neither session counter nor outgoing high-water rolls back. The next successful
outgoing command exceeds the accepted peer counter. Accepted sync consumes the
request once and uses ordinary 100 ms retry backoff without resetting the
eight-attempt/total budget, rekeying, persistence or a fresh handshake. An exposed
wake is never resent; recovery proceeds through status. Eight retired epochs are
remembered per live session; exhaustion refuses further epoch changes without
evicting old history. Reconnect resets that finite history, not high-water.
Unlimited arbitrary-duration epoch-history support is not claimed.

Only current Clean sources/tests and the clean official Apache checkout at
`a4b43c1eff0e09d77deb9f2dce97031141fe8c8a` supplied implementation requirements.
Independently inspected official sections: protocol.md:711-731,779-786;
receiver.go:29-38; dispatcher.go:158-173,182-223; session.go:48-58,113-129;
signer.go:96-129. Hashes/pin are in `official-reference-inspected.json`.
No removed implementation, historical implementation diff or archive contents
were consulted. Existing integrity/similarity gates alone consume the opaque
archive and emit fingerprints/locations/counts, never source excerpts.

### Focused coverage and all fresh post-fix gates

Four added active native entries: test_astra_protocol, test_request_replay_scope,
test_command_session_update, test_astra_faults. They cover both domains, full-state
invalid-update negatives, nonrollback/epoch-history exhaustion, UUID-age/deadlines,
old UUID/hash/GCM, same-request replay, sibling admission, staged/refused/delayed
TX, disconnect/full-width aliases/terminal UINT64_MAX, nested completion reentry,
bounded recovery and once-only wake. Ten observed calloc failures are injected
across both domains' GCM/HMAC paths with no publication/state change on refusal,
same-object healthy retry, no throwing new and balanced crypto ownership. These
are matrix-local observed boundaries, not a whole-firmware allocation proof.
An initial new-test compile lacked the fixture search path; its retained
`command-update-first-result.json` is a harness error, not behavioral RED.

Complete current post-removal suite commands both pass in offline RO-lower overlays:

    bash software/tests/tesla_vehicle/run_host_tests.sh
    bash software/tests/tesla_vehicle/run_host_tests.sh sanitize

`post-fix-coverage-audit.json` reconciles exact sets: 18 preserved helpers,
43 distinct Clean native entries and 19 actual WARP modes in EACH configuration,
plus Node web actions and Python vector/safety/binding checks. No removed legacy
source was executed or called passed. All 48 old mapping records and mapped test
hashes remain unchanged. This addendum amends the B1/B2 coverage gap, particularly
records 29/30/31/32/40, rather than rewriting historical blanket acceptance.
All R7/R8/R9 and focused replacement matrices were rerun normally/sanitized:
22 successful executions in `focused-summary.json`. The original 1,876 primitive,
1,421 lifecycle and 1,877 controller allocation failures, 50 identity transactions,
60 key/session schedules, 82 actual WARP boundaries, 1,564 save faults/280
cancellations and 1,756 load faults/120 IO schedules remain passing. Counts are
matrix-local, not disjoint global totals or SDK guarantees.

The actual production Web/TypeScript helper passes tsc/esbuild/sass/postcss/HTML
minification (`web-production-result.json`). Offline clean BUILD-ONLY WARP4 passes:

    TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs
    uv run --offline --directory software pio run -e warp4_tesla_ble -t compiledb
    uv run --offline --directory software pio run -e warp4_tesla_ble -t clean
    uv run --offline --directory software pio run -e warp4_tesla_ble -j 4

Compiledb precedes the final clean/build, so its artifact deletion cannot invalidate
the final ELF/map. The final clean/build use an initially empty evidence-owned
SCons cache: all 13 Clean production units freshly compile, zero cache retrievals
(`uncached-build-proof.json`). Artifacts physically reside in the new evidence
root under `firmware/project-upper/software/.pio/build/warp4_tesla_ble/`; original
project artifacts and package/framework lower trees remain preserved. No upload.

Final ELF/map, 778 compile-database entries including 13 Clean units, 651 target
dependency files and current host include traces bind TeslaBLEClean only. No
TeslaBLE:: symbol or removed import/dependency path remains
(`uncached-binding-artifacts.json`). The ignored old library name is a negative
exclusion, not a linked dependency. SLAC MAC remains the binding vehicle identity;
BLE address remains transport only. Routine polling remains NoWakeSkip; no Tesla
charge-start was added. Original transactional persistence/publication and
64-bit lifecycle guards remain exercised by the passing unchanged matrices.

### Resources, source screening and final reconciliation

Final RAM: 92,244 / 327,680 bytes; flash: 4,773,029 / 6,488,064 bytes.
Broader ELF allocated 0x3ff-region sum: 92,260, not a runtime heap budget.
ELF SHA-256: `f3fdddb3983e8aa5a734daa5a24b9ac62c5db4414dcb370f7ad750b25e1024b2`.
Map SHA-256: `2852b2de6f3b786333fa9d307b65bec4408026cd9691cdabd0430163c6552f33`.
Fresh raw-section and CFG-reachable auditors ran on these exact artifacts. CFG:
1,339 reachable functions, 1,179 unresolved indirect sites, 14 cycle nodes, one
unknown direct target and seven unknown decode/indirect-jump sites; 651 fresh
compiler stack-usage files. Discovered worker ENTRY sum: 10,288 versus configured
12,288; teardown: 2,832 versus 4,096. These are NOT upper bounds, spare-stack
guarantees or target high-water measurements. Indirect/virtual calls, recursion,
ROM, decoder/symbol/jump ambiguity and RTOS/interrupt reserve remain unresolved.

Unrestricted worktree/index diff checks and both whole-source and independent
extension-unrestricted UTF-8 scans pass with no unresolved license/import/Tesla
charge-start hits. Historical documents/negative literals and authoritative
non-Tesla WARP control are classified separately. Clean SPDX, byte-identical
official Apache LICENSE and Tesla NOTICE attribution pass. Mechanical opaque
screening retains the same two explained public/generic matches; it is not
independent-authorship proof or legal clearance.

Protected/package/framework/prior-evidence inventories reconcile exactly against
the new BEFORE baseline, including the first review. Final source changes are
restricted to the fixes, four new tests/exact probes, runner include/wrap flags
and three documentation updates. Tested non-Markdown/build input hashes reconcile
exactly. Final source/artifact manifests and command/log/artifact hashes are in
the receipt. The removed TeslaBLE tree, all 48 excluded legacy tests and two
removed unused fixture headers remain absent. Branch/HEAD/raw index and original
project build artifacts are unchanged. Historical earlier metadata chronology
has not been retroactively reconstructed.

### Remaining limitations — no hardware or legal acceptance

- No WARP/Tesla/RF/interoperability, physical pairing, sleep/wake, target heap or
  stack-high-water measurements; no deployment acceptance.
- Static stack evidence is incomplete. Authorized target high-water and RTOS/
  interrupt-reserve validation remain necessary for later hardware acceptance.
- Host mbedTLS archives are uninstrumented. Modeled SDK/RTOS/deep-copy/cancellation
  boundaries and observed OOM enumeration do not certify all crypto/SDK internals,
  shared Config/API or whole-firmware OOM. Quarantine/reboot may be required.
- Injected storage IO/cancellation old-or-new outcomes are not power-loss or
  physical flash proof; uncertain LittleFS close ownership remains quarantined.
- Strict full VCSEC UUIDs remain required; omitted-UUID, plaintext/older-firmware,
  arbitrary-command and removed-private-API compatibility are not claimed.
- Ephemeral sessions, four commands/four deferred frames and bounded retired-epoch
  history define an allowlisted WARP4 subset, not a general Tesla client.
- Pinned custom Bluetooth SDK/WARP4 only; static Bluetooth memory and whole-system
  pressure remain relevant. No real external-SDK cancellation certification.
- Source/similarity/license screening is not legal, patent, authorship,
  release/distribution or publication approval.

This was readiness only for a separately user-authorized Astra re-review.

## Independent Astra re-review — Phase 2 approved with explicit limitations

Final verdict: `APPROVE PHASE 2 WITH EXPLICIT LIMITATIONS`.

The authorized second independent review verified the post-fix receipt
`e135e1e8f756bbd8bc3b146499eae62b3c9d64f2264c9356802ab345e1ff91b7`
and confirmed both original blockers fixed in current source:

- B1: response replay admission is request-scoped. New UUID/request hashes may
  independently use the same response counter; same-request replay and old
  request packets remain rejected in VCSEC and Infotainment.
- B2: established-peer, HMAC-authenticated SessionInfo on the live encrypted
  command UUID updates either domain transactionally. Peer counter 100 resumes
  at outgoing counter/high-water 101; invalid tags, identities, UUIDs, domains,
  clocks, epochs, deadlines and stale generations do not mutate state.

The original four confirmation modes passed individually, normally and with
ASan/UBSan. The review also retained and verified the complete host suites, 22
focused R7/R8/R9/replacement executions, production Web/TypeScript pipeline,
empty-cache WARP4 firmware build, Clean-only ELF/map/dependencies, license/source
screening and source/artifact reconciliation.

A review-only delayed-TX observation was classified as timely complete-frame
arrival followed by delayed callback return, not a material product defect.
Complete arrival exactly at or after the response deadline is rejected. A timely
same-request encrypted response may be published later while accepted transport
ownership plus transmit/total budgets remain valid; command-UUID SessionInfo also
rechecks current callback time. Normal and sanitized review matrices each passed
172 deferred, 24 arrival, four successor, ten callback/ownership, two bounded-wake
and 16 combined arrival checks. No stale packet completed successor work.

Authoritative review artifacts:

- `/home/hermes/.hermes/cache/scratch/astra-phase2-rereview2-cont/review.txt`
  (SHA-256 `5b02bb8c1221807ed411b4589874ecdbbe5a3364c9a7760a8b69875e7d909816`)
- `/home/hermes/.hermes/cache/scratch/astra-phase2-rereview2-cont/summary.json`
  (SHA-256 `8b31ef61f68a00110e315fa55db627e40609ff034cf6cbb2e6a223f53ebfd49d`)

Approval is limited to the reviewed software subset. Tinkerforge must still
perform WARP4/Tesla hardware, RF, real pairing, sleep/wake, interoperability,
target heap and stack-high-water validation. Reachable-stack results remain
incomplete rather than upper bounds. Source/license/similarity screening is not
legal, patent, distribution or release approval. No hardware was accessed or
flashed and no end-user hardware test is planned by this project.
