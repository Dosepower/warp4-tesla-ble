# Tesla BLE hardware acceptance checklist

This checklist covers the remaining real-vehicle validation for the experimental
multi-vehicle Tesla BLE integration. It intentionally contains no VIN, BLE
address, key material, network address, or device-specific identifier.

## Preconditions

- Use isolated, recoverable test hardware.
- Confirm the expected firmware version is running and the web UI is reachable.
- Keep Bluetooth enabled, but do not start discovery until explicitly requested.
- Create one WARP vehicle profile per Tesla and enter the matching VIN.
- Have the Tesla key card available for first-time pairing.

## 1. Passive boot safety

1. Reboot the charger.
2. Open the Bluetooth and Vehicles pages without pressing an action button.
3. Verify that no Tesla discovery, connection, pairing, SoC request, or other
   vehicle action starts automatically.
4. Verify that existing LAN/WLAN, web, API, MQTT, and charging functions remain
   available.

**Accept when:** boot is stable, the configured profiles remain present, and all
Tesla actions stay idle until explicitly requested.

## 2. VIN-bound manual discovery

1. Select the intended WARP vehicle profile.
2. Press **Tesla suchen** once.
3. Verify that only a discovery result whose advertised Tesla name matches the
   name derived from the selected profile's VIN can be selected.
4. Cancel once and verify that cancellation starts no follow-up action.
5. Search again, select the matching result, and confirm the selection.
6. Change the selected profile during a separate confirmation attempt and verify
   that the pending action is rejected.

**Accept when:** address, address type, and discovery revision are bound together;
wrong, ambiguous, stale, or manually substituted results cannot proceed.

## 3. First pairing

1. Connect the selected Tesla to the charger so that SLAC associates it with the
   intended WARP vehicle profile.
2. Start pairing explicitly in the Vehicles page.
3. Follow the key-card confirmation shown by the vehicle.
4. Verify that pairing succeeds only for the active SLAC-associated profile.
5. Verify that a durable key/session exists only for that profile.

**Accept when:** the selected vehicle is paired once, no other profile receives
its key or session, and no pairing starts from discovery alone.

## 4. SoC publication

1. Request SoC explicitly for the paired profile.
2. Verify that the result appears in the existing central vehicle state rather
   than in a second Tesla-only SoC field.
3. Verify that the source is Tesla and that another profile is not updated.
4. Repeat after a short disconnect/reconnect cycle.

**Accept when:** the correct profile receives the SoC through the shared WARP
vehicle model and no duplicate display or cross-profile update occurs.

## 5. Persistence after reboot

1. Reboot the charger.
2. Verify that the vehicle profile, VIN binding, key, and resumable session remain
   associated with the same profile.
3. Request SoC again without repeating key-card pairing.

**Accept when:** the already paired vehicle works after reboot without re-pairing
and no automatic request occurs during boot.

## 6. Multi-vehicle A/B/A sequence

1. Pair vehicle A with profile A and verify an explicit SoC request.
2. Disconnect A, connect vehicle B, pair it with profile B, and verify its SoC.
3. Disconnect B, reconnect A, and request A's SoC without pairing A again.
4. Verify that A and B retain separate VIN bindings, BLE addresses, keys,
   sessions, and SoC ownership.
5. Delete B's Tesla key under the confirmed advanced action.
6. Verify that A remains usable and that B requires pairing again.

**Accept when:** A -> B -> A works without cross-profile state and without
re-pairing A; deleting B affects only B.

## 7. Regression and failure handling

- Discovery timeout or cancellation leaves no connection, key, or session.
- A stale discovery result cannot be paired.
- A mismatched active SLAC profile blocks pairing and SoC requests.
- Loss of BLE connectivity reports a bounded error and does not trigger an
  automatic retry loop.
- A normal charger reboot remains possible from the web UI.

## Evidence to record privately

Keep timestamps, firmware version, pass/fail results, and sanitized error text.
Do not commit VINs, BLE/MAC addresses, key material, NVS/flash images, UART logs,
LAN addresses, credentials, or device-bound installer artifacts.

## Release gate

Do not mark the integration production-ready until sections 1 through 5 pass on
real hardware. Multi-vehicle support should remain experimental until section 6
has been exercised with two real vehicles. Any release notes must distinguish
boot/API verification from real pairing and SoC verification.
