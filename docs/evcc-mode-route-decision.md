# evcc mode-route compatibility: evidence and decision

This is an OFFLINE SOURCE finding, not a claim about the user's uninspected installed evcc version/configuration. No LAN/device access or evcc modification was performed. Overall remote work is not blocked by the orphan-Tag reset decision; no destructive gesture was implemented.

## Concrete mismatch

Pinned public evcc source revision: `e2240501caa2c3aae1ce021528cde75997427ca1`. Exact raw source copies and SHA-256 values are retained in `docs/evidence/evcc-compatibility/`; run `tools/audit_evcc_mode_route.py` for reproducible source anchors and digest checks. This audit does not pretend to run evcc: no Go toolchain is present and no dependency/environment install was performed.

- The `tinkerforge-warp-ws` template renders `type: warp-ws`, including WARP4 products. `charger/warp-ws.go`'s `setCurrent` posts `evse/external_current`. Its `handleEvent` receives EV/meter/EVSE/external-current/user-current/power-manager events, not `evse/charge_mode` or native CM mode requests.
- Legacy `charger/warp2-mqtt.go` writes `evse/external_current_update` and subscribes to EVSE/meter/charge-tracker/power-manager state. It also does not consume native requested-charge-mode or supply supported-mode/echo callbacks.
- Candidate installed-source WARP `EvseCommon::register_urls` handles `evse/charge_mode_update` by starting a 10-second request deadline. It is a REQUESTED Config, not proof of controller application.
- WARP `send_cm_client_update` places that requested mode in the CM client update. `cm_protocol.cpp` serializes `state_pkt.v4.requested_charge_mode` and sends its native CM state packet. Its inverse native manager callback is the source of `ManagerModeObservation` and supported modes.
- Therefore the inspected stock evcc drivers do NOT connect WARP's native CM request/echo mechanism to evcc's authoritative loadpoint mode. Merely calling `evse/charge_mode_update` cannot be claimed to change evcc mode; waiting for a nonexistent CM echo would only time out. Writing external_current directly from the remote would compete with evcc and is prohibited.

Official references:
- WARP evcc guide: https://docs.warp-charger.com/en/docs/smart_home/evcc/
- Pinned WebSocket driver: https://github.com/evcc-io/evcc/blob/e2240501caa2c3aae1ce021528cde75997427ca1/charger/warp-ws.go
- Pinned legacy driver: https://github.com/evcc-io/evcc/blob/e2240501caa2c3aae1ce021528cde75997427ca1/charger/warp2-mqtt.go
- evcc REST API: https://docs.evcc.io/en/docs/integrations/rest-api (documents `/loadpoints/{id}/mode/{mode}` and `/state`; successful POST alone is NOT applied evidence).

## Precise parent decision required for the charging branch

Which ONE route is authorized?

A. Extend the existing evcc WARP driver/controller side to consume WARP's native requested-mode protocol and return actual supported modes plus a fresh post-request committed revision/echo. evcc stays the only charging controller; this requires an evcc-side code/configuration change outside the current two candidate trees, which is NOT implicitly authorized.

B. Add a bounded native WARP client for the EXISTING evcc loadpoint mode API (no intermediary service and no current-slot writes), with explicit controller/loadpoint identity and a trustworthy fresh post-request applied-revision source. This changes the integration contract: existing CM callback revisions cannot be reused as evcc REST commit proof, and POST acceptance or equality with an old `/state` mode must not produce green success. The installed evcc version/driver and the available commit evidence would need offline confirmation from parent before qualification.

If parent has a custom installed evcc native-CM driver already supporting request/echo, provide its exact version/source offline instead. This finding does not assert that such a private extension is absent.

Until that decision, charge opcodes remain prohibited, no competing writer is introduced, and submit honestly fails unavailable. Ordinary read-only status, enrollment/recovery and fault qualification remain actionable. Orphan-Tag reset is a SEPARATE still-pending user decision.

## Telemetry maximum boundary

Existing native APIs provide measured charger input power with its original meter complete-update timestamp and live EV identity/accepted SoC timestamp. A manager's aggregate `maximum_available_current`, actual instantaneous charging phases, requested current or a nominal 230V multiplication is NOT independently verified installed wallbox maximum power. No default 11kW rating is manufactured. Until a per-charger configured/verified rating-and-phase source is supplied/qualified, combined shake SoC/power blink status stays unavailable. Fresh SoC and measured watts still travel authentically as independently valid fields.

## Progress in the same continuation

Full actual-peer harness now additionally drops every diagnostic result fragment for an entire first session, lets it expire, and performs a second real GATT exchange in the SAME production objects. Stable operation ID1 remains; WARP journal stays at three total writes (no reexecution), Tag final commits once, counters advance to Tag3/WARP2, host drains and charge calls remain zero. This is a harmless durable diagnostic, NOT a real charging command.

Actual car-status snapshot now validates the live EV against the existing configured profile before gathering fields and rechecks identity/generation afterward. Full-GATT RED/GREEN cases reject wrong profile and an identity switch during sampling, invalidating SoC AND measured-power attribution rather than sending an old vehicle's status under a new identity.
