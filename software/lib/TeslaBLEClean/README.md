# Clean-bound WARP4 Phase-2 implementation

Apache-2.0; see LICENSE, NOTICE and PROVENANCE.md. WARP and the firmware environment
use this core only. Continuation 6 locks all 48 excluded-entry supported external
contract mappings against fresh normal/sanitized executions, then removes the old
library and excluded tests by path. No excluded test is claimed passed.
The first independent review subsequently found two omitted external protocol
contracts. The request-scoped replay and command-UUID SessionInfo fixes and fresh
post-fix evidence supersede that blanket acceptance. A second independent review
then concluded `APPROVE PHASE 2 WITH EXPLICIT LIMITATIONS`; see
`TESLA_BLE_PHASE2.md`. Prior receipts remain historical. Hardware,
publication/distribution and legal acceptance remain separate Tinkerforge gates;
mechanical provenance screening is not legal clearance.

Supported, independently tested primitives:

- Bounded ordered metadata serialization and SHA256 hashing.
- NIST P-256 generation/scalar import/unencrypted PEM import/export; static ECDH
  with fixed-width x-coordinate and Tesla's required SHA1 truncation.
- Labeled session-info HMAC-SHA256 and out-of-place AES-128-GCM.
- Strict bounded SessionInfo decoder and authenticate-before-output publication;
  authenticated key denial is not an established session.
- Per-request response replay window; request/domain/UUID/hash/counter/flags/fault
  authentication, once-only completion and explicit receive deadline admission.
- Length-prefixed BLE framing, bounded fragmented/concatenated RX, timeout,
  detached callback reentry and full-width transport-generation checks.
- Checked key publication with one durable candidate across nested same-storage
  saves; independent pending/established identity leases, explicit commit/cleanup
  outcomes and restart quarantine.
- Checked no-exceptions callbacks (pinned invocation and safe capture moves),
  mutex initialization/lock port and bounded key-storage port.
- Generation-owned challenge admission, deferred synchronous RX until transport
  acceptance, bounded authentication retry/backoff/deadlines and signed updates.
- Charging-manager PRESENT_KEY pairing payload, VCSEC sleep/pairing status and
  explicitly authorized wake payload only; the standalone codecs grant no policy authority.
- Personalized AES-GCM routable request codec and transactional ENCODE counters;
  routed encrypted-response decoder and authenticated GetChargeState SoC subset.
- SessionInfo request/reply routable wire binding and a public bounded `tesla_clean::Vehicle`.
  Four total FIFO commands (including active), four staged synchronous RX frames,
  per-domain leased authentication and per-request replay, shared eight-attempt transmission budget,
  100 ms retry backoff and original total/response/per-transmit deadlines.
  Framed status, no-wake-or-explicitly-opted-in polling and GetChargeState now reach
  an exactly-once authenticated SoC completion. WARP now checks its current
  runtime/request/SLAC-MAC authority again before calling the IEC-side SoC setter.

API constraints:

- Single worker only. These are not thread-safe public client objects.
- `Random` must be a secure caller-supplied C RNG in production. RNG callbacks
  must not reenter or mutate a key under construction. Tests' deterministic RNG
  and fixed nonce are PUBLIC fixtures only, not production entropy sources.
- Key imports/generation stage a separate crypto handle; failure retains the
  current key. `IdentityStore::rotate` publishes only after an explicit storage
  commit. Nested rotation may join the same storage/candidate, to depth eight;
  other mutation/storage transactions are refused. Publication references and
  PEM bytes are prepared before save. The storage backend must honor its stated
  durable-commit contract; this port is NOT a POSIX/LittleFS implementation or
  power-loss guarantee. Restart quarantine cannot be cleared in-place.
- Leases retain their original cryptographic material across key publication;
  leased `key()` is borrowed and must NEVER be imported/generated/mutated by a
  caller. The revision is saturating diagnostic data, not identity equality.
- `CheckedCallback::assign` accepts only nothrow construction/destruction of its
  capture from the provided argument and checks its own allocation. Callers'
  capture creation and callback bodies may still allocate: no global OOM safety
  is claimed. Allocator/mutex/backend callbacks must not throw or destroy their
  owner. All these objects require single-worker ownership, not shared-thread use.
