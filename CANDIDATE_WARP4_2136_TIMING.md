# WARP4 2.13.6 Tesla timing candidate

Offline-qualified custom firmware 2.13.6+6ae39075, subsequently installed and automatically boot-validated on the test device. Existing vehicle/MQTT configuration and Tesla pairing retained. Full vehicle wake/charge/resume acceptance remains pending. Based on official warp4-2.13.6 source afa5c782176f2397ead9020ddc346b3d163fd840.

- Tesla SoC: bounded 25-second authentication/wake phases, fresh 1-second session replays with 4-second challenge lifetime, 90-second absolute limit, bounded exponential response retries.
- Existing single-phase pairing timings and authenticated late-approval proofs retained. Wake exposure spacing remains 120 seconds, periodic minimum 5 minutes.
- Custom ESP-IDF5.5.6/Arduino3.3.12 SDK rebuilt from public pinned sources; managed component and patch evidence retained locally. Internal upstream git-am revisions are not publicly resolvable: no official-binary equivalence claim. Old SDK not relabelled.
- Host/Sanitizer, real firmware/web build, frontend/TypeScript, SDK link audit and OTA/slot checks passed locally. Independent scoped reviews passed after corrections.
- Real vehicle plug/wake/charge/resume failures are NOT claimed solved until hardware tests.

No firmware binaries, private vehicle identifiers, provisioning keys or private logs published. Keep a valid original firmware slot; custom builds require separately qualified SDK/toolchain, artifact and device authorization. Main remains the previous installed source baseline pending acceptance.
