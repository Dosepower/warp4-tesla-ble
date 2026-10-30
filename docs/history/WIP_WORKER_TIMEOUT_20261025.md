# SOURCE REVIEW ONLY — DO NOT INSTALL

This checkpoint corrects a reproduced software failure mode after a complete 88-byte PairRequestSent, not a proven unique historical vehicle fault. No device, LAN, BLE, reboot, OTA or real-key cleanup was performed. No firmware binaries are published.

## Demonstrated cause

The application had an unchanged 10-second per-operation watchdog while the pinned SDK's GATT unresponsive timeout is 30 seconds. A returned WRITE submission with no peer ACK on a subsequent authentication-session request could therefore quarantine the entire Bluetooth runtime, even after complete enrollment-request transport. ACKed transport is not parsed enrollment or authenticated approval.

A production worker/main/queue/clock/crypto/native-adapter test reproduces this failure. The control with complete ATT ACKs but no authenticated approval traverses the full 60-second approval window without a lifecycle latch. A 15-second initial pair-command budget does not by itself demonstrate a 10-second stuck submission. Captures lack the historical operation kind, exact operation start/finish and card-action timeline, so the reproduced software path cannot uniquely identify what happened at the vehicle. No panic or RAM-pressure conclusion is claimed.

## Narrow correction

For a returned NimBLE WRITE submission, start physical peer-link retirement after 7 seconds without completion, reserving the remaining 3 seconds of the original 10-second watchdog for SDK procedure and terminal application-callback drainage. This is an earlier peer-unresponsive policy, not a larger watchdog. Terminal DISCONNECT and failed-CONNECT dispatch keep admission/deinitialization closed until the application callback returns. Only proven drained ownership permits ordinary bounded failure and later explicit retry. A blocked submission, missing terminal event or unresolved callback still quarantines and retains ownership. No elapsed-time assumption frees callback state.

The 10-second operation and 30-second aggregate worker watches, pairing/authentication checks, VIN and generation scopes, key retention, SDK/security/partition/PSRAM/stack/role policy and charging separation remain intact. Added first-cause state fields are nonsecret diagnostics only: timeout_cause, timeout_operation, timeout_elapsed_ms.

## UI

Identical rendered grey Pair/Poll or Search/Poll prerequisite explanations appear once per component; distinct diagnostics remain. Existing red-error handling, authenticated-only primary Entkoppeln, secondary local-key cleanup in Details and unknown-state fail-closed policy remain unchanged. Neither HTTP nor ATT success creates paired/SoC success.

## Local software evidence

Cold complete WARP4 firmware/web build succeeded against the retained, unchanged source-qualified SDK. The 61-wrapper suite (previous 60 plus the native SDK worker-timeout wrapper), full normal/ASan/UBSan host regressions, native schema and mounted authored/generated browser checks passed. Embedded ELF HTML equals standalone HTML and app/OTA compressed bytes; normal web rebuild repeats identical bytes. Test framework boundaries are explicitly OS/radio/controller/procedure-list storage; four pinned SDK GATT teardown function bodies execute in the actual module test. This is not ESP32 radio emulation.

New full-worker scenarios: no approval across 60 seconds, missing follow-up ACK, disconnect during WRITE, delayed ACKs, absent terminal callback, blocked submission, late ACK without terminal/main sampling, authenticated approval after 50 seconds, authenticated denial after 50 seconds. Existing reconnect/stale-callback/profile/service-lifetime/authenticated-response/SoC and no-auto-radio/charging tripwires remain.

This source backup is not a firmware release or hardware acceptance. Physical pairing, SoC, RF/WLAN coexistence, runtime heap/stack margins and actual vehicle attribution remain unverified. Any recovery, installation or vehicle action needs a separate explicit authorization for the exact candidate and device.
