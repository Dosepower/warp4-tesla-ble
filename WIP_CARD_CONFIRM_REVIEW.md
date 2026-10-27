# WIP: card-confirm pairing correction

**SOURCE REVIEW ONLY — DO NOT INSTALL.**

This branch hands off the existing Opus source implementation for user-led independent qualification. It is not a firmware release, installation approval, or a claim that physical Tesla pairing works.

## Scope

The card-confirm change makes worker-side binding checks wait for the outcome mutex rather than interpret ordinary Main-task mutex contention as revoked authority. Main-task checks retain their nonblocking behavior. Identity, revision, freshness, cancellation, authentication, persistence, and charging restrictions must remain intact.

This branch also includes previously local pairing first-cause diagnostics and their tests, needed to reproduce the source checkpoint on which the card-confirm correction was made. Its base is `wip/tesla-open-drain-opus-20261027` at `26998097ada3cb2cf38dc93862413ec134499367`.

## Status and evidence limits

- Opus reported focused baseline RED and candidate GREEN host executions, including normal and ASan/UBSan card-confirm, retry, contention, identity-change and authority-expiry cases.
- Those implementation results are not a completed independent acceptance verdict.
- Independent qualification was interrupted. Full qualification, final firmware/web build, resource analysis, artifact verification and physical vehicle acceptance remain incomplete for this candidate.
- The reproduced software mechanism does not establish the historical vehicle failure's unique cause.
- No candidate binary, device data, private logs, credentials, local SDK, or machine-bound installer locks are included in this handoff.
- No upload, reboot, key deletion, pairing, or charging action is performed by publishing this branch.

## Review entry points

From `software/`, with the project's compatible dependencies and pinned SDK configured as required by the test scripts:

```sh
python3 tests/tesla_vehicle/test_card_confirm.py
python3 tests/tesla_vehicle/test_card_confirm.py sanitize
python3 tests/tesla_vehicle/test_pair_trace.py
python3 tests/tesla_vehicle/test_pair_trace.py sanitize
```

Inspect the scripts' environment requirements before execution. This handoff does not bundle the local SDK or claim a standalone reproducible build environment.

Continue with independent adversarial locking/lifecycle review, complete relevant host/frontend regressions, the real WARP4 NimBLE firmware/web build, ELF/MAP/stack checks and embedded-web/app/OTA consistency checks. Preserve the known-good firmware and verify target, supported OTA path, recovery and hardware safety separately before any installation.

## Important files

- `software/src/modules/tesla_vehicle/tesla_ble_command_outcome.h`
- `software/src/modules/tesla_vehicle/tesla_profiles.inc`
- `software/src/modules/tesla_vehicle/tesla_vehicle.{h,cpp}`
- `software/src/modules/tesla_vehicle/tesla_nimble_gatt.inc`
- `software/src/modules/tesla_vehicle/tesla_ble_gatt.cpp`
- `software/tests/tesla_vehicle/host/card_confirm_contracts.inc`
- `software/tests/tesla_vehicle/test_card_confirm.py`
- `software/tests/tesla_vehicle/host/pair_trace_contracts.inc`

No stable release or binary publication is implied.
