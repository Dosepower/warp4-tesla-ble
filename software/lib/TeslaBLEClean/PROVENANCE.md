# Phase 2 source provenance and license firewall

Current status (Continuation 6): the WARP binding and supported external-contract
mapping are accepted; the old library and 48 excluded source paths were removed
only after fresh host normal/sanitized and firmware gates passed. Historical
checkpoint statements below remain chronological provenance, not current status.
The opaque scratch archive remains unchanged. No removed implementation contents
were read, emitted or used in this continuation. Final post-removal evidence and
engineering-review readiness are in TESLA_BLE_PHASE2.md and cont6/final-receipt.json;
neither similarity classification nor this file grants legal/distribution clearance.
Current-status correction: the first independent review found the response-counter
scope and command-UUID SessionInfo gaps in that acceptance. The fixes and fresh
post-fix gates are documented in TESLA_BLE_PHASE2.md and
tesla-phase2-astra1-fix/final-receipt.json; older receipts are unchanged history.

This file was established before production implementation and retains the
chronology of that clean-room boundary. The former `software/lib/TeslaBLE/` was
treated as tainted and was never an implementation source: no source file in
that directory was opened, diffed, copied, translated or used as a template.
After independent replacement coverage and Clean-only firmware gates passed, the
directory was removed. No historical implementation diff was used.

The sole protocol authority is Tesla's official Apache-2.0 vehicle-command tree:
`/home/hermes/.hermes/cache/scratch/tesla-vehicle-command-apache`, pinned and
verified clean at `a4b43c1eff0e09d77deb9f2dce97031141fe8c8a`.

## Official source sections used or reserved for this checkpoint

Paths relative to the official tree; hashes are recorded in the evidence source
manifest. No alternative third-party implementation is a protocol source.

| Official source | Section | Use |
| --- | --- | --- |
| `LICENSE` | complete license | Apache-2.0 license text |
| `internal/authentication/metadata.go` | Add, AddUint32, Checksum (33-56,80-84) | ordered TLV, big-endian integers, terminal byte |
| `internal/authentication/metadata_test.go` | TestCheckSum (45-65) | official SHA256 metadata test vector |
| `internal/authentication/native.go` | 38-88,94-145,147-204 | AES-GCM, label-derived HMAC, session-info HMAC, P-256/ECDH, PEM and public key encoding |
| `internal/authentication/crypto.go` | 12-22,32-48 | labels, sizes, replay-window policy |
| `internal/authentication/ecdh.go` | 10-18 | shared key size and exchange interface |
| `internal/authentication/ecdh_test.go` | 10-18,46-100 | leading-zero ECDH vector; invalid point/scalar negatives |
| `internal/authentication/native_test.go` | 37-87 | public point format / fixed-width shared-x requirement |
| `internal/authentication/window.go` | 3-68 | 32-position out-of-order replay admission; no counter wrap |
| `internal/authentication/peer.go` | 33-117 | request/response metadata and request-ID semantics |
| `internal/authentication/signer.go` | 48-61,114-176,215-242 | authenticated session-info, counter rollover exclusion and decrypt/counter contract |
| `pkg/protocol/protobuf/signatures.proto` | 8-94 | metadata tags, signature types and SessionInfo wire schema |
| `pkg/protocol/protobuf/universal_message.proto` | 10-20,63-101 | two domains, routable envelope and UUID fields |
| `pkg/connector/ble/ble.go` | 19-29,67-86,98-127 | 1024-byte length-prefixed framing, interfragment timeout and fragmentation |
| `pkg/vehicle/state.go` | 11-19,38-86 | future VCSEC status / GetChargeState subset; not yet bound |

SHA1 of the 32-byte shared x-coordinate, truncated to 16 bytes, is mandated by
`native.go:118-124`. Label subkeys are HMAC-SHA256. HKDF is not used in this
reference protocol and is deliberately not substituted for these operations.

## Independently inferred integration interface

