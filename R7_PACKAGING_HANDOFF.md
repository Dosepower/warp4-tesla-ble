# WARP4 Tesla BLE — R7 software packaging/source-review handoff

## Scope and review state

This is source-publication preparation only. Nothing has been committed, pushed,
flashed, released or sent to GitHub/Tinkerforge by this implementation run.
No hardware, serial, LAN, Proxmox, Tesla, Vaultwarden, qualified-A image or prior
hardware evidence was accessed. Later independent R7 review is pending; no
Astra invocation was made for R7. Parent verification/publication is separate.

Local implementation HEAD: `187e00a29fd0f39f47db010cec9f832e9f535158` on
`diagnostics/esp32-ethernet-iram-prep`, plus the explicitly inventoried working
source. Upstream source base: `a114447c4582456d123a0832eaa2d3ce5dd29f88`.
The local Git repository is shallow. The parent's statement about published
branch `feature/tesla-ble-upstream-ready` at `9db9f948...` was not remotely
checked. Do not represent a local HEAD as the published branch.

R6 lifecycle approval was supplied by the parent: `APPROVE R6 SOFTWARE LIFECYCLE`.
R6 remains immutable, manifest SHA-256
`464828efe10fc0ee34b49c97c07b6c75ff75f091dda09240c7bb2f41c31b08ab`.
Read `R6_EVIDENCE_CORRECTION.md`: firmware/source contains two unchanged upstream
ISO15118 development private-key PEM fixtures. The claim is no customer/Tesla
credentials or private/hardware evidence, **not** literal PEM absence.

## Layout investigation and honest packaging boundary

WARP4 inherits `default_16MB_secure.csv` and `custom_split_esptool_ota=true`.
`merge_firmware_hook.py` consequently places the table at absolute `0xe000`;
The partition CSV labels `0xd000` as a bootloader-signature slot; other upstream
split/OTA paths also place firmware-info at `0xd000`. This review
package deliberately does not make firmware-info, OTA, signing or provisioning
outputs. The table contains six exact unencrypted entries: app0 `0x10000`,
otadata `0x640000`, factorydata `0x642000`, app1 `0x650000`, data `0xc90000`,
coredump `0xff0000`. The review envelope ends before factorydata.

The retained **normal** SDK (`5.5.5+local.2553c5ad432`) configures its partition
reader at `0x8000`. Its rejection is still correct; no metadata was relabelled
and no `0x8000` table was simply relocated. This normal production image remains
unqualified, not installable and blocked for packaging/flash approval.

A separate fresh **ordinary diagnostic bench SDK** is compiled from pinned
IDF `2553c5ad432927e09ccbbad3b954e1cbc4bd7655` and Arduino
`f6225a7c680009d7353dca2dcdc252f60d7cba03`, with retained repository patches and
`defconfig.warp4-ble-bench`. Exact source/config/toolchain/export hashes are in
R7 evidence. Its generated headers and actual bootloader Xtensa partition-read
literal must both equal `0xe000`. Bootloader BIN must match that exact ELF and
fresh SDK export. The validator rejects altered config/security settings,
revision ranges excluding classic ESP32 rev 3.1, mismatched tables and images,
ELF loadable bytes, unbacked/overlapping loadable segments, stale SDK identity,
map section identity, wrong pre-compilation boot_app0 bytes and merged gaps.

**The bench environment is not the production lifecycle environment.**
`TESLA_BLE_BENCH_ONLY` + IRAM diagnostics deliberately prevents
`TeslaVehicle::start_runtime()` from creating a production Tesla runtime.
Its ELF therefore has diagnostic task roots, not a production Tesla task.
No isolation, authentication, deletion, resource or admission gate was removed.
Do not use its successful packaging to claim target lifecycle validation.

The only generated review envelope is isolated under
`software/.pio/lifecycle-r7-work/.pio/bench-exports/ordinary/` and copied into
local immutable R7 evidence. All members are labelled `NON_INSTALLABLE`, raw
objects use `.review-bytes`, and the report sets `installable=false`.
This is NOT FOR END USERS, NOT A RELEASE, and NOT an installation/flash approval.
A filename cannot prevent deliberate misuse; no flash command is supplied.

## R7 implementation and tests

