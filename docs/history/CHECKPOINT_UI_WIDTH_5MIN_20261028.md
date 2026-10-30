# Button alignment and 5-minute minimum offline-qualified checkpoint

Source checkpoint corresponding to offline-qualified experimental firmware candidate 2.13.5+6ae25bce (not yet installed). No firmware binary, keys, vehicle identifiers or private logs published.

- Settings/diagnostics blue buttons aligned to the red actions; colors and action gates preserved. Permanent unpair helper removed.
- Periodic interval choices start at 5 minutes. API rejects shorter intervals; legacy file restore normalizes only that field. Initial/resume/manual/post-wake timings unchanged.
- Initial SoC request on plug-in; bounded retries.
- Periodic requests only during actual charging, preserved value/receive time during pause.
- Charge resume demand retained across a previously running request.
- Independent host tests, ASan/UBSan, full firmware/web build and OTA verification passed locally.
- Wake-only evidence is distinguished from authenticated awake evidence. One bounded follow-up request without another wake is permitted after wake without SoC. Real sleeping-vehicle acceptance is still pending; do not treat this as a stable release.
- Wake switch removed; permitted plug/charge/manual requests now couple wake automatically, retaining spacing and safety gates. Clean UI with Settings/Diagnostics, request spinner and chronological scan status; cryptographic pairing gates remain unchanged.

## Recovery
Use the source and existing project build documentation with the separately retained local pinned SDK/toolchain. Device provisioning and original firmware must be preserved; do not publish private build/install state or use arbitrary unsigned binaries. This source checkpoint is not a complete SDK/environment backup.
