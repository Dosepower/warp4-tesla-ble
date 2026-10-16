#!/usr/bin/env python3
"""Read-only compact R11 delta packet verification; no index writes/network."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inventory(root):
    result = {}
    for directory, dirs, files in os.walk(root, followlinks=False):
        dirs[:] = sorted(n for n in dirs if n != 'hardware-evidence')
        for name in sorted(dirs + files):
            if name == 'hardware-evidence':
                continue
            path = Path(directory) / name
            info = path.lstat()
            row = {'mode': info.st_mode}
            if path.is_symlink():
                row['link'] = str(path.readlink())
            elif path.is_file():
                row.update(sha256=sha(path), bytes=info.st_size)
            result[str(path.relative_to(root))] = row
    return result


def inventory_digest(rows):
    return hashlib.sha256(json.dumps(rows, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def verify(packet_path):
    data = json.loads(packet_path.read_text())
    repo = Path(__file__).resolve().parents[2]
    assert sha(repo / 'review/r11/software-evidence-summary.json') == data['generated_summary_sha256']
    for name, checksum in data['R10_baseline_hashes'].items():
        assert sha(Path(data['R10_baseline_copy_root']) / name) == checksum, name
    for row in data['source_delta_hashes']:
        path = repo / row['path']
        assert not path.is_symlink() and path.stat().st_size == row['bytes'], row['path']
        assert sha(path) == row['sha256'], row['path']
    for row in data['protected_inventories']:
        root = Path(row['root'])
        rows = inventory(root)
        assert len(rows) == row['entries'] and inventory_digest(rows) == row['inventory_sha256'], row['root']
        if 'manifest_sha256' in row:
            assert sha(root / 'manifest.json') == row['manifest_sha256']
            assert not root.stat().st_mode & 0o222
            assert all(not v['mode'] & 0o222 and 'link' not in v for v in rows.values())
    env = dict(os.environ, GIT_OPTIONAL_LOCKS='0', GIT_NO_REPLACE_OBJECTS='1')
    git = lambda *args: subprocess.check_output(['git', '-C', str(repo), *args], env=env)
    assert sha(repo / '.git/index') == data['real_index']['sha256']
    assert not git('diff', '--cached', '--name-status', 'HEAD', '--').strip()
    assert hashlib.sha256(git('ls-files', '--stage', '-z')).hexdigest() == data['real_index']['entries_sha256']
    assert git('rev-parse', 'HEAD').decode().strip() == data['real_index']['head']
    for row in data['focused_evidence']:
        assert sha(Path(row['path'])) == row['sha256'], row['path']
    print(json.dumps({'packet_source_hashes': True, 'protected_inventories': True,
                      'real_staged_delta_empty': True, 'focused_evidence_hashes': True,
                      'qualification': 'run verify_publication.py separately for exact proposed index; no approval'}))


if __name__ == '__main__':
    verify(Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name('delta-review-packet.json'))
