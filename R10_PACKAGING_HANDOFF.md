# R10 software-only hard-link and quarantine correction

Independent GPT-6 Astra R9 review returned `BLOCK R9 SOURCE-ONLY HANDOFF`.
The complete immutable report SHA-256 is
`7e59c88f7da9dadcd9228957636ca511daaa8adae3012709446032235843d488`.
R10 addresses only R9-1 and R9-2, with separate evidence and new review inputs.
R6 lifecycle approval and the R7/R8/R9 BLOCK verdicts, reports and seals remain
unchanged. This is not implementer publication approval.

## R9-1: shared-inode logs

The existing shared confinement `stamp()` now rejects regular files with link
count other than one and includes link count in its captured identity. Existing
all-target driver preflight therefore rejects the initial `idf-export.log` AND
all fourteen later `run_logged` log paths before export cleanup, SDK cleanup,
mkdir or shell log redirection. Adding a hard link after a plan was captured is
also rejected at its pre-mutation revalidation. No shell-driver rewrite is
needed; the existing complete log enumeration is retained unchanged.

Actual-driver regressions cover every named log with eight export/SDK sentinels
and a separate hard-linked victim. Rejection requires exit 2, zero rmtree calls,
all sentinel/victim bytes unchanged and an unchanged fixture inventory/mode/
inode/link-count snapshot. Safe single-link controls allow intended cleanup.
A separate actual-driver control supplies only a local no-op IDF export and a
local stop stub: it reaches `run_logged`, safely logs SDK cleanup, then stops
with fixture exit 86 before any SDK build. This is a test fixture, not fabricated
SDK output or a claimed firmware build.

## R9-2: all-target legacy quarantine

The retained `quarantine_legacy()` uses the shared lexical/ancestor/type model.
Before discovery it validates project, output/quarantine roots and input logs.
It rejects parent traversal rather than resolving aliases away. Before its
first mkdir/move/unlink it captures ALL selected regular sources, explicit
unlink-only symlink leaves, and ALL absent destinations in one plan, rejects
any destination conflict, revalidates every identity and checks source bytes.
A conflict in a later destination can no longer leave an earlier source moved.

The shared helper's only symlink exception is explicitly named leaf links that
will be unlinked without following their referents. Their parent chain, inode,
mode and link text are checked. Legacy discovery recognizes direct lexical
aliases to logged source paths without resolving through unrelated filesystem
paths; it does not broaden discovery into arbitrary chained aliases. Unrelated
production files and aliases are preserved. Regular hard-linked quarantine
sources are conservatively rejected as well.

Tests cover project/.pio/quarantine aliases (including dangling aliases), wrong
types, lexical traversal, later conflicts, captured source/ancestor/unlink-leaf
identity replacement and a previously absent destination becoming occupied.
They assert no utility mkdir, move or unlink before rejection. Regular controls
retain the prior logged-output-only behavior and byte checks.

These are deterministic pre-existing-alias and statically injected substitution
protections for an exclusively owned private workspace. They are NOT resistance
to a hostile concurrent rename/mount after checks, all-or-nothing recovery from
arbitrary later I/O failure, or crash/power-loss durability. No such claim is
made. Do not run concurrent writers in these private trees.

## Exact immutable RED and stronger GREEN

The original Astra R9 driver and quarantine scripts are retained byte-for-byte,
with the reviewer tree inventoried before changes and checked afterwards.
They first ran unchanged against R9 production source. Their exit 0 means their
defect-expecting assertions reproduced RED: hard-linked victim overwritten,
export sentinels removed, ancestor-redirected quarantine accepted and a later
conflict caused a partial move. They are rerun against exact baseline files via
read-only namespace mounts. New regression tests ran RED before production edits
and GREEN after them. An expanded final harness is replayed against baseline too.

The original probes pin old hashes and/or assert unsafe outcomes; do not rewrite
them into artificially passing post-fix probes. R10's independent regression
harness supplies stronger safety assertions against the actual corrected
entrypoints. The unchanged earlier Astra F1–F4/copy-time/final-rename probe is
also replayed GREEN; only its result output is redirected to new R10 storage.
F2–F4 implementation is unchanged.

## Evidence, process qualifications and publication

Local immutable seal:
`software/.pio/production-ui-evidence-lifecycle-r10/manifest.json`.
Detached safe receipt: `review/r10/evidence-receipt.json`.
Compact summary: `review/r10/software-evidence-summary.json`.
Exact hashes/sizes/modes: `review/r10/publication-allowlist.json`.
Raw commands/logs/reproductions, baseline snapshots, reviewer originals and
selected NON_INSTALLABLE build evidence remain local only. The detached receipt
avoids recursive manifest/control self-hashes. Prior controls are not regenerated.
No historical handoff or verdict is silently rewritten; this new file is the R10
entry point. Counts overlap and must not be added into a unique aggregate.

