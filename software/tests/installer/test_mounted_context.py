"""Mounted-context reader boundaries; writable host data is never approval."""
import hashlib
import os
from pathlib import Path
import tempfile
import unittest

import installer_snapshot as snapshot


class MountedContextTests(unittest.TestCase):
    def test_build_context_requires_complete_run_and_exact_guard_identity(self):
        from unittest.mock import patch
        self.assertTrue(hasattr(snapshot, "read_build_context"), "hook context API missing")
        verified = {"environment": "warp4_ble_installer", "build_id": "a" * 32,
                    "start_ns": 1, "package_roots": {}, "effective_flags": {}}
        value = {"run": {"environment": "warp4_ble_installer", "verified": verified},
                 "snapshot_sha256": "b" * 64}
        with patch.object(snapshot, "read_mounted_context", return_value=value) as read:
            result = snapshot.read_build_context("warp4_ble_installer")
            read.assert_called_once_with("/run/verified-context.json", "warp4_ble_installer")
            self.assertEqual(result["snapshot_sha256"], "b" * 64)
            with self.assertRaisesRegex(ValueError, "GUARD_CONTEXT_MISMATCH"):
                snapshot.read_build_context("warp4_ble_installer", guarded=result | {"build_id": "c" * 32})
            value["run"]["verified"]["start_ns"] = True
            with self.assertRaisesRegex(ValueError, "BUILD_CONTEXT_SCHEMA"):
                snapshot.read_build_context("warp4_ble_installer")

    def test_namespace_output_limit_is_enforced_during_capture(self):
        import sys
        import installer_namespace
        with self.assertRaisesRegex(ValueError,"NAMESPACE_OUTPUT_LIMIT"):
            installer_namespace.spawn([sys.executable,"-I","-c","print('x'*8192)"],(),Path.cwd(),stdout_limit=1024)

    def test_writable_context_file_rejected_before_manifest_reads(self):
        self.assertTrue(hasattr(snapshot, 'read_mounted_context'), 'readonly-mounted context reader missing')
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'context'
            manifest = {'schema': 'warp4-sealed-closure-v1', 'files': {}}
            path.write_bytes(snapshot.canonical({'schema': 'warp4-sealed-run-v1',
                'snapshot_sha256': hashlib.sha256(snapshot.canonical(manifest)).hexdigest(),
                'manifest': manifest, 'run': {'environment': 'warp4_ble_installer'}}))
            with self.assertRaisesRegex(ValueError, 'CONTEXT_MOUNT_NOT_READONLY'):
                snapshot.read_mounted_context(path, 'warp4_ble_installer')


if __name__ == '__main__':
    unittest.main()
