"""Deterministic real ptrace counterexample, not a secure tracer implementation."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class CaptureMechanismTests(unittest.TestCase):
    def test_real_ptrace_exposes_fd_identity_and_post_bind_writer_race(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); exe=root/'ptrace-probe'
            source=Path(__file__).with_name('capture_mechanism_probe.c')
            self.assertTrue(source.exists(),'real tracee/tracer mechanism experiment missing')
            subprocess.run(['/usr/bin/cc','-Wall','-Wextra','-Werror','-O2',str(source),'-o',str(exe)],
                           capture_output=True,check=True)
            result=subprocess.run([str(exe),str(root)],capture_output=True,text=True,timeout=15,check=True)
            evidence=json.loads(result.stdout)
            self.assertEqual(evidence['bound_fd_bytes'],'ORIGINAL')
            self.assertEqual(evidence['replacement_path_bytes'],'PATHFAKE')
            self.assertEqual(evidence['consumed_bytes'],'MUTATED!')
            self.assertTrue(evidence['fd_identity_preserved_after_rename'])
            self.assertTrue(evidence['consumer_stopped_during_writer'])
            destination=os.environ.get('WARP4_TEST_EVIDENCE_DIR')
            if destination:
                out=Path(destination); out.mkdir(parents=True,exist_ok=True)
                (out/'capture-mechanism.json').write_text(json.dumps(evidence,indent=2))


    def test_mapped_consumption_needs_more_than_syscall_open_hashes(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); exe=root/'ptrace-probe'
            source=Path(__file__).with_name('capture_mechanism_probe.c')
            subprocess.run(['/usr/bin/cc','-Wall','-Wextra','-Werror','-O2',str(source),'-o',str(exe)],
                           capture_output=True,check=True)
            result=subprocess.run([str(exe),str(root),'mmap'],capture_output=True,text=True,timeout=15,check=True)
            evidence=json.loads(result.stdout)
            self.assertEqual(evidence['consumption'],'userspace-load-after-mmap-exit')
            self.assertEqual(evidence['bound_fd_bytes'],'ORIGINAL')
            self.assertEqual(evidence['consumed_bytes'],'MUTATED!')
            destination=os.environ.get('WARP4_TEST_EVIDENCE_DIR')
            if destination:
                out=Path(destination); out.mkdir(parents=True,exist_ok=True)
                (out/'capture-mmap-mechanism.json').write_text(json.dumps(evidence,indent=2))


if __name__=='__main__': unittest.main()
