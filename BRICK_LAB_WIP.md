# Brick laboratory checkpoint — WIP, not a wallbox release

This development branch preserves the current minimal WARP 2.13.6 Tesla work separately from `main`. `main` is intentionally unchanged and must not be interpreted as this candidate's qualification status.

## Two targets, different safety scope

- **Wallbox target:** `warp4_tesla`. Physical vehicle/session/profile checks remain required. This branch does not authorize installation on a secured production charger.
- **Brick laboratory target:** `warp4_tesla_stack_probe`. Full WARP application with bounded heap/arena diagnostics and lifecycle measurements, intended only for a separately recoverable ESP32 Ethernet Brick without EVSE.
- **Brick-only pairing fixture:** `warp4_tesla_brick`, explicitly compiled with `TESLA_BRICK_SESSION_TEST=1`. The build guard rejects every definition of this macro outside that target, and rejects missing/noncanonical/duplicate definitions inside it. An explicit volatile `PUT tesla_vehicle/brick_session` selects an existing exact EV/Tesla profile without a cable. Runtime checks require `config_type=brick`, no EVSE feature, secure boot and flash encryption off, and autocharge off. Synthetic identity is never persisted or enabled at boot. VIN/BLE/key binding and authenticated pairing are unchanged. Automatic polls/wakes are disabled on this target; manual outcome publication remains. `warp4_tesla` retains the real physical-profile gate.
- **Installed laboratory fixture:** `2.13.6+6ae8e0d7`; brick identity/profile activation, connected MQTT and active Bluetooth read back. Boot validation and actual authenticated vehicle pairing/SoC remain separate acceptance gates. This is not a production release.

No vehicle keys, MQTT credentials, VINs, device backups, coredumps or compiled firmware are published here. This is a sanitized source checkpoint, not a ready-to-flash release.

## Latest frozen laboratory build

`2.13.6+6ae8bf53` (artifacts retained privately):

- Tesla dispatcher-owned TX copy reduced 520 -> 20 bytes; RX and protocol frame limits unchanged. Linked dispatcher object 896 -> 396 bytes.
- Three unchanged Tesla UUIDs constant-initialized in mapped flash: 51 bytes payload moved; BSS section 56 bytes smaller including alignment.
- MQTT connected and Bluetooth active on the separate brick; three real VIN-scoped scans completed with one target each.
- Host NimBLE, dispatcher quarantine, modeled authenticated pairing/profile isolation and SoC/wake policy tests passed. These do not constitute authenticated physical pairing or a real SoC result for this build.
- Physical pairing, real authenticated SoC, simultaneous worker peak-memory reserve and production-wallbox acceptance remain unqualified.

## Changes beyond the Tesla module

This is NOT an untouched Tinkerforge tree. Source includes the previously investigated Config-schema PSRAM preference, EV identity integration, MQTT start diagnostics and bounded heap/arena instrumentation. The laboratory SDK additionally carries documented NimBLE/PSRAM/external-worker-stack, Mesh-off and measured host-stack configuration changes. Global allocation threshold remains 2048. This source checkpoint does not include an SDK binary distribution or substitute credentials.

The older stored crash was tied to build `6ae79559` and failed mutex creation during a ChargeManager file open. The later network outage on `6ae7b2d8` is NOT proven to share that cause.

## Build prerequisites and limitations

Upstream baseline: Tinkerforge `afa5c782`, WARP 2.13.6. Source snapshot Git base: `dbbd2fa333629ed1675c922f10a3582fbb6caab6` plus reviewed work-in-progress source changes. The branch history remains based on this repository's public `main`, not a push of private development history.

Provide the separately retained, matching SDK through the local `software/brick_sdk/selected` directory/symlink before building. It is deliberately not checked in. Read `software/warp4_tesla.ini` and `software/warp4_tesla_stack_probe.ini`. This publication export itself has not yet been clean-room rebuilt and must not be advertised as a reproducible binary release.

Never copy production vehicle keys onto the brick. Use a separate laboratory key. Do not change bootloader, partition table, encryption, secure boot or eFuses on a production wallbox to reproduce laboratory setup.