- PEM input length excludes the terminal NUL; output length excludes it too.
- `Bytes` spans must point to live storage of the indicated length; null+zero
  means empty. No ownership is transferred. GCM rejects overlapping input/output
  spans; refused output is erased. Ciphertexts/plaintexts are bounded to 1024.
- `ResponseGuard` borrows a session crypto object, which must outlive it and must
  not be rekeyed. Construct a new guard on a genuinely authenticated new session.
  Canceling/rearming the SAME UUID/request hash does NOT reset its replay history.
  A distinct UUID or authenticated request hash gets an independent response
  counter window. Old packets do not correlate with the active successor. Secure
  UUID generation remains required: callers must not resurrect completed older
  request identities after replacing their scope; this is not an unbounded UUID cache.
- A verified response with nonzero `VerifiedReply::fault` is an authenticated
  error, NOT successful SoC. The WARP adapter publishes only the admitted SoC result.
- SessionInfo output from the decoder alone is UNTRUSTED. The authentication
  primitive alone does not check an outer UUID/domain/generation. The separate
  `DomainAuthentication` binds them, a pending identity and original deadlines,
  and trusts the established session HMAC for updates/denial. Recovery permits
  three attempts with 100 ms backoff per context/generation and cannot move a
  running operation's deadline. Its private serial never wraps; UUID collision
  rejection covers the current/last UUID, not every historical UUID. Secure RNG
  remains essential. RNG reentry is unsupported.
  `receive_update` additionally authenticates an established-peer SessionInfo
  carried on an active encrypted-command UUID. The Vehicle supplies only a guard
  published after accepted full-frame TX, checks both RX and current callback time,
  and consumes synchronization once before a bounded retry. Response/total
  deadlines and a hard five-second UUID age apply; no identity/persistence/rekey
  transaction or fresh handshake is started. Same-epoch stale clocks and known
  retired epochs refuse without state changes. Newer valid clocks with lower peer
  counters are accepted but counters are clamped, as in the official signer.
  Eight retired epochs can be remembered per live session; exhaustion refuses
  further epoch changes without eviction. Local reconnect resets this bounded
  history, not outgoing high-water. This finite fail-closed limit is not general
  arbitrary-duration epoch-history support.
- Reset/identity replacement must occur only after borrowers of session crypto
  (response guards/outgoing encoders) have been retired. `crypto()` is null when
  not ready, but an already borrowed raw pointer is not magically revoked.
- `OutgoingMessages` requires an already authenticated SessionInfo, corresponding
  local public point and long-lived immutable session crypto. It commits a
  counter only after complete encoding, NOT after transport acceptance. Binding
  must preserve the outgoing high-water counter over session resynchronization,
  queue retries and refused/partial transport. There is no transport transaction
  or session-update ledger binding in this standalone codec yet. Payload cap is
  800 bytes, encoded cap 1024, TTL 1-60 seconds, and counter/expiry overflow refuses.
- A decoded encrypted response is untrusted until the response guard admits it.
  SoC requires successful authentication, zero signed fault, successful explicit
  CarServer action status, present battery_level and a value 0-100. Missing
  usable/battery fields are not coerced to zero. Pairing/status codecs alone are
  never evidence for SoC publication or permission to wake.
- Clock ages use unsigned 32-bit elapsed milliseconds with caller-supplied
  deadlines at most INT32_MAX. Caller clocks are monotonic modulo wrap; arbitrary
  full-cycle reuse/ABA is not solved. Full-width link tokens also have a finite
  wrap horizon. Framing admits at most 8208 input bytes per invocation, with a
  1024-byte payload, and discards incomplete data after a 1000 ms fragment gap.
- SessionInfo rejects duplicate known fields, groups, unknown status enums,
  non-P256-sized public points and non-16-byte epochs. Omitted proto3 scalar
  defaults and bounded unknown ordinary fields are supported.

Vehicle constraints (continuation 3):

- `IdentityStore`, RNG/clock/link/write contexts and their storage must outlive the
  controller. Only write/completion/framing-recovery callbacks may reenter lifecycle
  operations. They must not destroy the controller; RNG, clock and ownership checks
  must not reenter. All external SDK calls remain potentially noncancellable.
- `open(generation)` is idempotent for the current live token. Reopening a retired
  token or wrapping past UINT64_MAX refuses. `close` ignores stale tokens, detaches
  all old commands before callbacks, and preserves callback-created successors/RX.
