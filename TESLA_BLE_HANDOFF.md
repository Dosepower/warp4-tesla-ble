# WARP4 Tesla BLE SoC extension — engineering handoff

## Status

- Base revision: `a114447c4582456d123a0832eaa2d3ce5dd29f88`
- Build environment: `warp4_tesla_ble`
- Host integration tests: passed
- TeslaBLE upstream tests: 249/249 passed
- Clean PlatformIO target build: passed
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
- A changed configuration sets `restart_required`; reboot before pairing.

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

## Hardware validation checklist

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

## Licensing

The vendored TeslaBLE source is pinned to upstream revision
`7486ebfe4673c40b28264a75cee2fb98af444eec`. Its upstream package metadata
says MIT, while the included `LICENSE` file says AGPL-3.0. This project takes
the conservative approach and labels the vendored library AGPL-3.0-only unless
the upstream author clarifies otherwise. The complete vendored source, license,
provenance and local integration changes are included in this repository.
