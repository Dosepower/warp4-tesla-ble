#!/usr/bin/env python3
"""Proposed-index boundary tests; private alternate index/objects, no commits."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
VERIFIER = ROOT / 'review/r8/verify_publication.py'
BASE = 'a114447c4582456d123a0832eaa2d3ce5dd29f88'


class PublicationIndexTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        objects = self.root / 'objects'
        objects.mkdir()
        gitdir = subprocess.check_output(['git', 'rev-parse', '--absolute-git-dir'], cwd=ROOT, text=True).strip()
        self.env = dict(os.environ, GIT_INDEX_FILE=str(self.root / 'index'),
                        GIT_OBJECT_DIRECTORY=str(objects),
                        GIT_ALTERNATE_OBJECT_DIRECTORIES=str(Path(gitdir) / 'objects'))
        self.git('read-tree', BASE)
        self.name = 'review/r8/publication-fixture.txt'
        payload = b'source-only fixture\n'
        self.stage(self.name, payload)
        self.allowlist = self.root / 'allowlist.json'
        self.allowlist.write_text(json.dumps({'schema': 2, 'base_revision': BASE,
            'source_test_docs': [{'path': self.name, 'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest(), 'mode': '100644'}],
            'generated_evidence': [], 'control_files': []}))

    def git(self, *args, input=None):
        return subprocess.check_output(['git', *args], cwd=ROOT, env=self.env, input=input)

    def stage(self, name, payload, mode='100644'):
        oid = self.git('hash-object', '-w', '--stdin', input=payload).decode().strip()
        self.git('update-index', '--add', '--cacheinfo', f'{mode},{oid},{name}')

    def verify(self):
        return subprocess.run([sys.executable, '-B', str(VERIFIER), '--repo', str(ROOT),
            '--allowlist', str(self.allowlist), '--index', self.env['GIT_INDEX_FILE']],
            cwd=ROOT, env=self.env, text=True, capture_output=True)

    def test_exact_proposed_index_control(self):
        result = self.verify()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout)['exact_proposed_delta_verified'])

    def test_extra_staged_text_rejected(self):
        self.stage('review/r8/extra.txt', b'unlisted\n')
        self.assertNotEqual(self.verify().returncode, 0)

    def test_extra_staged_binary_rejected(self):
        self.stage('review/r8/extra.bin', b'\0firmware')
        self.assertNotEqual(self.verify().returncode, 0)

    def test_missing_expected_delta_rejected(self):
        self.git('update-index', '--force-remove', self.name)
        self.assertNotEqual(self.verify().returncode, 0)

    def test_indexed_blob_not_worktree_hash_is_checked(self):
        self.stage(self.name, b'tampered staged bytes\n')
        self.assertNotEqual(self.verify().returncode, 0)

    def test_indexed_symlink_mode_rejected(self):
        self.stage(self.name, b'source-only fixture\n', '120000')
        self.assertNotEqual(self.verify().returncode, 0)

    def test_indexed_executable_bit_change_rejected(self):
        self.stage(self.name, b'source-only fixture\n', '100755')
        self.assertNotEqual(self.verify().returncode, 0)

    def test_unlisted_deletion_rejected(self):
        names = self.git('ls-tree', '-r', '--name-only', BASE).decode().splitlines()
        self.git('update-index', '--force-remove', names[0])
        self.assertNotEqual(self.verify().returncode, 0)


if __name__ == '__main__':
    unittest.main()