R9 process exceptions remain disclosed: an unintended real-index byte/cache
change was recorded from `a74b7a61fdc4173816ecec27199054e3bad56b431be9bfb0e818fd5e84570218`
to `d24f5513141911e4595445fc75907f82598eba36c0c24a126c6b587ec3c5d3aa`,
with empty staged deltas but an unproven triggering invocation. Astra later
observed the original hash again. That does not prove what happened during R9;
R9 cannot be called byte-preserving. R9 also disclosed early unmasked Git status,
so strict zero hardware-evidence metadata access cannot be certified for R9.
These facts are not erased by a subsequent R10 verification.

R10 uses no unmasked Git status scans and never-descends hardware-evidence names
when inventorying prior reviewer trees. Its initial/final staged delta, index
bytes/entries and HEAD are checked directly. Verification namespaces mount real
`.git`, prior seals and reviewer trees read-only and mask hardware-evidence.
New proposals use private index/object storage only. Gate records directly
observe namespace inode, loopback-only interfaces and no non-loopback routes at
entry; these observations are not a continuous packet audit. Two initial harness
attempts stopped before executing a gate: `/proc/1/ns/net` was inaccessible, and
an empty IPv4 route file differed from the assumed header-only form. Discovery
was corrected using an outer namespace inode and empty/header-only route check.
No production fix was weakened or privileged/network retry used.

The R10 verifier delegates to the hash-pinned, unchanged R9 verifier. The entire
proposed private-index delta against upstream, all indexed text bytes/modes and
exact reviewed controls must match the NEW R10 allowlist, not the old R9 input.
No extra additions/deletions, binaries, symlinks, conflicts or staged substitutions
are permitted. Existing R8/R9 index tests and R10 input/wrapper tests remain gates.
The sole proposed clean-snapshot parent is exactly
`a114447c4582456d123a0832eaa2d3ce5dd29f88`; publishing local prototype history is
forbidden. The parent is a PLAN, not ancestry proof of a nonexistent commit.
Before eventual separately authorized publication the exact actual candidate
must additionally pass `--candidate-commit`, checking its raw sole parent and
entire tree against the verified index. No candidate is created here.

Read-only proposal verification from the repository root:

    python3 -B review/r10/verify_publication.py --index /absolute/private/index --proposed-parent a114447c4582456d123a0832eaa2d3ce5dd29f88

Exclude firmware/BIN/ELF/map/archive/object output, SDK/toolchain/build/blob trees,
full evidence, raw logs, hardware-evidence, credentials/Wi-Fi and private vehicle/
key/request material. Only exact listed text is proposed. No binary publication.
Unchanged upstream development PEM fixtures remain as documented in
`R6_EVIDENCE_CORRECTION.md`; this is not a literal private-PEM-free-tree claim.

## Unchanged limits and recommendation

The new summary records actual test counts, commands/exits and fresh identities.
Required gates retain R7/R8/R9 packaging/publication, driver/confinement, image/
IRAM/F2–F4, normal+ASan/UBSan host, web/safety/production binding, fresh clean
normal/diagnostic firmware, diagnostic SDK, current ELF/map/heap/stack and export
readback. Compiler caches are explicitly disabled and logs checked; normal SDK
inputs are reused via a copy, not claimed freshly rebuilt. Fresh source-input
binding separates the three generated build/UI outputs from source equality.

Normal production packaging remains expected BLOCKED: SDK `0x8000` vs WARP4
split `0xe000` (validator exit 2). Diagnostic bytes remain NON_INSTALLABLE and
lack the production Tesla task; this is not a production workaround or target
execution evidence. Host/sanitizer testing is not FreeRTOS/Xtensa concurrency
proof, ASan/UBSan are not race detectors, host mbedTLS is not sanitizer-built,
and unresolved stack calls prevent complete bounds. Hardware/resource, RF,
vehicle, power-loss, cryptographic, licensing, upstream and release qualification
remain open. B/C and two remaining A cold boots remain NO-GO.

No hardware, serial, LAN, Proxmox, Tesla, Vaultwarden, GitHub or external service;
no flash, real-index staging, commit, push, publication or release. No hardware-
evidence contents are inspected. Recommendation: send the exact R10 proposal and
receipt to a new independent Astra review only. Do not self-approve publication
or imply the historical R9 BLOCK verdict was reversed.
