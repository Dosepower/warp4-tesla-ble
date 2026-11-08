# Candidate remote enrollment protocol v1 — review input, not release approval

Status: integrated into both production peer paths and offline-exercised. No authenticated operational sessions or charge/status adapter yet. Default remote enable remains OFF. No installation authorization is implied.

## Trust boundary

The Tag has no display or out-of-band comparison code. Selecting the rapidly blue-blinking Tag and pressing it is physical consent to an initial TOFU enrollment, not strong authentication against an active MITM during first key exchange. P-256 key confirmation proves possession of the negotiated key, not a factory identity. Native German/English UI keeps this limitation visible. BLE address, RSSI, scan result and the one-byte framing cookie confer no authority.

Future operational sessions must authenticate a fresh challenge with the persisted root, use separately derived directional/session keys and replay counters, and reject prepared or committing records. No application charge writer exists in this slice. Tag actual-mode and actual-car caches remain invalid; no fabricated SoC/power/mode/ACK is supplied.

## Production path

WARP `TeslaVehicle::remote_load` installs the `RemoteEnrollmentLink` port into `RemoteSetup`/`RemoteBlinkClient`. Loading this port does no radio work. Explicit native start/select admits one bounded scan/connect lease from `TeslaNimbleCentral`; it cannot preempt Tesla. Callbacks copy at most20 bytes into an eight-entry queue. Generation/epoch checks, terminal SDK cleanup and the existing main/host drain barrier remain in force before reuse.

Tag local identity2, peripheral role and attempt-generation checks admit this GATT profile. Original owner-phone local identity0 and L4/SMP/MITM requirements remain separate. Only the main loop operates crypto/settings. The write callback validates/copies at most20 bytes into a four-entry queue; full queues reject the write. Owner iPhone bond and all tracker/Apple/Google/battery/high-water namespaces are untouched.

Selection starts a fresh Tag CSPRNG attempt. The input classifier sees SETUP, not CONFIRM, until the Tag has verified the WARP's authenticated hello receipt for both exact hellos. SETUP->CONFIRM transition invokes the existing quiet/context fence; an old press/release cannot confirm the new transcript. Confirmation generates proof1. A CSPRNG failure rejects enrollment. No deterministic scalar/nonce exists in production (only host fixtures).

## Wire framing

Minimum ATT MTU23 is sufficient:20-byte value, six-byte header, at most14 payload bytes.

Header: `a6 | cookie(nonzero,u8) | type(u8) | offset(u8) | total(u8) | version(1)`.

Type1 carries96 bytes (raw P-256 X||Y,64 bytes; fresh nonce,32 bytes). Types2..8 carry authenticated proof1..7 (17 bytes: one encrypted kind plus16-byte CCM tag). Type9 is the authenticated hello receipt (kind0).

Only exact96/17-byte totals are admitted. Oversized/truncated/changed duplicate, different-cookie/type interleave, gap or illegal offset rejects the attempt. Identical duplicate ranges are harmless and do not repeat persistence. Messages are bounded; complete output is retained for exact retransmission, every500ms within the absolute30s attempt, maximum60 retries. This is an ordered BLE link: arbitrary reordered/missing middle fragments fail closed rather than speculate. Cancellation/timeout/disconnect destroys provisional state. A new attempt requires fresh crypto even when the small framing cookie repeats.

`RemoteBlinkClient` advances the immutable submitted fragment on accepted submission, not when processing a possibly delayed ATT Write callback. The callback clears in-flight admission only. Both reply fragments may arrive before that callback; the real two-production-peer host test explicitly exercises this ordering. Submission is never success: authenticated committed proofs plus checked durable storage govern completion.

## Key derivation and proofs

Primitives: Nordic Oberon3.0.20 (Tag), mbedTLS (WARP). Raw P-256 points/scalars are checked by the providers. ECDH X-coordinate is HKDF-SHA256 input; salt is ordered WARP nonce||Tag nonce; info is the fixed protocol domain and ordered public keys. HKDF derives a32-byte enrollment root. A distinct HKDF info domain derives a shared32-byte binding ID. The binding ID is not an authenticator by itself.

