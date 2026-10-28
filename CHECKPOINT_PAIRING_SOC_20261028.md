# Checkpoint: Tesla pairing and first Bluetooth SoC verified

This development checkpoint preserves the application source of installed WARP4 firmware **2.13.5+6ae1304c** before the subsequent automatic-polling/wake/UI work. It is not a stable release.

## Observed on hardware

- Key-card pairing completed with the application's correlated authenticated-status verification.
- One manual Tesla Bluetooth state-of-charge request returned **96%**.
- The same value reached the shared WARP EV state, not merely the Bluetooth UI.
- The custom firmware booted and completed automatic OTA validation; the original firmware remained in the alternate slot.

## Important limitations

- Repeated manual requests were NOT reliable: subsequent tests did not advance the successful-reception timestamp; one displayed a generic action error.
- Reliable configurable autonomous background polling, repeatable wake policy and wall-clock reception display are follow-up work and are NOT included here.
- One successful SoC request does not establish charging-control acceptance, multi-vehicle acceptance or long-term stability.
- ISO 15118 cable telemetry is a distinct source; this Bluetooth checkpoint does not populate its protocol-specific diagnostic fields.

## Source and verification

The production delta includes full-ATT-completion reply windows, bounded identical SessionInfo challenge replays with fixed freshness, and capture/dispatch deadline enforcement. The preserved build source manifest SHA-256 is:

`20af9b569a7fd8d825da27892447b3cca4644d235223315f847dd6cb08e14ed8`

Implementation: Claude Opus 5.5; independent focused source review, original-source RED reproductions, normal/ASan/UBSan regressions, clock-wrap countertest, firmware/web build and canonical OTA checks: GPT Sol.

The installed canonical OTA SHA-256 was:

`0f55eb7ccbda4730d3926e68e45c15bc2a9fff279b16fc5f7e7f931fc2423d67`

No firmware binaries, device dumps, vehicle identifiers, credentials or private build locks are published by this checkpoint. Source publication does not back up the complete SDK/toolchain environment; the retained local build/evidence directory remains necessary for exact reconstruction. Existing repository build-support documentation applies; no untested one-command reproducibility promise is made.

Publication-only change: one test's machine-specific default SDK path was replaced with `.pio/tesla-sdk`; production source is unchanged from the installed checkpoint.