R7 changes packaging tools/tests and handoff documents, not the approved
production Tesla lifecycle source. The transaction validates original bytes,
hash-checks every copied component, validates the exact private stage again,
reads back image/report identities, then renames a complete fresh directory.
Failures leave no success directory. Production/build_latest/OTA exports are
not destinations. Mutable paths are never trusted as a substitute for the
validated byte identities.

Exact pre-R7 packaging-source replay preserves six assertion REDs and nine
passing controls. Failures cover copy-after-validation mutation, report naming
of a different image, two map substitutions, same-sized OTA substitution and an
unbacked loadable segment. Existing identity/security/layout tests remain active.
A further intermediate assertion RED exposed an export-variant naming mismatch;
the export now refuses any label inconsistent with the validated variant.
The final new suite has 17 tests, including publication-rename failure cleanup.
GREEN results, exact commands and exits, firmware/ELF hashes, source snapshots,
selected build artifacts, limitations and superseded attempts are recorded in:

`software/.pio/production-ui-evidence-lifecycle-r7/manifest.json`

The detached manifest SHA-256 is in `review/r7/evidence-receipt.json` and the
immutable bundle's `manifest.sha256`. The receipt is generated after sealing to
avoid a circular manifest hash. `review/r7/software-evidence-summary.json`
contains the concise, publication-safe results; raw multi-megabyte input/build
provenance is local evidence, not part of the proposed public source branch.

Software regression commands (from repository root):

- `bash software/tests/tesla_vehicle/run_host_tests.sh`
- `bash software/tests/tesla_vehicle/run_host_tests.sh sanitize`
- `python3 -m unittest discover -s software/tests/tesla_vehicle -p 'test_r7_review_packaging.py' -v`
- `python3 -m unittest discover -s software/tests/tesla_vehicle -p 'test_iram_*.py' -v`
- `node software/tests/tesla_vehicle/test_web_actions.mjs`
- `node software/tests/tesla_vehicle/test_web_integration.mjs`
- `python3 -B software/tests/tesla_vehicle/cleanroom/test_safety.py`
- `python3 -B software/tests/tesla_vehicle/cleanroom/test_production_binding.py`
- `git diff --check`

Host framework/mbedTLS input paths and exact offline namespace/build commands
are preserved in R7 `reproduction/`. See `review/r7/reproduce.sh` for the SDK and
firmware command sequence with explicit caller-supplied pinned inputs. Builds
ran in private copy-on-write source/package overlays with default-Wi-Fi inputs
removed and compiler caches disabled. No shared latest or production export was
modified. ELF/map/native heap-subtraction/stack/IRAM checks run on both fresh
normal and diagnostic ELFs; normal production stack-budget checks are retained.
Diagnostic stack observations use the roots actually present, not a silently
missing production worker. Stack sums with unknown indirect calls are not bounds.

## Publication and remaining decisions

`review/r7/publication-allowlist.json` is an exact source/test/docs delta to the
upstream base, with hashes and explicit exclusions. Use that allowlist, not
`git add .`, a working-directory archive, or the entire `.pio` tree. A clean
source-snapshot branch based on the named upstream revision is preferable to
publishing prototype history containing the earlier removed legacy dependency.
Include Clean LICENSE/NOTICE/PROVENANCE and preserve existing WARP/Espressif
licenses. Mechanical clean-room/license checks are not legal or OEM approval.

Only compact results, the detached receipt and handoff source are proposed as
public evidence. Exclude local immutable bundles, all firmware/ELF/map/archive
bytes, SDK/toolchain/blob/build trees, raw logs, hardware-evidence, Wi-Fi/credential
files, identifiers, key storage, request dumps, UART captures and full-flash
backups. There is no proposed binary GitHub release.

Recommended next step: parent reviews the exact allowlisted source, followed by
independent R7 review when the required account window is available, then may
publish a source-only review branch to `Dosepower/warp4-tesla-ble` and decide what
to link to existing issue #535. The implementation agent does neither.

Tinkerforge must provide/approve a WARP4 production SDK/bootloader/table/security
chain for its exact board and silicon policy before any target installation is
considered. The fresh non-secure diagnostic package does not meet that need.
Hardware/runtime-resource, actual FreeRTOS/Xtensa concurrency, RF, vehicle
interoperability, power-loss recovery, cryptographic/licensing/upstream approval,
flashable-image qualification and release readiness remain unapproved.
ASan/UBSan are not race detectors; host mbedTLS archives are not sanitizer-built.
**B/C and the two remaining A cold boots remain NO-GO.**
