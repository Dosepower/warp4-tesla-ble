# Charge-aware installed checkpoint

Source checkpoint corresponding to locally installed and automatically boot-validated experimental firmware 2.13.5+6ae20f71. No firmware binary, keys, vehicle identifiers or private logs published.

- Initial SoC request on plug-in; bounded retries.
- Periodic requests only during actual charging, preserved value/receive time during pause.
- Charge resume demand retained across a previously running request.
- Independent host tests, ASan/UBSan, full firmware/web build and OTA verification passed locally.
- A real SoC request succeeded while charging. Sleeping-vehicle wake reliability remains under investigation; do not treat this as a fully qualified stable release.
- Current per-profile wake setting remains effective in this checkpoint. UI/wake cleanup is a subsequent change, deliberately not mixed into this recovery baseline.

## Recovery
Use the source and existing project build documentation with the separately retained local pinned SDK/toolchain. Device provisioning and original firmware must be preserved; do not publish private build/install state or use arbitrary unsigned binaries. This source checkpoint is not a complete SDK/environment backup.
