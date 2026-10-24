# SOURCE REVIEW ONLY — DO NOT INSTALL

Offline WIP: pairing transmission and lifecycle corrections. No firmware binaries are published by this checkpoint. It is not an official Tesla/Tinkerforge release and does not establish real-vehicle pairing success.

## Changes

- Give the explicit Pair command a bounded 10-second fragment-transmission budget instead of the generic 500-ms default. Keep the existing strict generation, identity and authenticated-approval checks.
- Reconcile displayed connection state with the current logical link, including local retirement of an incomplete framed stream.
- End the SDK connect attempt before the worker's GATT watchdog, allowing the normal terminal callback to arrive without quarantining an absent vehicle as a stuck worker.
- Cancel/terminate and drain outstanding connection ownership while the NimBLE host still processes events, then stop/join/deinitialize. A late successful connect is terminated. Actual missing callbacks or stop failures retain ownership and remain fail-closed; no unsafe object reclamation.
- Expose only nonsecret sampled Pair transport counters and completion code. A complete ATT write is not vehicle approval. Zero acknowledged bytes is not proof that no transmission occurred. These counters never authorize charging or mark a key paired.

No changes to key role/form factor, cryptography, charging permissions, SLAC/profile/VIN/address/type/generation binding, existing guided API revision or confirmed-action discovery refresh. No scan/connect/pair/poll on save, adoption, mount, boot or plug-in. Retained keys are not deleted or recreated for retry.

## Offline regression coverage

The native production-module harness exercises delayed ACKs, partial send, missing approval, untrusted decline, link drop, explicit retry, stale callbacks, profile switch, duplicate Pair, connect timeout, pending cancellation, late connect, benign disconnect race and retained ownership on missing terminal callbacks. The SDK stop harness compiles exact functions from the pinned source with explicitly controlled HCI/NPL boundaries; this is not radio emulation.

Real vehicle success, card-reader behavior, radio/WLAN coexistence and live resource measurements still require separately authorized installation and physical tests. Retained snapshots alone cannot uniquely attribute the previous vehicle failure to a particular callback or transmit duration. Do not treat local tests or a firmware link as that missing evidence.

Official protocol reference: teslamotors/vehicle-command at a4b43c1eff0e09d77deb9f2dce97031141fe8c8a. Its BLE enrollment example uses cloud_key with NFC approval; role and form-factor metadata are distinct. No speculative form-factor substitution is included.