The inventory at
`/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom/scope-inventory.json`
records external call sites and behavioral tests. Legacy names/signatures, if
later provided, must be independently inferred there, not copied from tainted
headers. This checkpoint uses a separate namespace/API with no compatibility
shim. No Phase-1 tests are deleted, weakened or silently replaced.

Historical `TESLA_BLE_PHASE1.md` and review10 were opened solely for status and
explicit limitations. Their implementation descriptions are NOT source material
for this code. The implementation derives protocol details only from the
official sources above. The independent core does not reproduce the legacy
internal state machine. Mechanical baseline comparison must report only hashes,
locations and classifications, never baseline excerpts to the implementer.

## Licensing

New core and its independently authored tests carry SPDX Apache-2.0 identifiers.
Protocol derivations retain Tesla attribution in NOTICE and this provenance.
mbedTLS is an existing Apache-2.0 dependency, not a vendored replacement here.
No generated code, AGPL fragments or license text from the tainted directory is
retained in this clean library. Production, host tests and firmware now bind only
to `TeslaBLEClean`; the former directory and coupled legacy tests are absent.
Independent engineering review approved Phase 2 with explicit limitations. This
does not constitute legal, patent, distribution or release clearance.

## Compatible dependency API documentation (not protocol sources)

Only mbedTLS API declarations/comments were additionally inspected under the
existing Espressif framework builder's `esp-idf/components/mbedtls/mbedtls/include/`:
`mbedtls/sha256.h` (hash API), `sha1.h` (hash API), `pk.h` (PK ownership,
parse/export and EC access), `ecp.h` (point/group/key operations), `ecdh.h`
(shared-point API), `md.h` (HMAC API), `gcm.h` (authenticated encryption API),
and `constant_time.h` (constant-time equality and C linkage). These headers are
Mbed TLS's Apache-2.0-compatible API documentation, not a source of Tesla
protocol logic. The framework/package trees were not edited. mbedTLS 3 API
names and the constant-time header's explicit C linkage were resolved from
these declarations, rather than inferred from the tainted library.

Independent test vectors were generated using installed Python cryptography
(Apache-2.0/BSD-compatible) and hashlib/hmac, from the official algorithm and
schemas. This generator is a test oracle, not another Tesla implementation.

## Continuation 2 — independent identity, lifecycle and wire subsets (still UNBOUND)

Evidence is separate from the immutable first checkpoint:
`/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom-cont2/`.
The official pin was rechecked before implementation. Exact whole-file hashes and
the sections below are in this continuation's `official-source-manifest.json`.

| Official source | Exact sections used | Clean implementation |
| --- | --- | --- |
| `internal/dispatcher/session.go` | processHello, lines 113-129 | recently transmitted UUID requirement; establishment versus existing-peer signed update |
| `internal/dispatcher/dispatcher.go` | SessionInfoRequest, lines 463-477 | public-key request; wire/controller binding still pending |
| `internal/authentication/signer.go` | UpdateSessionInfo/UpdateSignedSessionInfo, 96-129; encryptWithCounter/Encrypt, 132-176 | established-peer HMAC trust, nondecreasing counter, epoch/clock update; personalized AES-GCM, counter rollover refusal |
| `internal/authentication/peer.go` | extractMetadata/RequestID/responseMetadata, 33-117 | outgoing AAD and request identifier, received-response AAD |
| `pkg/protocol/protobuf/universal_message.proto` | Destination, 16-20; RoutableMessage, 80-101 | bounded outgoing routable envelope and routed encrypted-response decoder |
| `pkg/protocol/protobuf/signatures.proto` | AES-GCM data, 41-53; SignatureData, 66-74 | outgoing and received encrypted signature fields |
| `pkg/vehicle/state.go` | StateCategoryCharge, 38-40; GetState, 70-86 | GetChargeState-only read request |
| `pkg/protocol/protobuf/car_server.proto` | Action/VehicleAction, 13-25; GetVehicleData, 88-90; GetChargeState, 117-118; Response/ActionStatus, 145-169 | query and checked action-status/vehicle-data nesting |
| `pkg/protocol/protobuf/vehicle.proto` | VehicleData, 14-27; ChargeState battery fields, 306-323 | battery-level presence and bounded integer parsing; no substitution of usable battery level |
| `pkg/vehicle/vcsec.go` | getVCSECInfo, 109-129; addKeyPayload, 151-168; executeRKEAction, 171-193 | status payload, charging-manager permission subset and explicitly requested wake payload |
| `pkg/vehicle/security.go` | SendAddKeyRequestWithRole, 334-359 | raw PRESENT_KEY pairing envelope (requires physical/user approval; not a SoC authentication mechanism) |
| `pkg/protocol/protobuf/keys.proto` | charging-manager role, line 15 | fixed minimal charging-manager role |
| `pkg/protocol/protobuf/vcsec.proto` | 10-23, 29-45, 47-51, 67-81, 104-108, 121-143, 169-232, 263-267, 279-295 | pairing metadata/envelope, status, explicit wake, pairing OK/WAIT/error and sleep state |

