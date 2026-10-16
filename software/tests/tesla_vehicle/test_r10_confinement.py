#!/usr/bin/env python3
"""R10 hard-link and quarantine regressions; scratch-only actual entrypoints."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import test_r9_driver_confinement as r9

sys.path.insert(0, str(r9.ROOT / 'software/tools'))
import bench_build_identity as identity
import bench_confinement as confinement


def state(root):
    result = {}
    for directory, dirs, files in os.walk(root, followlinks=False):
        for name in dirs + files:
            p = Path(directory) / name
            s = p.lstat()
            result[str(p.relative_to(root))] = (s.st_mode, s.st_ino, s.st_nlink,
                str(p.readlink()) if p.is_symlink() else p.read_bytes() if p.is_file() else None)
    return result


class LogAliasTests(unittest.TestCase):
    def fixture(self):
        case = r9.DriverConfinementTests()
        case.setUp()
        self.addCleanup(case.doCleanups)
        case.exports(case.pio / 'bench-exports')
        for sdk in confinement.SDKS:
            for name in ('build', 'out'):
                case.sentinel(case.pio / sdk / name)
        evidence = case.pio / 'bench-evidence/astra-fixes'
        evidence.mkdir(parents=True)
        victim = case.outside / 'VICTIM'
        victim.write_bytes(b'UNRELATED VICTIM MUST NOT CHANGE\n')
        return case, evidence, victim

    def test_every_preexisting_hardlinked_log_rejects_before_any_mutation(self):
        # Initial idf-export.log and ALL fourteen later run_logged log paths.
        for name in confinement.LOGS:
            with self.subTest(log=name):
                case, evidence, victim = self.fixture()
                os.link(victim, evidence / name)
                before = state(case.root)
                result = case.run_driver()
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertRegex(result.stderr, 'hard.?link|shared.*inode')
                self.assertFalse(case.audit.exists(), 'rmtree occurred before rejection')
                self.assertEqual(state(case.root), before)
                self.assertEqual(victim.read_bytes(), b'UNRELATED VICTIM MUST NOT CHANGE\n')
                self.assertTrue(all(p.read_text() == 'unrelated scratch sentinel\n' for p in case.sentinels))
                self.assertEqual(len(case.sentinels), 8)

    def test_ordinary_single_link_logs_allow_intended_cleanup(self):
        case, evidence, victim = self.fixture()
        for name in confinement.LOGS:
            (evidence / name).write_bytes(b'old safe log\n')
        self.assertTrue(all((evidence / n).stat().st_nlink == 1 for n in confinement.LOGS))
        result = case.run_driver()
        self.assertNotEqual(result.returncode, 0)  # deliberately missing IDF
        self.assertEqual(len(case.audit.read_text().splitlines()), 4)
        self.assertFalse(any(p.exists() for p in case.sentinels[:4]))
        self.assertTrue(all(p.exists() for p in case.sentinels[4:]))
        self.assertEqual(victim.read_bytes(), b'UNRELATED VICTIM MUST NOT CHANGE\n')
        self.assertIn('export.sh', (evidence / 'idf-export.log').read_text())
        for name in confinement.LOGS[1:]:
            self.assertEqual((evidence / name).read_bytes(), b'old safe log\n')

    def test_link_added_after_preflight_rejected_before_delete(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            a = root / 'export'; a.mkdir()
            log = root / 'log'; log.write_bytes(b'unchanged')
            plan = confinement.preflight(directories=(a,), files=(log,))
            os.link(log, root / 'victim')
            with patch.object(confinement.shutil, 'rmtree') as delete:
                with self.assertRaisesRegex(ValueError, 'hard.?link|shared.*inode'):
                    confinement.remove_directories(plan, (a,))
                delete.assert_not_called()
            self.assertEqual(log.read_bytes(), b'unchanged')

    def test_safe_run_logged_control_reaches_sdk_clean_without_build(self):
        case, evidence, victim = self.fixture()
        for name in confinement.LOGS:
            (evidence / name).write_bytes(b'old safe log\n')
        idf = case.pio / 'bench-idf'; idf.mkdir()
        (idf / 'export.sh').write_text('return 0\n')
        tools = case.root / 'fake-bin'; tools.mkdir()
        command = tools / 'idf.py'
        command.write_text('#!/bin/sh\nprintf "LOCAL TEST STOP: no SDK build\\n"\nexit 86\n')
        command.chmod(0o755)
        env = dict(case.env, PATH=str(tools) + os.pathsep + case.env['PATH'])
        result = subprocess.run(['bash', str(case.tools / 'rebuild_warp4_bench.sh')],
                                cwd=case.project, env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 86, result.stderr)
        self.assertEqual(len(case.audit.read_text().splitlines()), 6)
        self.assertIn('SHELL_EXIT_CODE=0', (evidence / 'diag-sdk-clean.log').read_text())
        self.assertIn('LOCAL TEST STOP', (evidence / 'diag-sdk-configure.log').read_text())
        self.assertEqual(victim.read_bytes(), b'UNRELATED VICTIM MUST NOT CHANGE\n')
        self.assertTrue(all(p.exists() for p in case.sentinels[6:]))


class QuarantineTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.project = self.root / 'project'
        self.common = self.project / 'build'
        self.common.mkdir(parents=True)
        self.outside = self.root / 'outside'; self.outside.mkdir()
        (self.outside / 'UNRELATED').write_bytes(b'must survive')
        self.names = ['a-logged.bin', 'b-logged.bin']
        self.log = self.root / 'historical.log'
        self.log.write_text(''.join('Copying .pio/build/warp4_tesla_ble_iram_bench/firmware_esptool.bin -> build/' + n + '\n' for n in self.names))
        for name in self.names:
            (self.common / name).write_bytes(('old bench ' + name).encode())
        self.pio = self.project / '.pio'
        self.q = self.pio / 'bench-quarantine/legacy-shared-output'

    def invoke(self, project=None):
        return identity.quarantine_legacy(project or self.project, [self.log])

    def refused(self, project=None):
        before = state(self.root)
        with patch.object(Path, 'mkdir') as mkdir, patch.object(Path, 'rename') as move, patch.object(Path, 'unlink') as unlink:
            with self.assertRaises(ValueError):
                self.invoke(project)
            mkdir.assert_not_called(); move.assert_not_called(); unlink.assert_not_called()
        self.assertEqual(state(self.root), before)

    def test_pio_ancestor_symlink(self):
        self.pio.symlink_to(self.outside, target_is_directory=True)
        self.refused()

    def test_quarantine_ancestor_symlink(self):
        self.pio.mkdir(); (self.pio / 'bench-quarantine').symlink_to(self.outside, target_is_directory=True)
        self.refused()

    def test_dangling_pio_alias(self):
        self.pio.symlink_to(self.root / 'missing', target_is_directory=True)
        self.refused()

    def test_dangling_quarantine_ancestor_alias(self):
        self.pio.mkdir(); (self.pio / 'bench-quarantine').symlink_to(self.root / 'missing', target_is_directory=True)
        self.refused()

    def test_project_alias(self):
        alias = self.root / 'alias'; alias.symlink_to(self.project, target_is_directory=True)
        self.refused(alias)

    def test_lexical_traversal(self):
        self.refused(str(self.project) + '/../project')

    def test_wrong_ancestor_type(self):
        self.pio.write_bytes(b'not a directory')
        self.refused()

    def test_wrong_logged_source_type(self):
        p = self.common / self.names[1]; p.unlink(); p.mkdir()
        self.refused()

    def test_later_destination_conflict_no_partial_move(self):
        self.q.mkdir(parents=True)
        (self.q / ('REJECTED-UNVALIDATED-LEGACY-build-' + self.names[1])).write_bytes(b'preexisting')
        self.refused()

    def test_dangling_destination_conflict(self):
        self.q.mkdir(parents=True)
        (self.q / ('REJECTED-UNVALIDATED-LEGACY-build-' + self.names[1])).symlink_to(self.root / 'missing')
        self.refused()

    def test_quarantine_leaf_wrong_type(self):
        self.q.parent.mkdir(parents=True); self.q.write_bytes(b'not directory')
        self.refused()

    def test_quarantine_dangling_leaf_alias(self):
        self.q.parent.mkdir(parents=True); self.q.symlink_to(self.root / 'missing')
        self.refused()

    def test_hardlinked_logged_source_is_rejected_conservatively(self):
        os.link(self.common / self.names[1], self.outside / 'source-alias')
        self.refused()

    def test_unlink_alias_identity_substitution_before_first_move(self):
        alias = self.common / 'latest.bin'; alias.symlink_to(self.names[0])
        original = confinement.preflight
        hits = []
        def preflight(*args, **kwargs):
            plan = original(*args, **kwargs)
            if alias in kwargs.get('links', ()):
                hits.append(True)
                os.unlink(alias); os.symlink(self.names[1], alias)
            return plan
        with patch.object(confinement, 'preflight', side_effect=preflight), patch.object(Path, 'rename') as move, patch.object(Path, 'unlink') as unlink, patch.object(Path, 'mkdir') as mkdir:
            with self.assertRaises(ValueError):
                self.invoke()
            move.assert_not_called(); unlink.assert_not_called(); mkdir.assert_not_called()
        self.assertEqual(hits, [True])
        self.assertTrue(all((self.common / n).is_file() for n in self.names))

    def test_logged_parent_traversal_name(self):
        self.log.write_text('Copying .pio/build/warp4_tesla_ble_iram_bench/firmware.bin -> build/..\n')
        self.refused()

    def test_regular_control_preserves_unrelated_and_removes_only_logged_alias(self):
        unrelated = self.common / 'production.bin'; unrelated.write_bytes(b'production')
        alias = self.common / 'latest.bin'; alias.symlink_to(self.names[0])
        production_alias = self.common / 'production-latest.bin'; production_alias.symlink_to('production.bin')
        result = self.invoke()
        self.assertEqual(len(result), 3)
        self.assertFalse(alias.is_symlink())
        self.assertEqual(production_alias.read_bytes(), b'production')
        self.assertEqual((self.outside / 'UNRELATED').read_bytes(), b'must survive')
        for name in self.names:
            self.assertFalse((self.common / name).exists())
            self.assertEqual((self.q / ('REJECTED-UNVALIDATED-LEGACY-build-' + name)).read_bytes(), ('old bench ' + name).encode())

    def substitution(self, kind):
        original = confinement.preflight
        hits = []
        def preflight(*args, **kwargs):
            plan = original(*args, **kwargs)
            if self.q in kwargs.get('directories', ()) and any(Path(p) == self.common / self.names[1] for p in kwargs.get('files', ())):
                hits.append(True)
                if kind == 'source':
                    p = self.common / self.names[1]; os.rename(p, self.common / 'saved'); p.write_bytes(b'substitution')
                elif kind == 'ancestor':
                    os.rename(self.pio, self.project / 'old-pio'); self.pio.mkdir()
                else:
                    (self.q / ('REJECTED-UNVALIDATED-LEGACY-build-' + self.names[1])).write_bytes(b'late conflict')
            return plan
        self.q.mkdir(parents=True)
        with patch.object(confinement, 'preflight', side_effect=preflight), patch.object(Path, 'rename') as move, patch.object(Path, 'unlink') as unlink:
            with self.assertRaises(ValueError):
                self.invoke()
            move.assert_not_called(); unlink.assert_not_called()
        self.assertEqual(len(hits), 1)
        self.assertTrue((self.common / self.names[0]).exists())
        self.assertFalse((self.q / ('REJECTED-UNVALIDATED-LEGACY-build-' + self.names[0])).exists())

    def test_captured_source_substitution(self):
        self.substitution('source')

    def test_captured_ancestor_substitution(self):
        self.substitution('ancestor')

    def test_previously_absent_destination_substitution(self):
        self.substitution('destination')


if __name__ == '__main__':
    unittest.main()
