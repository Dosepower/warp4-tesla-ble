#!/usr/bin/env python3
"""R9 exact-index checks plus explicit clean snapshot parent; no commits."""
import json
import subprocess
import sys
import unittest
import test_r8_publication as r8


class R9PublicationIndexTests(r8.PublicationIndexTests):
    def setUp(self):
        super().setUp()
        self.parent = r8.BASE
        self.extra = []
        data = json.loads(self.allowlist.read_text())
        data.update(schema=3, snapshot_parent=r8.BASE, publication_strategy='clean-snapshot')
        self.allowlist.write_text(json.dumps(data))

    def verify(self):
        return subprocess.run([sys.executable, '-B', str(r8.ROOT / 'review/r9/verify_publication.py'),
            '--repo', str(r8.ROOT), '--allowlist', str(self.allowlist),
            '--index', self.env['GIT_INDEX_FILE'], '--proposed-parent', self.parent, *self.extra],
            cwd=r8.ROOT, env=self.env, text=True, capture_output=True)

    def test_local_branch_head_cannot_be_snapshot_parent(self):
        self.parent = self.git('rev-parse', 'HEAD').decode().strip()
        self.assertNotEqual(self.parent, r8.BASE)
        result = self.verify()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('parent', result.stderr)

    def test_missing_proposed_parent_is_rejected(self):
        self.parent = ''
        self.assertNotEqual(self.verify().returncode, 0)

    def test_allowlist_cannot_select_local_history_parent(self):
        data = json.loads(self.allowlist.read_text())
        data['snapshot_parent'] = self.git('rev-parse', 'HEAD').decode().strip()
        self.allowlist.write_text(json.dumps(data))
        self.assertNotEqual(self.verify().returncode, 0)

    def test_existing_local_commit_is_not_clean_snapshot_candidate(self):
        self.extra = ['--candidate-commit', self.git('rev-parse', 'HEAD').decode().strip()]
        result = self.verify()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ancestry', result.stderr)


if __name__ == '__main__':
    unittest.main()
