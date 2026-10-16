R11 source-only delta handoff — independent Astra delta review requested

Disposition

R11 is a narrowly scoped software/tooling correction of Astra R10-1 through
R10-4. It is not publication, installation, flash, hardware or release approval.
R6 lifecycle approval is preserved without expansion. R7/R8/R9/R10 source-only
BLOCK verdicts remain historical authoritative verdicts, including BLOCK R10
SOURCE-ONLY HANDOFF. No historical evidence has been regenerated or resealed.

Start here

    review/r11/delta-review-packet.json
    review/r11/software-evidence-summary.json
    review/r11/publication-allowlist.json
    review/r11/evidence-receipt.json

The compact packet binds exact changed paths and their hashes, source/test
pointers, gate exits, prior inventory digests, the real-index state, and limits.
It deliberately contains hashes/pointers rather than raw private evidence.
The detached receipt binds the final R11 manifest and publication allowlist.

Local R11 evidence:

    software/.pio/production-ui-evidence-lifecycle-r11/manifest.json
    software/.pio/lifecycle-r11-work/post-seal-verification.json
    software/.pio/lifecycle-r11-work/commands.jsonl

The R11 seal is delta-only: current source snapshots, changed R10 baseline
sources, new recipes/logs/checks and hash-bound references to earlier evidence.
It does not redundantly copy or reinterpret all old evidence or firmware.

Authoritative RED input

    /home/hermes/.hermes/cache/scratch/r10-independent-review-r7hg7fse/R10-independent-review.txt
    SHA-256 2db6adf6d540ed61628a361485b703fcf822126f9de3b5ad230cbdcdd21d32c5

    Reviewer index SHA-256
    b81c48e1c450da236c657552466d0ef07e2759a21c494b7ef95e5da6b4a517da

The original sdk-start-probe.py, hook-matrix.py and additional-findings.py are
unchanged. Byte-exact copies reproduced their defect-expecting assertions on
R10 before edits and again under read-only R10-baseline file mounts. Their
exit 0 means unsafe R10 behavior reproduced, not safety. The new initial
focused suite ran RED before production edits. The expanded suite was also
replayed against R10 and then GREEN against R11. Expanded baseline errors can
include missing-new-helper errors; they are not all counted as independent
exploit demonstrations. See separate exact original RED logs.

Corrections

R10-1: Shared complete output-plan validation now includes retained SDK start
identity, package/tools/provenance/version outputs, copied/stripped archives,
bootloader copy, sdkconfig outputs, existing SDK build/out descendants,
firmware identity and flat content-store outputs. The actual driver's first
preflight includes both variants before export deletion or evidence/log mkdir.
Shared inode aliases, symlink leaves/ancestors, wrong expected types and
captured substitutions reject before the planned first mutation. Direct
prepare/begin/finalize/seal/capture entrypoints preflight their own outputs.
Preparation still requires an absent output root with validated ancestry.
Read-only source/input symlinks (for example esp-idf) are not output roots.

R10-2: Both actual SCons bench hooks use the same complete BUILD_DIR/report/
export cleanup plan. BUILD_DIR must be a proper project descendant; legitimate
project/build fixtures remain supported. All build-output descendants and
known identity/merge/bootloader leaves are checked before export cleanup.
Only the explicit report symlink leaf is an unlink-only exception, including
a dangling link. Its referent is never followed for mutation. Root/ancestor
aliases, wrong types and substitutions cause zero partial export deletion.

R10-3: Quarantine no longer uses abspath's blind parent cancellation. It only
recognizes a direct alias after validating each traversed directory component.
A parent step is folded only across a proven real directory within project.
This retains valid ../build/file aliases from a real build_latest directory.
Intermediate alias chains, including Astra's build/jump -> outside/inner and
unrelated-latest -> jump/../a.bin, are conservatively preserved. The unrelated
alias AND outside bytes survive while the logged source is quarantined.

R10-4: Host and cleanroom runners validate output roots, their ancestors and
all existing descendants before mkdir/redirection/compiler invocation. This
includes external configured mbedTLS output storage, .includes files and both
configure/build logs. Dynamic test/mode path fragments are validated first.
The actual-script compiler-stop tests prove victim preservation; stop fixtures
are not reported as successful compilation. Full normal/sanitizer hosts are
separate real executions.

Bounded class-wide audit

The deterministic writer scan traverses the complete R10 allowlist, records
path hashes and Python/shell mutator sites, and classifies equivalent writers.
Related host generators, legacy host/stack audit wrappers, stack graph output,
missing-only source fixture generators and frontend generated-output roots now
use the shared checks. The frontend JSON error diagnostic uses an exclusively
created temporary file with validated parent instead of /tmp/out.json.
Module-validation generation inherits the checked target-OOM entrypoint.
Already-fresh TemporaryDirectory tools retain their fresh storage semantics.
Historical review reproduction recipes remain immutable historical controls,
not instructions to run old unsafe generations. No current required tooling
was silently removed from the proposal. This is a bounded mutator-site audit,
not an unbounded historical, embedded-runtime or third-party-build-system audit.

