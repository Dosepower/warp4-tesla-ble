R12 frontend digest correction — bounded independent Astra review requested

Disposition and scope

R12 repairs only R11-1 and equivalent digest calls in the current pio_hooks
frontend path. Only software/pio_hooks.py and software/util.py change at
runtime. No whole-tree or historical audit was launched. Historical R6 approval
and R7/R8/R9/R10/R11 BLOCK verdicts remain unchanged; nothing is approved for
publication, installation, hardware operation or release by this handoff.

Start with review/r12/delta-review-packet.json and its mechanical verifier:

    python3 -B review/r12/verify_delta_packet.py

The packet contains exact changed/new paths and hashes, focused RED/GREEN
pointers, unchanged-source and protected inventory digests, hash-bound reused
R11 results, current gate exits, boundary result and limits. It is intentionally
small; it does not duplicate raw historical logs/manifests. The detached receipt
binds the final immutable R12 delta manifest, allowlist, packet and summary.

Authoritative input

    /home/hermes/.hermes/cache/scratch/r11-independent-delta-FSy9ZBkl/R11-independent-delta-review.txt
    SHA-256 2a7e4d1ecf818fbbbd90d4fa9a511b9ecbc08d23ff589306c50f466bd6be0898

    R11-evidence-index.json SHA-256
    dcb54d9c1cdcce16ff1364036f1e67be15c887189dbaa0ac63aaa3a7ba9cbe02

    Verdict: BLOCK R11 SOURCE-ONLY HANDOFF

Astra's frontend_digest_probe.py is preserved byte-for-byte in the original
reviewer tree and as a read-only R12 evidence copy. Exact copies ran before
edits and again using read-only R11-baseline source mounts. Exit 0 means its
unsafe symlink-deletion assertion reproduced, while ordinary/hardlink controls
preserved the victim. The original was not rewritten into a GREEN probe.
The new focused regression ran RED before runtime edits and was also replayed
against the baseline after expansion. RED failure/error counts are not counts
of separate exploit demonstrations; exact Astra reproduction is recorded apart.

Correction and declared semantics

The single pio_hooks.main entry output plan now explicitly includes:

    web/node_modules.digest
    $BUILD_DIR/web,node_modules.digest
    src/index_html.digest
    $BUILD_DIR/src,index_html.digest

Both pairs therefore validate lexical ancestry, expected regular-file types,
shared-inode safety and captured identities before any generated cleanup or
other entry mutation. Source-bound substitution tests inject after plan capture
and require rejection before the first mutation. Existing generated-output
roots/index/marker checks remain intact.

util.get_digest_paths no longer applies realpath to mutable output leaves. It
constructs lexical paths, validates project/build/destination ancestry and leaf
names, and preflights the complete pair before returning paths to any caller.
check_digest reads through that checked path selection. remove_digest and
store_digest capture/revalidate both leaves before the first unlink, mkdir,
write or digest registration. Symlink leaves, including dangling links, are
REJECTED, not unlinked or followed. Hardlinks/shared inodes are conservatively
rejected too, even though Astra's old unlink-only hardlink control preserved
the victim. Ordinary single-link digests retain intended remove/store behavior.

This is deliberately one consistent mutable-digest policy, not a special-case
basename fix. Project-external build/destination paths and lexical traversal
are refused. No hostile concurrent rename/mount or crash/I/O rollback guarantee
is made. Source/input symlinks elsewhere are outside this digest-output change.

Bounded equivalent-call audit

Exactly seven current frontend calls were inventoried: node_modules check,
path lookup, remove and store; index_html check, remove and store. Both pairs
use the repaired helpers and all four leaves are in the entry plan. The changed-
path writer scan binds the two runtime source hashes, actual AST sites and
pair inventory. No repository/history-wide writer scan was repeated.

Tests and reuse

software/tests/tesla_vehicle/test_r12_frontend_digest.py executes exact source
AST statements for the entry plan and dependency/index_html subprocedures,
plus actual get/check/remove/store and metadata-helper definitions. It asserts
victim existence/bytes, complete fixture preservation, no earlier unlink/write,
paired late-leaf rejection, ancestor/type/traversal checks, captured identity
substitution, hardlink controls and ordinary controls. Execution stops after
real digest removal, before package processes; intervening main statements are
NOT represented as executed. Store tests bound third-party generated-file
registration/writer calls with scratch-only observations, not the full frontend.

