# WARP4 Tesla BLE SoC extension — engineering handoff

Current Phase-2 status supersedes the historical Phase-1 handoff below.
The AGPL implementation has been removed and replaced by the independently
authored Apache-2.0 `TeslaBLEClean` subset. The second independent GPT-6 Astra
review concluded `APPROVE PHASE 2 WITH EXPLICIT LIMITATIONS`. See
`TESLA_BLE_PHASE2.md` for the reviewed scope, evidence and limitations. This is
software review approval only: Tinkerforge hardware/interoperability validation
and any upstream/release decision remain outstanding.

## Status

- Base revision: `a114447c4582456d123a0832eaa2d3ce5dd29f88`
- Build environment: `warp4_tesla_ble`
- Host integration tests: 18 preserved helpers, 43 Clean native entries and 19
  WARP module modes passed normally and with ASan/UBSan
- Focused R7/R8/R9 and replacement matrices: 22 executions passed
- Production Web/TypeScript pipeline: passed
- Clean PlatformIO target build: passed; all 13 Clean production units rebuilt
- Independent review: `APPROVE PHASE 2 WITH EXPLICIT LIMITATIONS`
- Final build accounting: 92,244 / 327,680 bytes RAM (28.2%); 4,773,029 /
  6,488,064 bytes flash (73.6%)
- Hardware/vehicle validation: **not yet performed**
- Firmware has **not** been uploaded to a WARP.

## Safety model

The WARP SLAC vehicle MAC is the authoritative vehicle identity. Pairing is allowed only while the configured WARP EV profile is active. The pairing key is persisted together with the six-byte SLAC MAC observed during pairing. Automatic and manual SoC requests are rejected unless the currently connected vehicle has the same SLAC MAC. Disconnects, vehicle/session changes and BLE disconnects invalidate in-flight results.

BLE connection uses the separately configured Tesla BLE address; it is not used to decide which plugged-in vehicle receives the SoC.

The extension does not send a Tesla BLE charge-start command. WARP's normal
Autocharge/IEC 61851 path remains responsible for AC charging. Tinkerforge's
forum reports distinguish SLAC MAC identification from the simulated DC
communication used by WARP's built-in ISO 15118 SoC reader: Autocharge works
with Tesla, while the problematic AC-start state occurs after that simulated
DC SoC procedure. References:

- https://www.tinkerunity.org/topic/13899-sammelthread-autos-die-nach-dem-soc-auslesen-keine-ac-ladung-starten/?do=findComment&comment=63913
- https://www.tinkerunity.org/topic/13809-warp4-und-wem2-pv-laden-startet-nicht-automatisch-bei-tesla/?do=findComment&comment=62824
- https://www.tinkerunity.org/topic/13809-warp4-und-wem2-pv-laden-startet-nicht-automatisch-bei-tesla/?do=findComment&comment=62759
- https://www.tinkerunity.org/topic/13693-jetzt-noch-eine-warp-3-wb-kaufen-oder-warten/?do=findComment&comment=62440

## Configuration API

The firmware includes a dedicated **Tesla Bluetooth** page in the WARP menu with configuration, status, pairing instructions and test actions.

Persistent config endpoint: `tesla_vehicle/config`

```json
{
  "enable": true,
  "vin": "17_CHARACTER_VIN",
  "ble_address": "AA:BB:CC:DD:EE:FF",
  "ev_index": 0,
  "poll_interval": 300,
  "wake_once": false
}
```

- `ev_index` is the index of the WARP vehicle profile whose SLAC MAC identifies the Tesla.
- `wake_once=false` is the conservative default: polling does not wake a sleeping Tesla.
- First enable after a disabled boot starts from the main loop without reboot.
- Disable or changing a live configuration stops the dynamic Tesla runtime. A
  successful stop frees those dynamic resources, but the build's BLE-only
  `btMemRelease` call releases only 5,172 bytes of controller ROM reserve. It
  does not release 33,240 bytes of other controller ROM reservations or the
  15,116 bytes of named Bluetooth data/BSS ranges. Disabled boot therefore
  means no dynamic Tesla runtime, not zero Bluetooth DRAM or full static
  release. Re-enabling after controller use requires reboot;
  `restart_required` exposes this latch, and `stopping` exposes pending teardown.
- Phase-1 ownership, limitations, measurement hooks and RED/GREEN evidence are
  documented in `TESLA_BLE_PHASE1.md`.

State endpoint: `tesla_vehicle/state`

Commands (POST an empty JSON object):

- `tesla_vehicle/pair`
- `tesla_vehicle/poll`
- `tesla_vehicle/forget_key`

Pair only while the Tesla is physically plugged in and the expected WARP vehicle profile is active. Follow the Tesla key-card confirmation procedure. A stored key is not reported as successfully paired merely because a key file exists; authenticated vehicle communication is required.

## Build

The stock Tinkerforge ESP32 libraries have Bluetooth disabled. Rebuild the framework libraries with `software/lib-builder/defconfig.esp32ethernetbrick`, then point the environment at the result:

```bash
export TESLA_BLE_FRAMEWORK_LIBS=/absolute/path/to/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs
cd software
uv run pio run -e warp4_tesla_ble
```

Host tests:

```bash
software/tests/tesla_vehicle/run_host_tests.sh
```

## Tinkerforge hardware validation checklist

This project deliberately stops before hardware execution. The following checks
are requested from Tinkerforge on its own WARP4/Tesla bench; they are not an
instruction to install the firmware on an end-user wallbox.

1. Back up the WARP configuration and retain the currently installed official firmware for rollback.
2. Install only during a controlled test window; do not start charging automatically.
3. Confirm Ethernet/Wi-Fi, meter, EVSE state, Home Assistant MQTT identity and evcc connectivity after reboot.
4. Create/select the Tesla EV profile using the MAC learned by WARP through SLAC.
5. Apply Tesla BLE config, reboot, plug in that Tesla, verify the active EV profile, then start pairing.
6. Confirm the key-card action in the Tesla and poll once manually.
7. Verify the WARP SoC changes only for the bound SLAC MAC.
8. Disconnect/reconnect, connect a different vehicle if available, and verify stale or foreign SoC is rejected.
9. Test sleeping-car behavior with `wake_once=false`; only later test one bounded wake attempt if explicitly desired.
10. Start an ordinary AC charge and verify it begins without the ISO 15118 simulated-DC SoC procedure.
11. Observe reboot stability, BLE/Wi-Fi coexistence, heap and EVSE responsiveness.

## Licensing and provenance

The earlier AGPL-3.0-only TeslaBLE dependency and its coupled legacy tests have
been removed. Production, host tests and firmware bind only to
`software/lib/TeslaBLEClean/`, an Apache-2.0 subset independently implemented
from Tesla's official `vehicle-command` repository at commit
`a4b43c1eff0e09d77deb9f2dce97031141fe8c8a`. Exact source sections, attribution
and the clean-room boundary are documented in `TeslaBLEClean/PROVENANCE.md` and
`NOTICE`. Mechanical source screening and independent review are engineering
evidence, not legal, patent or distribution advice; Tinkerforge must perform its
own licensing and release assessment, including the Espressif Bluetooth stack.

## Response-deadline semantics

The reviewed controller records when a complete response frame reaches the
Clean controller. A frame completed exactly at or after the response deadline
is rejected. A complete encrypted response that arrived before the deadline may
be published later after an already-started transport callback returns, provided
the same request still owns the connection and its transmit and total budgets
remain valid. Command-UUID `SessionInfo` recovery additionally rechecks current
callback time. This distinction was independently reviewed; stale data did not
complete successor work.
