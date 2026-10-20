# TeslaBLEClean source provenance

This port imports the clean core from Git checkpoint
`9b23f3e8b66efba4ae347764f3da877bbba83aba`. It does not import the
obsolete TeslaBLE implementation or device/evidence archives.

Protocol authority: Tesla's official Apache-2.0 vehicle-command repository,
https://github.com/teslamotors/vehicle-command at
`a4b43c1eff0e09d77deb9f2dce97031141fe8c8a`.

The checkpoint records the following protocol derivations:

- `internal/authentication/{metadata,native,crypto,ecdh,window,peer,signer}.go`:
  ordered metadata TLV, P-256 ECDH, SHA-1 shared-x truncation, HMAC-SHA256,
  AES-GCM, session authentication, counters and replay protection.
- `pkg/protocol/protobuf/{signatures,universal_message}.proto`:
  routing, UUID correlation, signed session information and encrypted envelopes.
- `pkg/connector/ble/ble.go`: length-prefixed framing and fragmentation.
- `pkg/vehicle/{state,vcsec,security}.go` and
  `pkg/protocol/protobuf/{car_server,vehicle,keys,vcsec}.proto`:
  charge-state requests, charging-manager key role, explicit key-card approval,
  authenticated status, and explicit wake requests.
- `internal/dispatcher/{dispatcher,session,receiver}.go` and
  `pkg/protocol/protocol.md`: request-scoped response replay and authenticated
  command-correlated SessionInfo updates.

Mbed TLS is a dependency, not vendored protocol implementation. Host test vectors
use independently encoded public schemas, Python cryptography, hashlib/hmac and
Mbed TLS primitives. The clean core and tests retain Apache-2.0 SPDX identifiers;
existing WARP adapters retain their original license provenance. See LICENSE and
NOTICE. Attribution/provenance is not a claim of legal or installation clearance.

The upstream port must independently verify compatibility, resource use, security
layout and source-bound storage semantics. Historical checkpoint test/build or
hardware claims are not evidence for this port and are intentionally not carried
into this public source note.