The new protobuf reader/writer is independently authored from ordinary protobuf
wire encodings and the official field numbers above. No legacy generated header,
source or schema was read or copied. `wire_vectors.py` independently uses Python
cryptography and explicitly encoded official schema fields to generate public
outgoing/response/pairing KATs. Existing primitive KATs remain unchanged.

Identity transactions, intrusive checked leases, callback pins, no-exceptions
capture restrictions, mutex and storage ports, transport-response staging,
bounded recovery/backoff and local nonwrapping ownership are independently
authored local machinery, NOT implementations copied from Tesla's Go dispatcher.
External requirements were inferred from allowed tests, especially
`test_tesla_ble_key_transaction.cpp`, `test_tesla_ble_key_reentry.cpp`,
`test_tesla_ble_callback_contracts.cpp`, and the R7/R8/R9 behavioral assertions.
The external WARP storage declarations and `tesla_ble_storage_io.h` were read for
adapter/lifetime constraints only. No tainted include was followed. Historical
Phase-1 documentation was opened for checkpoint status/append context, not used
as an implementation source. No old test/header compatibility shim was added.

Mechanical screening reports one exact long-line match at clean
`identity.hpp:6` (hash/positions/count only in `similarity-audit-final.json`),
and no minimum-length exact or normalized token matches. This is the required
public storage-outcome declaration, independently inferred from external
`test_tesla_ble_key_reentry.cpp:98-101,156-165` and
`test_tesla_ble_strong_transaction.cpp:9-34`. The declaration was not changed to
hide the match. No baseline excerpt was emitted. This explanation is provenance,
NOT legal clearance; the full distributable still retains the old library.

## Continuation 3 — public framed controller, still UNBOUND to WARP

Evidence: `/home/hermes/.hermes/cache/scratch/tesla-phase2-cleanroom-cont3/`.
The official pin was checked before code changes and in the final audit. Exact
file hashes and section ranges are in `official-source-manifest.json`.

- `universal_message.proto:16-20,69-101` and `signatures.proto:55-74,81-94`:
  SessionInfo request/reply envelope, domain/address destinations, outer UUID and
  raw signed body/HMAC wrapper. The nested request challenge is omitted like
  `dispatcher.go:463-488`; it is not confused with GetSessionInfoRequest.
- `dispatcher.go:182-293,386-413`, `session.go:113-135` and inherited clean
  authentication: outer routing/UUID and recently owned challenge admission.
  This controller deliberately requires full UUIDs even for VCSEC; it does not
  claim support for the official dispatcher's omitted-UUID variant.
- `signer.go:96-176` and inherited clean message/response primitives: authenticated
  update semantics, personalized AES-GCM, counter overflow exclusion. Reserving
  exposed high-water counters across refused writes/reconnects, staged RX publication,
  four-command capacity and finite deadlines/retry budgets are independent local policy.
- External WARP call sites and public tests `test_tesla_ble_review7.cpp`,
  `test_tesla_ble_review8.cpp`, `test_tesla_ble_review9.cpp` were inspected only
  for behavioral/interface requirements; their tainted includes were never followed.
  New public controller tests do not reproduce old private state-machine machinery.

