# R9 software-only driver confinement handoff

R8 remains BLOCKED. Independent GPT-6 Astra session
`20261016_092230_dff350` returned `BLOCK R8 SOURCE-ONLY HANDOFF`.
The immutable complete report SHA-256 is
`79ab1e1d86eb89e7685580994c6524e49bc53c558faea25b1245fe92b1c2daa4`.
R9 addresses only the two remaining F1-class rebuild-driver blockers.
F2/F3/F4 behavior and the R8 indexed-blob boundary are not rewritten.
R6's supplied lifecycle approval remains unchanged; R7/R8 BLOCK verdicts and
all R6/R7/R8 seals remain unchanged. New independent Astra review is required;
the implementer does not approve publication, installation or release.

## Changes and transaction boundary

`software/tools/bench_confinement.py` supplies one explicit lexical confinement
implementation shared by the shell driver and Python hooks/exporter (the hooks
continue to obtain `review_paths` through `bench_review_export`). It rejects
parent traversal before normalization, symlinked existing targets/ancestors
including dangling links, and unexpected file/directory types. The deletion
plan snapshots filesystem identities and rechecks the complete set before its
first removal; replacing a previously checked directory is rejected.

Before ANY mkdir, log creation/truncation or deletion, the actual shell driver
preflights all four ordinary/minimal success/unchecked exports, both SDK roots,
both build/out children under each SDK, the evidence directory, every exact
log leaf and the two later firmware-clean build roots. An invalid later target
therefore cannot leave an earlier target already deleted. Before each logged
command the driver repeats this full static validation. SDK cleanup additionally
requires an exact root under the explicit project `.pio`, not just a matching
basename, and preflights the root, both cleanup targets and every ancestor before
removing either child. Builder inputs are retained.

This is fail-closed deterministic path/substitution validation for an exclusively
owned private build workspace. It is NOT proof against a hostile concurrent
rename/mount between checks and filesystem operations, a filesystem crash, or
arbitrary tool output paths chosen by a hostile environment. No claim of such
race resistance is made. Run no concurrent writers in these private trees.

## Immutable RED and stronger GREEN

Astra's entire retained R8 review tree and prior R7 tree are hash/mode inventoried
before changes and rechecked after sealing. Original reproducers are not edited
or weakened. `additional_confinement_probe.py` runs byte-for-byte against the
pinned pre-fix driver, initially and again via a read-only baseline file mount.
Its exit 0 means the defects reproduced: four export sentinels and both SDK
build/out sentinels were deleted through aliases.

That original probe intentionally asserts the old driver hash, extracts its old
inline `-c` SDK command, and expects deleted sentinels. It cannot honestly be a
GREEN test of changed source without rewriting those assertions. R9 retains it
as RED evidence, not as an artificially passing post-fix test. The separate
`replay_driver_green.py` uses the same alias/sentinel fixtures against the actual
new driver and actual replacement SDK entrypoint, including the SDK ancestor
case. It requires diagnostic rejection, ALL sentinels surviving, zero rmtree
calls and no evidence/log creation. New regression tests also cover late-stage
and late-minimal-SDK aliases, log/evidence aliases, lexical traversal, wrong-root
same-basename substitution, captured target identity substitution, and regular
successful cleanup controls. They ran RED before production edits, then GREEN.

Astra's separate unchanged `independent_probes.py` is also replayed GREEN against
R9 (its output directory alone is privately remapped). All prior F1–F4 checks
remain mandatory. Exact argv/exits are in the local R9 `commands.jsonl`; expected
RED exits and any superseded attempts are retained, never presented as GREEN.

## Separate evidence and source-only publication proposal

Local seal: `software/.pio/production-ui-evidence-lifecycle-r9/manifest.json`.
Detached publication-safe receipt: `review/r9/evidence-receipt.json`.
Compact safe summary: `review/r9/software-evidence-summary.json`.
Exact source hashes, sizes and modes: `review/r9/publication-allowlist.json`.
Reproduction scripts and raw logs remain local to the R9 evidence bundle.
Manifest/allowlist/receipt digests are detached to avoid recursive self-hashes.
No R6/R7/R8 manifest, bundle or detached control is regenerated.