Fresh verification includes the R12 focused tests, unchanged R11 focused suite,
both web action/integration suites, changed-path AST/writer checks, shell syntax,
whitespace checks, packet verification and exact private-index validation.
Counts and exits are in software-evidence-summary.json; counts overlap.

R7-R11 recorded packaging/publication, host/sanitizer, IRAM, safety/binding,
F2-F4 and retained packaging results are reused through immutable R11 log/result
hashes and unchanged source/test hashes. They are NOT claimed as rerun. No
firmware/SDK rebuild or fresh source-to-firmware association is claimed.
Normal packaging remains expected exit 2 (SDK 0x8000 vs WARP4 0xe000).
Diagnostic bytes remain NON_INSTALLABLE with no production Tesla task; retained
normal ELF contains that task. The retained packaging result is hash-bound,
not revalidated against newly compiled R12 bytes.

Artifacts and mechanical readback

    review/r12/software-evidence-summary.json
    review/r12/delta-review-packet.json
    review/r12/publication-allowlist.json
    review/r12/evidence-receipt.json
    review/r12/verify_delta_packet.py
    review/r12/verify_publication.py

    software/.pio/production-ui-evidence-lifecycle-r12/manifest.json
    software/.pio/lifecycle-r12-work/post-seal-verification.json
    software/.pio/lifecycle-r12-work/commands.jsonl

Local evidence is a new read-only delta seal: two baseline/runtime sources,
new R12 source/control snapshots and new local scripts/logs/results. Prior
bundles/reviewer trees stay in place and are referenced by exact inventories.
No prior reports, controls, seals or verdicts are regenerated or rewritten.
Every hardware-evidence name is excluded before inventory descent; its contents
are never inspected. R6-R11 seal inventories and R7-R11 reviewer inventories
remain hash/mode/path/link/size identical. Protected current historical controls
are checked against the captured R11 baseline, not reinterpreted.

Publication boundary

    Sole clean-snapshot parent plan:
    a114447c4582456d123a0832eaa2d3ce5dd29f88

The new util.py modification is explicitly included in the exact proposed text
delta; it is no longer merely unchanged upstream content. Only the exact
allowlist enters a new private index/object store. The hash-pinned unchanged
R9 verifier checks complete delta and indexed blobs/modes. Final detached
receipt/index readback follows sealing; pre-seal metadata is not substituted
for the final proposal. No candidate commit is created. Any eventual separately
authorized candidate still requires exact raw sole-parent/full-tree checks.
Binaries, private/raw evidence, credentials, hardware-evidence and build/SDK/blob
trees remain excluded. Upstream development PEM fixtures outside the delta are
not a literal PEM-free-tree or licensing certification.

The complete local check is run inside the R12 offline wrapper:

    unshare --user --map-root-user --mount --net --fork bash software/.pio/lifecycle-r12-work/isolated.sh python3 -B software/.pio/lifecycle-r12-work/finalize_r12.py verify

Never invoke mount wrappers directly in the host namespace. The readback uses
only the private index/object store; the real staged delta stays empty and its
index bytes, entries, HEAD and branch remain unchanged.

Process qualifications and retained limits

A tiny read-only delegated inspection was restricted to these two source files.
It ran gpt-6.1-sol/openai-codex; the delegation API offers no worker-profile
selector and no provider fallback occurred. The file-edit tool's older Python
syntax checker reported an existing nested f-string after util.py line shifts.
Actual-runtime AST parsing of both baseline/current files succeeds. This is
recorded, not a runtime syntax failure or a reason to rewrite unrelated code.
One packet harness attempt mistakenly hashed its own actively opened output
log, so it failed on a self-reference rather than runtime source behavior. The
failed log/ledger are retained. That self-dependency was removed; final packet,
receipt and readback bindings remain detached and mechanically verified.

Historical R9 index/cache and possible early hardware-metadata exceptions and
R10 observation/early-RED qualifications remain disclosed; matching current
hashes do not erase them. R11 intermediate-control failures and private-index
cache-byte qualification also remain historical. Gate namespace/interface/route
observations are entry observations, not continuous packet auditing.

All prior target/hardware/runtime limits persist: no hardware/resource/RF/vehicle,
power-loss, cryptographic, licensing, upstream or release qualification; no
FreeRTOS/Xtensa concurrency/race proof, host mbedTLS sanitizer instrumentation
or complete stack bounds. B/C and two remaining A cold boots remain NO-GO.
No hardware/serial/LAN/Proxmox/Tesla/Vaultwarden/GitHub/external services, flash,
real-index staging, commit, push, publication or release occurred.

Ready for independent bounded Astra R12 delta review only, not approval.