Verification and precise limits

The machine-readable summary records exact test counts and exits, including
R7/R8/R9/R10/R11 confinement/packaging/publication suites, retained F2-F4,
122-test IRAM regression, normal/ASan/UBSan hosts, web/safety/production-binding,
shell syntax and whitespace checks. Counts overlap and must not be summed.
The actual hooks run under minimal SCons environments. The additional generic
pio_hooks main test executes the exact main AST at its clean-return boundary;
it is not a fresh full PlatformIO/frontend build.

The current validator reconstructs/revalidates retained R10 diagnostic bytes
with read-only logical-path/PlatformIO overlays. Normal production validation
still returns expected exit 2: SDK 0x8000 versus merged WARP4 0xe000. Diagnostic
bytes remain NON_INSTALLABLE; production Tesla task symbols are absent there
and present in the retained normal ELF. In-memory R7-R10 artifact mutations
still reject. These are retained-build packaging controls, not newly compiled
R11 target firmware or a current R11 source-to-firmware binding claim. No fresh
SDK/target firmware build was requested or claimed in this delta generation.

All five prior seals and the inventoried Astra R7-R10 trees are compared by
exact deterministic path/type/mode/link/hash/size inventory, with every
hardware-evidence name excluded before descent. Original report and reviewer
index hashes are checked. No hardware-evidence contents are inspected.

Process qualifications

The first regression attempt exposed an overly strict .pio/build-only policy
that rejected the legitimate project/build control. The second exposed the
valid ../build/file direct-alias control. Those failed logs are retained; both
controls now pass without weakening the exact Astra counterexample.
The recursive output scanner also propagates unreadable-directory errors rather
than silently skipping them. A final concurrent test run exposed unrelated
temporary sibling creation changing directory link counts. Its deterministic
RED regression is retained. Directory identity now binds device/inode/mode;
regular-file link counts remain mandatory and all alias/substitution tests pass.
One bounded read-only delegated audit ran gpt-6.1-sol/openai-codex because the
delegation interface exposes no worker-profile selector; there was no provider
fallback. An unattended execute_code call was refused before execution; local
deterministic scripts were used instead. Exact namespace observations are at
gate entry, not continuous packet auditing.

Historical R9 index-byte/cache change and possible early hardware-metadata
traversal remain disclosed. A later equal index hash does not erase that event.
Historical R10 observation-harness failures and early unmeasured RED runs also
remain disclosed. New integrity success is not retroactive process approval.

Source-only publication boundary

    Sole proposed clean-snapshot parent:
    a114447c4582456d123a0832eaa2d3ce5dd29f88

Only the exact allowlisted text delta is placed in a private index/object store.
The real index is never staged; its initial/final bytes and staged entries are
checked and the real staged delta stays empty. No commit is created. The
hash-pinned unchanged R9 verifier checks indexed bytes/modes and the complete
delta, and supports raw sole-parent/full-tree verification of an eventual
separately authorized candidate. A checked parent plan is not commit proof.
Binaries, SDK/blob/build trees, raw logs/private evidence, hardware-evidence,
credentials/private vehicle/key/request material remain excluded. Existing
upstream development PEM fixtures are outside the delta; no literal PEM-free
or licensing certification is asserted.

Mechanical review commands, local/offline only

    python3 -B review/r11/verify_delta_packet.py

    python3 -B review/r11/verify_publication.py --index software/.pio/lifecycle-r11-work/proposed.index --proposed-parent a114447c4582456d123a0832eaa2d3ce5dd29f88

The second command also needs the private object store environment, as recorded
in the finalizer/readback recipe. For the complete offline readback, use the
R11 isolated namespace wrapper and finalize_r11.py verify. Never execute its
mount wrapper directly in the host namespace. No real-index operation follows
from these commands. The complete local recipes are sealed for review.

All remaining limits persist: exclusively owned workspace rather than hostile
filesystem races; no arbitrary-I/O rollback or crash durability; host/sanitizer
results are not FreeRTOS/Xtensa concurrency/race proof; mbedTLS is not sanitizer
instrumented; unresolved stack calls prevent complete bounds. Hardware,
resource, RF, vehicle, power-loss, cryptographic, licensing, upstream and release
qualification remain open. B/C and two remaining A cold boots remain NO-GO.
No hardware/serial/LAN/Proxmox/Tesla/Vaultwarden/GitHub/external services, flash,
real-index staging, commit, push, publication or release occurred.

Readiness: new independent Astra delta review only. Not approval.
