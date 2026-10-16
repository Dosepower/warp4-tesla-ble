#!/usr/bin/env python3
"""Actual rebuild-driver alias regressions; scratch only, no SDK/build executed."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
NAMES = ('ordinary', 'ordinary.unchecked', 'minimal', 'minimal.unchecked')


class DriverConfinementTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.project = self.root / 'project'
        self.tools = self.project / 'software/tools'
        self.tools.mkdir(parents=True)
        for name in ('rebuild_warp4_bench.sh', 'bench_confinement.py'):
            source = ROOT / 'software/tools' / name
            if source.exists():
                shutil.copyfile(source, self.tools / name)
        self.pio = self.project / 'software/.pio'
        self.outside = self.root / 'unrelated'
        self.outside.mkdir()
        self.audit = self.root / 'deletions.jsonl'
        # Observe real child-process rmtree calls, without changing behavior.
        site = self.root / 'python-observer'
        site.mkdir()
        (site / 'sitecustomize.py').write_text(
            'import os,json,shutil\n'
            'original=shutil.rmtree\n'
            'def observed(path,*a,**kw):\n'
            ' with open(os.environ["R9_DELETE_AUDIT"],"a") as f: f.write(json.dumps(str(path))+"\\n")\n'
            ' return original(path,*a,**kw)\n'
            'shutil.rmtree=observed\n')
        self.env = dict(os.environ, PYTHONPATH=str(site), PYTHONDONTWRITEBYTECODE='1',
                        R9_DELETE_AUDIT=str(self.audit))
        self.sentinels = []

    def sentinel(self, directory):
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / 'MUST_SURVIVE'
        path.write_text('unrelated scratch sentinel\n')
        self.sentinels.append(path)

    def exports(self, parent):
        for name in NAMES:
            self.sentinel(parent / name)

    def run_driver(self):
        # Missing export.sh ensures even the valid control stops before builds.
        self.assertFalse((self.pio / 'bench-idf/export.sh').exists())
        return subprocess.run(['bash', str(self.tools / 'rebuild_warp4_bench.sh')],
                              cwd=self.project, env=self.env, text=True, capture_output=True)

    def assert_refused_without_mutation(self, result):
        self.assertNotEqual(result.returncode, 0)
        self.assertRegex(result.stderr, 'symlink|confinement|redirect|substitut')
        self.assertFalse(self.audit.exists(), self.audit.read_text() if self.audit.exists() else '')
        for path in self.sentinels:
            self.assertEqual(path.read_text(), 'unrelated scratch sentinel\n')
        self.assertFalse((self.pio / 'bench-evidence').exists(), 'preflight created evidence/log path')

    def test_actual_driver_pio_alias_preserves_all_four_exports(self):
        self.pio.symlink_to(self.outside, target_is_directory=True)
        self.exports(self.outside / 'bench-exports')
        self.assert_refused_without_mutation(self.run_driver())

    def test_actual_driver_exports_alias_preserves_all_four_exports(self):
        self.pio.mkdir()
        (self.pio / 'bench-exports').symlink_to(self.outside, target_is_directory=True)
        self.exports(self.outside)
        self.assert_refused_without_mutation(self.run_driver())

    def test_actual_driver_late_minimal_stage_alias_preserves_earlier_targets(self):
        parent = self.pio / 'bench-exports'
        parent.mkdir(parents=True)
        (parent / 'minimal.unchecked').symlink_to(self.outside, target_is_directory=True)
        self.exports(parent)
        self.assert_refused_without_mutation(self.run_driver())

    def sdk_alias(self, variant='diag', ancestor=False, child=None):
        if ancestor:
            self.pio.symlink_to(self.outside, target_is_directory=True)
            sdk = self.outside / ('warp4-bench-' + variant + '-sdk')
        else:
            self.pio.mkdir(exist_ok=True)
            sdk = self.pio / ('warp4-bench-' + variant + '-sdk')
            if child:
                sdk.mkdir()
                (sdk / child).symlink_to(self.outside, target_is_directory=True)
            else:
                sdk.symlink_to(self.outside, target_is_directory=True)
        for name in ('build', 'out'):
            self.sentinel(sdk / name)
        self.exports(self.pio / 'bench-exports')
        return sdk

    def test_actual_driver_sdk_root_alias_preserves_build_out_and_exports(self):
        self.sdk_alias()
        self.assert_refused_without_mutation(self.run_driver())

    def test_actual_driver_sdk_ancestor_alias_preserves_build_out_and_exports(self):
        self.sdk_alias(ancestor=True)
        self.assert_refused_without_mutation(self.run_driver())

    def test_actual_driver_late_minimal_sdk_out_alias_rejects_before_first_mutation(self):
        self.sdk_alias('minimal', child='out')
        self.assert_refused_without_mutation(self.run_driver())

    def test_actual_driver_evidence_alias_is_refused_before_cleanup(self):
        self.exports(self.pio / 'bench-exports')
        (self.pio / 'bench-evidence').symlink_to(self.outside, target_is_directory=True)
        result = self.run_driver()
        self.assertNotEqual(result.returncode, 0)
        self.assertRegex(result.stderr, 'symlink|confinement|redirect')
        self.assertFalse(self.audit.exists())
        self.assertEqual(list(self.outside.iterdir()), [])
        for p in self.sentinels:
            self.assertTrue(p.exists())

    def test_actual_driver_existing_log_alias_not_truncated(self):
        self.exports(self.pio / 'bench-exports')
        evidence = self.pio / 'bench-evidence/astra-fixes'
        evidence.mkdir(parents=True)
        target = self.outside / 'unrelated.txt'
        target.write_text('must not truncate')
        (evidence / 'minimal-sdk-finalize.log').symlink_to(target)
        result = self.run_driver()
        self.assertNotEqual(result.returncode, 0)
        self.assertRegex(result.stderr, 'symlink|confinement|redirect')
        self.assertFalse(self.audit.exists())
        self.assertEqual(target.read_text(), 'must not truncate')
        self.assertEqual(len(list(evidence.iterdir())), 1)
        self.assertTrue(all(p.exists() for p in self.sentinels))

    def test_actual_driver_regular_paths_control_revokes_exports_then_stops_before_build(self):
        self.exports(self.pio / 'bench-exports')
        result = self.run_driver()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(p.exists() for p in self.sentinels))
        calls = [json.loads(line) for line in self.audit.read_text().splitlines()]
        self.assertEqual(calls, [str(self.pio / 'bench-exports' / name) for name in NAMES])
        self.assertIn('export.sh', (self.pio / 'bench-evidence/astra-fixes/idf-export.log').read_text())

    def module(self):
        spec = importlib.util.spec_from_file_location('r9_confinement', ROOT / 'software/tools/bench_confinement.py')
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def sdk_cleanup(self, sdk):
        # Invoke the actual helper called by the shell, not a test cleanup model.
        return subprocess.run([sys.executable, str(self.tools / 'bench_confinement.py'),
                               'sdk-clean', str(self.project / 'software'), str(sdk)],
                              cwd=self.project, env=self.env, text=True, capture_output=True)

    def test_sdk_cleanup_root_alias(self):
        sdk = self.sdk_alias()
        self.assert_refused_without_mutation(self.sdk_cleanup(sdk))

    def test_sdk_cleanup_ancestor_alias(self):
        sdk = self.sdk_alias(ancestor=True)
        # Keep the lexical project path, not the unrelated target path.
        sdk = self.pio / 'warp4-bench-diag-sdk'
        self.assert_refused_without_mutation(self.sdk_cleanup(sdk))

    def test_sdk_cleanup_out_alias_preserves_build(self):
        sdk = self.sdk_alias(child='out')
        self.assert_refused_without_mutation(self.sdk_cleanup(sdk))

    def test_sdk_cleanup_wrong_root_same_basename_rejected(self):
        sdk = self.outside / 'warp4-bench-diag-sdk'
        for name in ('build', 'out'):
            self.sentinel(sdk / name)
        result = self.sdk_cleanup(sdk)
        self.assert_refused_without_mutation(result)

    def test_sdk_cleanup_lexical_traversal_rejected(self):
        self.pio.mkdir()
        sdk = str(self.pio) + '/../.pio/warp4-bench-diag-sdk'
        result = self.sdk_cleanup(sdk)
        self.assert_refused_without_mutation(result)

    def test_sdk_cleanup_regular_control(self):
        sdk = self.pio / 'warp4-bench-diag-sdk'
        for name in ('build', 'out'):
            self.sentinel(sdk / name)
        result = self.sdk_cleanup(sdk)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(sdk.is_dir())
        self.assertFalse(any(p.exists() for p in self.sentinels))
        self.assertEqual(len(self.audit.read_text().splitlines()), 2)

    def test_preflight_detects_target_substitution_before_any_delete(self):
        module = self.module()
        a, b = self.outside / 'a', self.outside / 'b'
        a.mkdir(); b.mkdir()
        plan = module.preflight(directories=[a, b])
        b.rename(self.outside / 'old-b')
        b.mkdir()
        with patch('shutil.rmtree') as deletion:
            with self.assertRaisesRegex(ValueError, 'substitut|changed'):
                module.remove_directories(plan, [a, b])
            deletion.assert_not_called()
        self.assertTrue(a.exists())


if __name__ == '__main__':
    unittest.main()
