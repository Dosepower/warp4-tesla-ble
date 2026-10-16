#!/usr/bin/env python3
"""Verify the EXACT proposed source-only Git index delta, not worktree hashes.

Read-only: does not stage, write Git objects, commit, publish or use a network.
Use --index for a private proposed index; never assume the live index is clean.
The allowlist and this verifier are trusted review inputs, not self-approval.
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


def verify(repo, allowlist, index):
    data = json.loads(allowlist.read_text())
    if data['schema'] != 2 or data['base_revision'] != BASE:
        raise ValueError('unrecognized allowlist schema/base')
    rows = data['source_test_docs'] + data['generated_evidence']
    expected = {safe_path(row['path']): row for row in rows}
    controls = [safe_path(name) for name in data['control_files']]
    if len(expected) != len(rows) or len(set(controls)) != len(controls) or set(expected) & set(controls):
        raise ValueError('duplicate allowlist paths')
    allowed = set(expected) | set(controls)
    env = dict(os.environ, GIT_OPTIONAL_LOCKS='0', GIT_INDEX_FILE=str(index.resolve()))

    def git(*args):
        return subprocess.check_output(['git', '-C', str(repo), *args], env=env)

    # NUL-delimited, no rename heuristics, no pathspec: compare the entire
    # proposed delta (including unexpected deletions) to this exact boundary.
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
            if name in allowed:
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
            # Detached controls avoid circular hashes. They must still match
            # the EXACT locally reviewed regular files, not arbitrary blobs.
            target = repo / name
            mode = '100755' if target.stat().st_mode & 0o111 else '100644'
            if entries[name][0] != mode or any(p.is_symlink() for p in [target, *target.parents]) or blob != target.read_bytes():
                raise ValueError('indexed control differs from reviewed control: ' + name)
    return {'exact_proposed_delta_verified': True, 'indexed_text_files': len(allowed),
            'base_revision': BASE, 'index': str(index.resolve()),
            'allowlist_sha256': hashlib.sha256(allowlist.read_bytes()).hexdigest(),
            'scope': 'source-only boundary verification; independent review/publication approval still required'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--allowlist', type=Path, default=Path(__file__).with_name('publication-allowlist.json'))
    parser.add_argument('--index', type=Path, required=True, help='exact proposed Git index to inspect read-only')
    args = parser.parse_args()
    try:
        if not args.index.is_file() or args.index.is_symlink():
            raise ValueError('existing regular proposed index required')
        print(json.dumps(verify(args.repo, args.allowlist, args.index), indent=2))
    except (OSError, ValueError, KeyError, UnicodeError, subprocess.CalledProcessError) as exc:
        print('publication verification failed: ' + str(exc), file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
