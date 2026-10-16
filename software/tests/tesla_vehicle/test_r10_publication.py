#!/usr/bin/env python3
"""Run all retained boundary/parent regression cases through R10 inputs."""
import subprocess
import sys
import unittest
import test_r9_publication as r9
import test_r8_publication as r8


class R10PublicationIndexTests(r9.R9PublicationIndexTests):
    def verify(self):
        return subprocess.run([sys.executable, '-B', str(r8.ROOT / 'review/r10/verify_publication.py'),
            '--repo', str(r8.ROOT), '--allowlist', str(self.allowlist),
            '--index', self.env['GIT_INDEX_FILE'], '--proposed-parent', self.parent, *self.extra],
            cwd=r8.ROOT, env=self.env, text=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
