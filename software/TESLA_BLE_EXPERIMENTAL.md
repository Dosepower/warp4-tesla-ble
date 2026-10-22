# Experimental WARP4 Tesla BLE source port

> **Source review only — do not install.**
>
> This branch intentionally does not provide an application image, OTA package,
> merged flash image, installer, release signature, or hardware approval.

## Scope

This port integrates Tesla BLE support into the current WARP4 source tree while
keeping the existing WARP vehicle profiles authoritative. Tesla configuration is
shown under **Vehicles**; Bluetooth remains the technical interface.

The source supports multiple Tesla profiles with separately scoped VINs, BLE
bindings, keys, sessions, and WARP vehicle-profile identities. Discovery,
pairing, and SoC requests require explicit user actions. Bluetooth being enabled
does not automatically scan, connect, pair, poll, or operate a vehicle.

## Charging and SoC separation

The cable-side SLAC/PEV MAC remains the source for WARP vehicle identification
and, when enabled, the existing Autocharge authorization path.

A vehicle recognized as Tesla is excluded from cable-based ISO 15118 SoC
acquisition. This prevents entering the incompatible PLC/V2G SoC flow that can
leave a Tesla unable to begin ordinary AC charging until it is unplugged.
Recognition is performed from maintained Tesla MAC/OUI entries and, for an
initially unknown OUI, from Tesla's application-protocol namespace before a
DIN/ISO2/ISO20 decoder can be selected.

Tesla SoC is published separately through BLE into the common EV model. BLE
success, failure, discovery, selection, and pairing state do not authorize or
start charging and do not gate IEC 61851 AC charging. Compatible non-Tesla
vehicles retain the existing cable-based SoC behavior.

## Verification status

All VINs, MAC addresses, keys, and protocol identities in the published tests are synthetic fixtures and do not identify a real vehicle or device.

The source-review test set covers:

- multi-profile key, session, and identity separation;
- VIN-bound manual discovery and stale-result rejection;
- deterministic concurrent identity publication;
- known-OUI and late protocol-namespace Tesla recognition;
- Autocharge/cable-SoC/BLE-SoC separation;
- supportedAppProtocol decoder/API capacity boundaries;
- empty and undersized V2GTP payload rejection before decoder entry;
- normal and ASan/UBSan host runs;
- frontend TypeScript checks;
- SDK/build disposition and partition-layout guards.

The review target produces only a non-installable ELF/MAP for static inspection.
Static DRAM, IRAM, flash, and compiler stack reports are not runtime heap,
fragmentation, stack-watermark, interrupt-latency, or RF-coexistence evidence.

## Required before installation or release

- qualified SDK sources, patches, configuration, tools, and build derivation;
  a separately source-derived Secure+BLE candidate need not be byte-identical
  to the manufacturer's precompiled SDK, but its differences must be explicit
  and its WARP4 compatibility independently reviewed;
- official signing, secure-boot, partition, OTA, and recovery acceptance;
- runtime heap, largest-block, PSRAM, and task stack measurements;
- WLAN/BLE/charging coexistence measurements on a real WARP4;
- real Tesla validation for discovery, key-card pairing, SoC, reboot
  persistence, Autocharge, ordinary AC charging, and multi-vehicle A→B→A;
- an explicitly reviewed and authorized release process.

No firmware generated from this branch is approved for installation.