Verification process exception: the real index byte hash changed during SDK
preparation, from `a74b7a61fdc4173816ecec27199054e3bad56b431be9bfb0e818fd5e84570218`
to `d24f5513141911e4595445fc75907f82598eba36c0c24a126c6b587ec3c5d3aa`.
The initial and final staged deltas are empty and HEAD is unchanged; content and
modes were not staged. The exact refreshing Git invocation is not proven.
Original index bytes were not preserved, and no restoration was attempted.
The exception is retained explicitly in the R9 summary/receipt integrity data;
remaining verification and reproduction disable optional Git refreshes. All
proposed changes use separate private index/object stores. Do not describe the
real index as byte-for-byte unchanged in this R9 run.
Initial Git status commands were not protected by the later build-overlay
hardware-evidence mask. No hardware-evidence file contents were intentionally
opened or copied, but Git may have traversed directory metadata; strict zero
metadata access cannot be attested. This process exception is disclosed rather
than represented as full boundary compliance. No further unmasked status scan
is used; proposed-index checks inspect Git objects/index entries only.

Use the R9 verifier and R9 allowlist, not historical verifier inputs. It compares
the ENTIRE proposed private Git-index delta against the pinned upstream base,
then checks exact indexed blobs and regular modes, not merely worktree hashes.
Extras, deletions, missing files, conflicts, symlinks, binaries, wrong blobs and
executable-bit substitutions are rejected. Detached controls must equal the
exact reviewed local controls; verifier/allowlist are trusted review inputs,
not cryptographic self-approval.

Clean snapshot ancestry is now an explicit requirement, not a preference:
sole proposed parent `a114447c4582456d123a0832eaa2d3ce5dd29f88`.
The local prototype branch HEAD/history MUST NOT be published or used as parent.
The private index is constructed from that upstream tree and only the exact
R9 allowlist. No real-index staging, commit, branch/ref change or publication
occurs. Read-only proposal check from repository root:

    python3 -B review/r9/verify_publication.py --index /absolute/private/index --proposed-parent a114447c4582456d123a0832eaa2d3ce5dd29f88

This proves the proposed tree boundary and pinned parent PLAN. It cannot prove
ancestry of a commit that has not been created. Before eventual publication a
separately authorized publisher MUST rerun with `--candidate-commit <exact-id>`;
that additionally requires the actual commit's sole parent to equal the base and
its full tree to equal the exact proposed index. Existing local HEAD is tested
and rejected as a candidate. No candidate commit is created in R9.

All binary firmware/BIN/ELF/map/archive/object outputs, SDK/toolchain/input-blob/
build trees, full local evidence, raw logs, private vehicle/key/request material,
credentials/Wi-Fi, UART/full-flash captures and `hardware-evidence` are excluded.
Unchanged upstream development PEM fixtures remain as documented in
`R6_EVIDENCE_CORRECTION.md`; no claim of literal private-PEM absence is made.
Do not `git add .`, archive the worktree, or publish prototype history.

## Verification and unchanged limitations

The compact summary records final tests, commands/exits, source/build identities
and prior-integrity results. Required gates include driver and prior F1 tests,
R7/R8 packaging, image/IRAM, R8/R9 publication-index, normal and ASan/UBSan host,
web/safety/production binding, fresh diagnostic SDK, clean normal/diagnostic
firmware with compiler caches disabled, current ELF/map/stack/heap and exact
export reconstruction. Counts overlap and must not be added as a unique total.
Builds use local inputs in network-isolated private overlays; hardware evidence
is masked, never read. Diagnostic source-input binding includes the new helper.

Normal production packaging remains BLOCKED: SDK partition reader `0x8000`
versus WARP4 split `0xe000`. Diagnostic packaging with the actual `0xe000`
bootloader remains NON_INSTALLABLE and lacks the production Tesla task. It does
not supply production lifecycle, boot or resource qualification.
No hardware, serial, LAN, Proxmox, Tesla, Vaultwarden, GitHub or external service
access. No flash, real-index staging, commit, push, publication or release. The index
cache/possible Git metadata-traversal process exceptions above remain disclosed.
Host tests are not FreeRTOS/Xtensa concurrency proof; ASan/UBSan are not race
detectors and host mbedTLS is not sanitizer-built. Stack observations contain
unresolved calls and are NOT complete bounds. Hardware/resource, RF, vehicle,
power-loss, crypto, licensing, upstream and release qualification remain open.
B/C and the two remaining A cold boots remain NO-GO.

Recommendation after final readback: submit the exact R9 source-only proposal,
receipt and local evidence for another independent Astra review. Not publication
approval; do not infer that the historical R8 BLOCK has been overturned.
