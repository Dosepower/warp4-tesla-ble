# R8 software-only packaging correction handoff

R7 is BLOCKED, not approved. Independent GPT-6 Astra session
`20261016_074934_0c8be6` returned `BLOCK R7 SOURCE-ONLY HANDOFF`.
This R8 stage addresses its four findings; it does not rewrite that review or
R6/R7 evidence. R6's supplied `APPROVE R6 SOFTWARE LIFECYCLE` remains unchanged.
R8 requires a new independent Astra review. The implementer does not approve
publication, installation, deployment or release.

## Corrections

F1: both build hooks and the exporter use one destination/stage confinement
check. It rejects symlinks at either target or ANY ancestor before either old
success or unchecked directory can be deleted. Tests preserve scratch sentinels
for symlinked `.pio`, `bench-exports`, destination and unchecked stage, and assert
that no deletion was attempted. The pre-guard also validates both targets first.
This is deterministic path confinement in an exclusively owned private build
workspace, not proof against a hostile concurrent filesystem replacement race.

F2: each loadable segment's entire address range must be covered by allocated
ELF sections or their exact image-format padding. The only accepted loadable
padding is esptool's per-section length rounding to four bytes: zero-filled
0–3 byte section tails, including between merged adjacent sections. Leading,
interior and trailing unbacked extents and a 4096-byte extension of an otherwise
backed segment are rejected. Byte comparison still rejects nonzero padding.
Section overlaps are rejected. Non-loadable address-zero MMU alignment segments
remain subject to the existing zero-byte comparison; they do not authorize any
additional loadable extent. This is a pinned packaging checker, not a general
ESP image/ELF validator or hardware execution proof.

F3: classic ESP32 flash mode/frequency encodings are checked and narrowed to the
pinned DIO ROM header and 80 MHz encoding (`2`, `0xf`). All three generated SDK/
boot config views must select QIO, describe the generated header as `"dio"`,
and select/describe `80m`. This deliberately preserves IDF's DIO boot header
followed by QIO operation; equating the header byte to the QIO runtime selection
would be wrong. Invalid values and supported-but-unpinned values are tested,
including bootloader mutations. Fixtures now model this actual configuration.

F4: the existing R7 publication-failure test now delegates all component renames
and injects ONLY at `ordinary.unchecked -> ordinary`. It asserts that this exact
operation was reached once, the complete stage/report existed, the destination
was absent, and both destination and stage are absent after failure. The exporter
already passed this case; this fixes coverage, not a new exporter defect.

## RED, GREEN and immutable provenance

Astra's original probes/logs remain unchanged. Their exact originals and hashes
are retained in R8 local evidence. They reproduced deleted sentinels and accepted
F2/F3 mutations against pre-fix production source. New regressions were run RED
before production edits, and again with exact baseline production files mounted
read-only in a private offline namespace. The current test harness alone models
the legitimate DIO/80m configuration. Post-fix GREEN replay uses the unchanged
adversarial probe. The symlink probe requires a harness-only pacing adjustment:
catch the new EARLY hook rejection and invert its obsolete final expectation of
deleted sentinels in memory. Original files are never rewritten.

The original unconditional-rename demonstration remains intentionally unchanged:
it still shows why the OLD patch failed at `firmware.elf`. The corrected test and
Astra's separate final-rename probe establish the final publication failure case.

Current results and exact source/build identities are in
`review/r8/software-evidence-summary.json`. The separate local sealed bundle is
`software/.pio/production-ui-evidence-lifecycle-r8/manifest.json`; its detached
hash/count receipt is `review/r8/evidence-receipt.json`. Raw logs, reproduction
scripts, baseline snapshots and selected NON_INSTALLABLE build/stack evidence
are local only. Suite counts overlap and must not be added into a unique total.

R6 manifest SHA-256:
`464828efe10fc0ee34b49c97c07b6c75ff75f091dda09240c7bb2f41c31b08ab`
R7 manifest SHA-256:
`78629cbe0e2ac04d15b7625e2b149b3b90528537a8c98eaf973a46b09678b895`

## Publication boundary

Use ONLY `review/r8/publication-allowlist.json`, not the historical R7 verifier.
R7 documents/receipts remain historical and are superseded by this R8 status.
The R8 verifier requires an explicit proposed Git index, compares its ENTIRE
NUL-delimited delta against upstream
`a114447c4582456d123a0832eaa2d3ce5dd29f88` with the exact allowlist (including
named controls), checks regular indexed modes and hashes the INDEXED blobs.
It rejects extras, missing paths, unlisted deletions, symlinks, binary data and
changed staged payloads even if the working files are correct. Controls must
match the exact reviewed local control files; the allowlist and verifier remain
trusted review inputs, not self-authenticating publication approval.

Read-only verification command, from the repository root:

    python3 -B review/r8/verify_publication.py --index /absolute/path/to/proposed-index

The implementer verifies a private alternate index/object store only. The real
Git index is not staged, no commit is made, and nothing is published. A later
publisher MUST rerun this against its exact proposed index after independent
review; do not use `git add .`, archive the worktree or publish prototype history.
A clean source snapshot based on the named upstream revision remains preferable.

Exclusions: all firmware/BIN/ELF/map/archive/object output, SDK/toolchain/blob/
build trees, full local seals, raw logs, private identifiers/key storage,
credentials/Wi-Fi, request dumps, UART/full-flash captures, qualified hardware
images and `hardware-evidence`. There is NO proposed binary publication.
Unchanged upstream ISO15118 development key fixtures remain as documented in
`R6_EVIDENCE_CORRECTION.md`; this is not a claim of literal private-PEM absence.

## Remaining gates and honest recommendation

Fresh clean normal and diagnostic firmware builds disable compiler caches and
run in private source/package overlays with networking disabled. Diagnostic SDK
inputs are rebuilt separately from pinned local sources. Current ELF/map/heap/
stack checks use the actual roots present in each build. The diagnostic image
has no production Tesla task and cannot validate production lifecycle execution.

Normal production packaging remains BLOCKED: retained normal SDK partition
reader `0x8000` versus WARP4 split layout `0xe000`. Successful diagnostic packaging
with an actual `0xe000` bootloader is NON_INSTALLABLE, not a production fix.
No hardware, serial, LAN, Proxmox, Tesla, Vaultwarden, GitHub or other external
service was accessed. No flash, commit, push, publish or release was performed.
R6/R7 seals and Astra originals are checked unchanged; hardware-evidence is not
read, scanned, modified or included.

No hardware/runtime-resource, RF, vehicle interoperability, power-loss,
cryptographic, licensing, upstream or release approval is supplied. Host tests
are not FreeRTOS/Xtensa concurrency proof; ASan/UBSan are not race detectors and
host mbedTLS archives are not sanitizer-built. Stack sums retain unresolved
indirect calls and are NOT complete bounds. B/C and two remaining A cold boots
remain NO-GO.

Recommendation: submit this exact R8 source-only delta and detached receipt for
new independent Astra review. Do not represent R7 or R8 as publication-approved.
