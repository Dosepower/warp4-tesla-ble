#!/usr/bin/env python3
"""Read-only exact proposed-index boundary and clean-snapshot ancestry checks.

No real-index writes, object writes, commits or network. A proposed parent is a
reviewed plan, NOT an existing commit's ancestry proof. If a candidate already
exists, --candidate-commit additionally verifies its sole parent and full tree.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys

BASE = 'a114447c4582456d123a0832eaa2d3ce5dd29f88'
PROHIBITED_PARTS = {'.pio', 'build', 'build_latest', 'hardware-evidence', '__pycache__'}
PROHIBITED_SUFFIXES = {'.bin', '.elf', '.map', '.a', '.o', '.zip', '.tar', '.gz', '.pcap', '.log', '.pyc'}


def safe_path(name):
    path = PurePosixPath(name)
    if (not name or str(path) != name or path.is_absolute() or '..' in path.parts
            or any(p in PROHIBITED_PARTS or p.startswith(('.env', 'default_wifi')) or p == 'warp4_wifi.json' for p in path.parts)
            or path.suffix in PROHIBITED_SUFFIXES):
        raise ValueError('prohibited publication path: ' + name)
    return name


def verify(repo, allowlist, index, proposed_parent, candidate=None):
    data = json.loads(allowlist.read_text())
    if data['schema'] != 3 or data['base_revision'] != BASE:
        raise ValueError('unrecognized allowlist schema/base')
    if (data['publication_strategy'] != 'clean-snapshot' or data['snapshot_parent'] != BASE
            or proposed_parent != BASE):
        raise ValueError('clean snapshot parent must be the exact pinned upstream base, not local history')
    rows = data['source_test_docs'] + data['generated_evidence']
    expected = {safe_path(row['path']): row for row in rows}
    controls = [safe_path(name) for name in data['control_files']]
    if len(expected) != len(rows) or len(set(controls)) != len(controls) or set(expected) & set(controls):
        raise ValueError('duplicate allowlist paths')
    allowed = set(expected) | set(controls)
    env = dict(os.environ, GIT_OPTIONAL_LOCKS='0', GIT_INDEX_FILE=str(index.resolve()),
               GIT_NO_REPLACE_OBJECTS='1')

    def git(*args):
        return subprocess.check_output(['git', '-C', str(repo), *args], env=env)

    if git('cat-file', '-t', BASE).strip() != b'commit':
        raise ValueError('pinned snapshot parent is not an available commit')
    # Entire delta, with no pathspec or rename heuristics: unexpected additions,
    # deletions, symlinks, modes, conflicts and indexed-vs-worktree substitution
    # must not be hidden by a list of correct file hashes.
    changed = git('diff', '--cached', '--no-renames', '--name-only', '-z', BASE, '--').split(b'\0')
    actual = {name.decode('utf-8') for name in changed if name}
    if actual != allowed:
        raise ValueError('proposed Git delta differs from allowlist: ' + json.dumps({
            'extra': sorted(actual - allowed), 'missing': sorted(allowed - actual)}))
    entries = {}
    for record in git('ls-files', '--stage', '-z').split(b'\0'):
        if record:
            metadata, name = record.split(b'\t', 1)
            mode, oid, stage = metadata.decode().split()
            name = name.decode('utf-8')
            if stage != '0' or name in entries:
                raise ValueError('unmerged/duplicate proposed index entry: ' + name)
            entries[name] = (mode, oid)
    for name in sorted(allowed):
        if name not in entries or entries[name][0] not in ('100644', '100755'):
            raise ValueError('missing/nonregular indexed file: ' + name)
        blob = git('cat-file', 'blob', entries[name][1])
        blob.decode('utf-8')
        if b'\0' in blob:
            raise ValueError('binary indexed payload: ' + name)
        if name in expected:
            row = expected[name]
            if entries[name][0] != row['mode'] or len(blob) != row['bytes'] or hashlib.sha256(blob).hexdigest() != row['sha256']:
                raise ValueError('indexed blob differs from allowlist: ' + name)
        else:
            target = repo / name
            mode = '100755' if target.stat().st_mode & 0o111 else '100644'
            if entries[name][0] != mode or any(p.is_symlink() for p in [target, *target.parents]) or blob != target.read_bytes():
                raise ValueError('indexed control differs from reviewed control: ' + name)
    ancestry = {'proposed_sole_parent': BASE, 'local_history_excluded_from_plan': True,
                'candidate_commit': None, 'actual_commit_ancestry_verified': False,
                'requirement': 'before publication recheck eventual candidate sole parent and full tree; no commit created here'}
    if candidate is not None:
        # Require an exact object ID, not a movable ref or option-shaped token.
        if len(candidate) != 40 or any(c not in '0123456789abcdef' for c in candidate):
            raise ValueError('candidate ancestry requires an exact lowercase commit ID')
        if git('cat-file', '-t', candidate).strip() != b'commit':
            raise ValueError('candidate ancestry requires a commit')
        # Read the actual commit header, not graft/replace-influenced revision
        # traversal output. Never manufacture a commit to test this gate.
        header = git('cat-file', '-p', candidate).split(b'\n\n', 1)[0]
        parents = [line.removeprefix(b'parent ').decode() for line in header.splitlines()
                   if line.startswith(b'parent ')]
        if parents != [BASE]:
            raise ValueError('candidate ancestry is not a clean snapshot: sole parent differs from pinned base')
        tree = {}
        for record in git('ls-tree', '-r', '-z', candidate).split(b'\0'):
            if record:
                meta, name = record.split(b'\t', 1)
                mode, kind, oid = meta.decode().split()
                tree[name.decode()] = (mode, oid)
        if tree != entries:
            raise ValueError('candidate full tree differs from exact proposed index')
        ancestry.update(candidate_commit=candidate, actual_commit_ancestry_verified=True)
    return {'exact_proposed_delta_verified': True, 'indexed_text_files': len(allowed),
            'base_revision': BASE, 'index': str(index.resolve()), 'ancestry': ancestry,
            'allowlist_sha256': hashlib.sha256(allowlist.read_bytes()).hexdigest(),
            'scope': 'source-only boundary/ancestry-plan verification; independent review and publication approval still required'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--allowlist', type=Path, default=Path(__file__).with_name('publication-allowlist.json'))
    parser.add_argument('--index', type=Path, required=True)
    parser.add_argument('--proposed-parent', required=True, help='exact sole upstream parent of planned clean snapshot')
    parser.add_argument('--candidate-commit', help='optional existing exact candidate commit ID; never created here')
    args = parser.parse_args()
    try:
        if not args.index.is_file() or args.index.is_symlink():
            raise ValueError('existing regular proposed index required')
        print(json.dumps(verify(args.repo, args.allowlist, args.index, args.proposed_parent, args.candidate_commit), indent=2))
    except (OSError, ValueError, KeyError, UnicodeError, subprocess.CalledProcessError) as exc:
        print('publication verification failed: ' + str(exc), file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
