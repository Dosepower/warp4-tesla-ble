# Persistent Tesla BLE binding — qualified candidate

This checkpoint contains the reviewed source and tests for WARP4 firmware `2.13.6+6ae4a299`.

## Behaviour
- Search and authenticated pairing establish the persistent vehicle binding.
- Normal manual and charge-aware automatic SoC requests reuse the trusted binding; they no longer require a fresh discovery result on every cycle.
- An existing correctly owned BLE link is reused; otherwise the stored address and address type are used. Failed connections never adopt another advertiser automatically.
- VIN, PLC identity, key authorization, configuration/generation and command ownership checks remain mandatory. Pairing retains its separate fresh-discovery checks.
- Wake remains conditional and bounded. Periodic polling remains charging-only with a five-minute minimum; initial plug-in and resume demands retain their own scheduling.
- Includes the preceding deferred-RX filtering and coherent diagnostic snapshot/ring corrections.

## Evidence and limits
- 65 NimBLE modes passed normally and with ASan/UBSan.
- 14 persistent-binding modes passed normally and with sanitizers; behavioral baseline RED/current GREEN controls recorded.
- Filesystem-backed binding restore, negative identity controls, focused suites and all nine frontend tests passed.
- Fresh offline target build, generated frontend TypeScript check, ELF-to-app conversion, embedded web payload and OTA container verification passed. Framework content inventories were unchanged.
- OTA SHA-256: `97dc24305165359775f07536f34d66d6fe3d63bba7a57b19e9ca44bdad910372`.
- Full local build-input manifest SHA-256: `3b58682e6f7968abab07ca0c57620c709036a7bacd7f9149b6b84e82d4336e20`.

At checkpoint creation device installation is still in progress. Boot validation and real-vehicle reliability are not yet claimed. A successful synthetic regression does not establish the historical field-failure cause.

This publication includes reviewed product source, tests and public package descriptors. Private device logs, identifiers, keys, build artifacts and local installer identity/runtime locks are excluded. The full local build manifest is a qualification reference, not a claim that private/local installer material is published or usable as a general provisioning release. No unsigned firmware binary is published here.