Proof CCM uses a13-byte domain/kind nonce,4-byte version/kind AAD,32-byte AES key and16-byte authentication tag. Each kind is encrypted once per fresh root and cached retries preserve identical ciphertext. Kind0 confirms both hellos before physical confirmation is possible. Opposite-role proof kinds and exact expected sequence reject reflection/replay/cross-transcript input. Scalar and ECDH intermediate are erased after derivation; cancel/reset erases root, scalar, nonce/public hello and fragment buffers. Persistence holds the root only in the dedicated private binding record, never in public API/HTML/logs.

Sequence:

1. WARP hello; Tag hello; WARP kind0 hello receipt.
2. Physical Tag confirmation -> Tag kind1.
3. WARP verifies kind1, durably PREPARE/readback -> WARP kind2.
4. Tag verifies kind2, durably PREPARE/readback -> Tag kind3.
5. WARP verifies kind3, durably COMMIT-PENDING/readback -> WARP kind4.
6. Tag verifies kind4, durably COMMIT-PENDING/readback -> Tag kind5.
7. WARP verifies kind5 -> WARP kind6 (mutual durable-commit confirmation).
8. Tag verifies kind6, creates kind7, durably activates/readback; sends complete kind7 -> bounded local green. It retains exact final receipt for retry until peer disconnect/timeout.
9. WARP verifies kind7, durably activates/readback -> `paired_tofu`/trusted UI, then disconnect/drain. A receipt loss cannot produce WARP success.

This is not an atomic distributed transaction. Last-packet loss or power loss can leave one active peer and the other commit-pending. Pending records never authorize, are not counted as trusted after boot, and cannot be silently replaced. Active records were reached only after authenticated peer durable-commit evidence. Automatic cross-reset completion/reconciliation and explicit Tag reset/forget UX remain engineering work; never erase tracker settings or convert pending records into trusted records merely to recover usability.

## Durable formats

Tag `warp_remote/binding_v1`:77 bytes, magicWRT1, phase(1 prepared/2 commit-pending/3 active), shared binding ID32, root32, monotonic high-water8 big-endian. Exact key-specific settings load/save only. Missing is different from malformed/read-error; malformed state latches fault and blocks admission. Every transition writes then independently reads and compares bytes. Existing record blocks a different binding/root and backward phases. Counter reservations are checked/read back before use.

WARP NVS `warp_remote/bindings_v1`: existing fixed four-slot WRB1 collection, dedicated namespace. State1 prepared, state3 commit-pending (never trusted), state2 active. Activation now requires state3; a simple prepare cannot activate. Each stage commits and verifies the entire record. Readback failure quarantines memory. Per-peer forget remains individually confirmed. Pending records consume capacity; UI handling/recovery for these records needs completion before release.

## Evidence and open acceptance

- Real `TeslaVehicle` API/shared NimBLE + real Tag GATT/gesture/secure/settings wrappers: fragmented enrollment, replies-before-write-callback, twelve write/readback phase failures across both peers, dropped final receipt/retry, duplicate notifications, no charge/reboot.
- Actual two-peer enrollment engines plus real WARP/Tag stores: persistence interruption/readback matrix and reload, exact retry/lost final receipt, timeout/cancel, changed cookie/ciphertext, nonce/transcript mismatch, no silent rebind.
- Independent runtime vectors: actual ARM Oberon archive instructions under Unicorn vs real host mbedTLS vs OpenSSL-backed Python cryptography, including P-256 public/ECDH, RFC5869 HKDF, CCM and bad-tag zeroization. This is primitive qualification, not physical RF/SoC execution acceptance.
- Independent application-protocol/code review, stronger full-GATT loss/cancel/queue/power-loss scenarios, pending-state recovery, operational sessions, evcc applied-state adapter, fresh authenticated car telemetry, canonical OTA packaging and physical/RF/power/phone/battery/color acceptance are still required. Parent owns independent review and installation approval.