`controller_fixture.hpp` uses independently encoded official-schema envelopes,
raw mbedTLS HMAC/GCM/SHA APIs and the existing independent Python ECDH key vector
as a peer oracle. It decrypts actual outgoing requests to assert the status,
GetChargeState and explicit wake bytes. `session-wire-kat.json` holds the separate
Python request-envelope vector. Production controller/SessionInfo wire tests had
saved behavioral assertion REDs before their implementations. Follow-up REDs
cover exposed wake consumption, unique private budgets, reconnect high-water,
refused partial-frame recovery and wake refresh without retransmission.

The old production library, external WARP sources and old tests/runner are unchanged.
No legacy compatibility headers or generated APIs were copied, no tainted source
was read, and no historical AGPL diff/prose was an implementation source. Phase-1
documentation was accessed only for status/append context after implementation.
The mechanical audit is still fingerprint/location/count-only: 50 archived baseline
files versus 30 clean C/C++ files, zero minimum-length token matches and the same
previously explained public storage declaration match. No code was changed to hide it.


## Continuation 4 — independently authored WARP binding

The existing allowed WARP call sites and tests outside the tainted tree supplied
interface/lifetime requirements. The clean callback/mutex/VIN boundary is new
Apache-2.0 code; existing WARP integration/storage I/O code retains its existing
license provenance and is adapted, not falsely relicensed. No tainted header,
implementation, historical diff or implementation prose was used as a source.
The official pin is unchanged. Clean public protocol primitives are reused, not
rederived from the old implementation. Persistence is exercised through the real
POSIX adapter; native mutexes and esp_fill_random are platform ports.

Host sources build clean only; compiler include traces and private empty mounts
support the firewall. Passing execution does not claim all legacy coverage.
`cont4/replacement-coverage.json` records exclusions/open acceptance. Pair status
approval uses clean authenticated VCSEC status plus SLAC/request authority, not
raw pairing acceptance. New-key publication remains persistence-bound, including
explicit pairing after forget. Physical CLOSE is separate from cancelled WRITE.

Opaque comparison additionally found a 127-token identifier-normalized signature
pattern in `tesla_ble_clean_platform.h`, a borrowing type-erased callback utility
independently implemented from ordinary C++ call/forward semantics and the public
external GATT callsite requirement. No baseline text was emitted or used and no
code was altered to hide the match. The existing public storage declaration match
also remains. Both are reported for later authorized review, not legal clearance.

## First independent review — B1/B2 protocol fixes

Only GPT-6.1 Sol implemented and verified these changes; no additional reviewer
or agent was started. The full first Astra report and exact confirmation probes
were read. The probe and diagnostic driver were preserved byte-for-byte under
`software/tests/tesla_vehicle/cleanroom/astra/` before production changes. Eight
individual behavioral REDs (four modes, normal and ASan/UBSan) precede the fixes.
The diagnostic driver's zero exit for expected failures is not a product pass.

The same clean official pin remains the sole protocol authority. Independently
inspected sections: `pkg/protocol/protocol.md:711-731,779-786`,
`internal/dispatcher/receiver.go:29-38`, `dispatcher.go:158-173,182-223`,
`session.go:48-58,113-129`, and `internal/authentication/signer.go:96-129`.
Their hashes and the pin/clean-check result are in the new evidence root's
`official-reference-inspected.json`.

Response replay is scoped to the current UUID/authenticated request hash, not the
domain-session lifetime. Rearming that same scope retains replay rejection.
Recent command-correlated SessionInfo trusts the existing peer HMAC/public point,
not a newly supplied key, and updates epoch/clock/counter transactionally before
a budgeted retry. Lower peer counters never decrement the outgoing high-water.
The five-second recency cap, eight-retired-epoch fail-closed history, original
eight-attempt budget, once-only exposed wake and callback-time recheck are bounded
local policy; they are not copied dispatcher internals or unlimited compatibility.
No removed implementation, historical implementation diff or archived content
was consulted. Only the unchanged mechanical similarity/integrity gates consume
the opaque archive and emit fingerprints/locations/counts, never source excerpts.
