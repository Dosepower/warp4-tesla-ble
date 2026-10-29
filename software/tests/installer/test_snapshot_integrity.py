"""Snapshot object mutation regressions; no firmware integration claim."""
import copy
import hashlib
import os
from pathlib import Path
import tempfile
import unittest

import installer_snapshot as s


class SnapshotIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / 'input'
        self.path.write_bytes(b'approved')
        self.inputs = {'/snapshot/source': {'path': str(self.path),
                       'sha256': hashlib.sha256(b'approved').hexdigest(),
                       'mode': 0o444, 'role': 'source'}}

    def test_manifest_mutation_cannot_change_mount_permissions_or_role(self):
        for field, value in [('mode', 0o555), ('role', 'loader_runtime'), ('size', 1)]:
            with self.subTest(field=field), s.Snapshot(self.inputs) as snap:
                snap.manifest['files']['/snapshot/source'][field] = value
                with self.assertRaisesRegex(ValueError, 'SNAPSHOT_'):
                    snap.mounts()

    def test_missing_file_cannot_be_silently_omitted(self):
        with s.Snapshot(self.inputs) as snap:
            fd = snap.files.pop('/snapshot/source')
            try:
                with self.assertRaisesRegex(ValueError, 'SNAPSHOT_'):
                    snap.mounts()
            finally:
                os.close(fd)

    def test_context_cannot_publish_mutated_manifest(self):
        with s.Snapshot(self.inputs) as snap:
            snap.manifest['files']['/snapshot/source']['role'] = 'tool'
            with self.assertRaisesRegex(ValueError, 'SNAPSHOT_'):
                fd = snap.context({'environment': 'warp4_ble_installer'})
                os.close(fd)

    def test_same_bytes_in_unsealed_replacement_fd_rejected(self):
        with s.Snapshot(self.inputs) as snap:
            old = snap.files['/snapshot/source']
            fd = os.memfd_create('unsealed-replacement')
            os.write(fd, b'approved')
            snap.files['/snapshot/source'] = fd
            os.close(old)
            with self.assertRaisesRegex(ValueError, 'SNAPSHOT_'):
                snap.mounts()

    def test_workspace_alias_cannot_substitute_other_sealed_bytes(self):
        with s.Snapshot(self.inputs) as snap:
            old=snap.alias_fd("/snapshot/source")
            snap.aliases["/snapshot/source"]=s.sealed(b"other bytes")
            os.close(old)
            with self.assertRaisesRegex(ValueError,"SNAPSHOT_ALIAS"):
                snap.alias_fd("/snapshot/source")

    def test_bootstrap_mode_is_not_just_an_unchecked_label(self):
        self.path.chmod(0o644)
        spec=self.inputs["/snapshot/source"] | {"mode":0o555,"role":"python"}
        with self.assertRaisesRegex(ValueError,"BOOTSTRAP_MODE"):
            s.verify_bootstrap({"python":spec},{"python":str(self.path)})

    def test_full_assembly_can_exceed_inherited_soft_fd_limit_without_global_change(self):
        import subprocess,sys
        probe = r'''import hashlib,resource,tempfile
from pathlib import Path
from installer_snapshot import Snapshot
with tempfile.TemporaryDirectory() as td:
 p=Path(td)/"bytes"; p.write_bytes(b"scalable fixture")
 inputs={"/snapshot/file"+str(i):{"path":str(p),"sha256":hashlib.sha256(p.read_bytes()).hexdigest(),"mode":0o444,"role":"source"} for i in range(128)}
 soft,hard=resource.getrlimit(resource.RLIMIT_NOFILE)
 resource.setrlimit(resource.RLIMIT_NOFILE,(64,hard))
 with Snapshot(inputs) as snap:
  assert len(snap.files)==128
  snap.mounts()
 assert resource.getrlimit(resource.RLIMIT_NOFILE)==(64,hard)
print("isolated FD capacity and restoration passed")
'''
        result=subprocess.run([sys.executable,"-c",probe],text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_context_invalid_run_type_rejected(self):
        manifest = {'schema': 'warp4-sealed-closure-v1', 'files': {}}
        for run in (None, [], 'run'):
            fd = s.sealed(s.canonical({'schema': 'warp4-sealed-run-v1',
                'manifest': manifest, 'snapshot_sha256': hashlib.sha256(s.canonical(manifest)).hexdigest(),
                'run': run}))
            try:
                with self.subTest(run=run), self.assertRaisesRegex(ValueError, 'CONTEXT_'):
                    s.read_context(fd)
            finally:
                os.close(fd)


if __name__ == '__main__':
    unittest.main()
