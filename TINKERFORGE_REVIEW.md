# Tinkerforge review request — WARP4 Tesla BLE SoC

## Scope

This branch replaces the earlier AGPL-based prototype with a minimal,
independently authored `TeslaBLEClean` implementation derived only from Tesla's
official Apache-2.0 `vehicle-command` reference at commit
`a4b43c1eff0e09d77deb9f2dce97031141fe8c8a`.

The extension is WARP4-only. It reads Tesla SoC over BLE, but does not send a
Tesla BLE charge-start command. WARP's existing SLAC/IEC-61851 path remains the
charging authority. The vehicle identity gate is the SLAC MAC learned by WARP;
the configured Tesla BLE address is transport-only.

## Software evidence

The completed software gates include:

- complete host suite in normal and ASan/UBSan configurations;
- 18 preserved WARP helper entries, 43 Clean native entries and 19 module modes
  in each complete suite;
- 22 focused R7/R8/R9, lifecycle, storage, allocation and replacement runs;
- production Web/TypeScript pipeline;
- clean offline `warp4_tesla_ble` firmware build with all 13 Clean production
  units freshly compiled;
- Clean-only compile database, dependencies, ELF/map and symbol inspection;
- source/license/similarity scans, diff checks and source/artifact reconciliation;
- two independent GPT-6 Astra reviews. The first found two protocol-liveness
  defects; both were fixed with preserved RED→GREEN evidence. The second verdict
  was `APPROVE PHASE 2 WITH EXPLICIT LIMITATIONS`.

Final reviewed build accounting:

- RAM: 92,244 / 327,680 bytes (28.2%)
- Flash: 4,773,029 / 6,488,064 bytes (73.6%)

The old `software/lib/TeslaBLE/` tree and its 48 coupled legacy tests are absent.
Production, host tests and firmware bind only to `TeslaBLEClean`.

## Requested Tinkerforge bench validation

No end-user wallbox has been flashed. Please validate on Tinkerforge-owned
WARP4/Tesla hardware:

1. Build the `warp4_tesla_ble` environment with the supplied Bluetooth-enabled
   framework and confirm the framework/package revisions are acceptable.
2. Flash only a WARP4 bench unit and verify boot, recovery, networking, web UI,
   meter, EVSE and ordinary charging functions before enabling Tesla BLE.
3. Measure free/minimum heap, task stack high-water marks and watchdog stability
   during pairing, fragmented RX/TX, disconnect/reconnect, storage failures,
   runtime disable/enable and repeated SoC polling.
4. Check BLE/WLAN coexistence and practical range.
5. Pair with the Tesla key-card workflow and verify key persistence over reboot.
6. Verify that pairing and SoC publication are refused without the configured
   active EV profile and matching SLAC MAC; BLE MAC alone must never authorize
   publication.
7. Verify awake and sleeping-vehicle behavior. Routine polling defaults to
   `NoWakeSkip`; an explicitly authorized wake is bounded to one per SLAC session.
8. Verify that a BLE SoC read is followed by an ordinary IEC-61851 AC start,
   without the simulated DIN-70121/ISO-15118 DC SoC session and without a Tesla
   BLE charge-start command.
9. Exercise runtime enable, disable, teardown, reboot-required reporting and
   rollback to official firmware.
10. Decide whether Espressif Bluetooth components and the Clean Apache-2.0 subset
    satisfy Tinkerforge's official and OEM distribution requirements.

## Explicit limitations

Software review does not establish real vehicle interoperability, RF behavior,
physical pairing, sleep/wake behavior, target heap, power-loss storage behavior,
real stack high-water marks or deployment safety. Static reachable-stack figures
are incomplete rather than upper bounds. Source screening is not legal, patent or
release advice.

Detailed architecture and build instructions are in `TESLA_BLE_HANDOFF.md`.
Chronological implementation, tests, review findings and limitations are in
`TESLA_BLE_PHASE1.md` and `TESLA_BLE_PHASE2.md`. Recovery boundaries are in
`HARDWARE_COMPATIBILITY_AND_RECOVERY.md`.