- Command admission does not allocate; completion is optional. An intentionally empty
  completion admits supported operations and terminal invocation is a no-op. Callers
  needing notification must check `assign` before enqueue: a failed allocation must
  not be mistaken for intentional absence. WARP explicitly gates all its enqueue calls.
  Public
  context reuse is allowed after retirement, but private operation budgets are unique
  and nonwrapping. Queue time consumes the original total deadline; admission/execution
  refuses zero or greater-than-INT32_MAX deadlines. Exact deadline arrival expires.
- Response admission timestamps the complete frame at delivery to the controller.
  A timely encrypted reply for the same request can therefore be published after
  its response deadline if an already-started accepted transport callback returns
  later and transmit/total ownership remains valid. This is not stale-successor
  admission. Command-UUID SessionInfo additionally rechecks current callback time.
- Successfully encoded counters are reserved before possible transport exposure,
  never rolled back on refusal, and retained across signed refresh and reconnect.
  Request correlation is published only after the entire frame is accepted. Staged
  RX is discarded on refusal/overflow; it cannot grant a session or SoC early.
- A refused/abandoned framed prefix cannot safely be followed by another frame.
  `recover_frame` may return true ONLY if the backend can prove the peer framing
  stream is clean/reset. Otherwise the controller locally retires the link and
  completions once. The WARP adapter never asserts same-link stream reset: it
  closes the physical GATT link before exposing retired completions. Cancelled
  WRITE uses a separate CLOSE slot and quarantines the runtime. These paths are
  exercised against host SDK fakes, not a physical BLE peripheral.
- Routine polling first requires authenticated VCSEC awake status. Asleep with
  `NoWakeSkip` completes without wake or Infotainment. Explicit wake authorization
  is consumed before possible exposure; refusal/timeout/signed recovery uses status,
  not a second wake. BLE connection itself is not a hardware no-wake guarantee.
- `PairRequestSent` means PRESENT_KEY bytes were accepted by the transport, NOT that
  the key was approved/paired. Physical approval and a subsequent authenticated
  exchange are required. WARP waits for a fresh authenticated VCSEC status,
  tolerates authenticated key denial while awaiting approval, and uses its final
  SLAC/request gate and acknowledged outcome slot for approval publication.
  Raw pairing status never grants approval or SoC; verification has a 60-second
  original horizon with bounded 5-second attempts and no PRESENT_KEY retransmit.
- The wire decoder deliberately requires a complete 16-byte response UUID in both
  domains. The official VCSEC dispatcher can route an omitted UUID; that variant
  is NOT supported here. There is no hardware/interoperability claim.

WARP adapters use checked nothrow callback allocation, native checked mutexes,
bounded POSIX descriptors on the shared LittleFS mount and durable identity
rotation/import. Startup/forget reconstruction never auto-generates an absent key;
explicit pairing durably creates it. Production uses esp_fill_random; deterministic entropy
exists only in host SDK fixtures. Disabled setup remains storage/runtime-free;
runtime stop preserves worker ACK, callback draining and quarantine ownership.
Routine polling uses NoWakeSkip; explicit manual and opt-in polling share one
wake authorization per SLAC session. A BLE connection itself may wake a vehicle.

The supported public-behavior mapping covers R7/R8/R9, module/platform allocation,
load/save/LittleFS close, synchronization, optional callbacks and queue refusal.
The runner executes clean sources plus 18 explicitly preserved WARP helpers.
The four-command queue has no 31-tail case; overflow is independently exercised,
not silently omitted. Private cached sessions and arbitrary command APIs are not
implemented or claimed source-compatible. The full VCSEC UUID limitation remains.
Fresh linked ELF/compiler-frame evidence is not a complete reachable-stack bound
or target high-water measurement. There is no charge-start command and no
hardware/interoperability claim.

Run from the repo root:

    bash software/tests/tesla_vehicle/cleanroom/run_suite.sh
    bash software/tests/tesla_vehicle/cleanroom/run_suite.sh sanitize

The runners independently build matched local mbedTLS archives when absent,
then build the clean core and applicable WARP module adapters. They never invoke
the old protocol runner. Python fixture verification requires cryptography.
Core C++ is compiled with exceptions and RTTI disabled. Focused ASan/UBSan
instruments the C++ core/tests; the existing mbedTLS archives themselves are not
rebuilt with sanitizers. None of this constitutes a target OOM or stack proof.
