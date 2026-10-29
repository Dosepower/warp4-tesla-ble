"""Managed-component pristine check vs. idf-component-manager 2.x metadata.

Root cause (Stage A, manager 2.5.2): the manager downloads CHECKSUMS.json from
the registry checksums_url separately from the archive and copies it into
every managed component next to .component_hash. The recipe removed only
.component_hash, so hash_dir also hashed CHECKSUMS.json and every component
differed from its pinned registry hash.

RED runs the pre-fix verify_managed_components body (reconstructed by exact
text substitution); GREEN runs the production recipe. Every run uses scratch
copies; the real managed_components root is only read.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

S = Path(__file__).resolve().parents[2]
SRC = S / 'lib-builder/tesla_sdk.py'
PRIV = Path(os.environ.get('TESLA_SDK_PRIVATE_ROOT', '/home/hermes/warp-tesla-timing-upstream/sdk-rebuild/privat'))
MANAGED = Path(os.environ.get('TESLA_SDK_MANAGED_SOURCE', str(PRIV / 'sdk-src/builder/managed_components')))
IDF = Path(os.environ.get('TESLA_SDK_IDF_PATH', str(PRIV / 'sdk-src/esp-idf-secure')))
TOOLS = Path(os.environ.get('TESLA_SDK_IDF_TOOLS_PATH', str(PRIV / 'idf-tools')))
REGISTRY = PRIV / 'verify/registry'
ZIPS = {'espressif__mdns': 'espressif__mdns-v1.14.0.zip',
        'espressif__libsodium': 'espressif__libsodium-v1.0.22_1.zip',
        'joltwallet__littlefs': 'joltwallet__littlefs-v1.23.1.zip'}
NEW = '            strip_manager_metadata(copy, expected)\n'
OLD = ("            for marker in ('.component_hash',):\n"
       "                (copy / marker).unlink(missing_ok=True)\n")
RAWPATCH = S / 'patches/lib-builder/managed_components/espressif__mdns/0004-mdns-Place-static-buffer-to-assemble-packets-on-stac.rawpatch'


def load(name, text):
    spec = importlib.util.spec_from_loader(name, loader=None, origin=str(SRC))
    mod = importlib.util.module_from_spec(spec)
    mod.__file__ = str(SRC)
    exec(compile(text, str(SRC), 'exec'), mod.__dict__)
    return mod


PROD_TEXT = SRC.read_text()
sdk = load('tesla_sdk_prod', PROD_TEXT)
assert PROD_TEXT.count(NEW) == 1
pre_fix = load('tesla_sdk_prefix', PROD_TEXT.replace(NEW, OLD))
CEILING = "    return dict(os.environ, GIT_CEILING_DIRECTORIES=str(Path(target).resolve().parent))\n"
assert PROD_TEXT.count(CEILING) == 1
# Pre-fix git discovery: patches applied with the inherited environment only.
pre_fix_apply = load('tesla_sdk_prefix_apply', PROD_TEXT.replace(CEILING, '    return dict(os.environ)\n'))
SEND_PRISTINE = 'e2ac7c84ac1314a14cd20f1cd8b717e5d7f7584386cff1092734ec96a72b2146'
SEND_PATCHED = '483961b60027c726b20ae50562b6f694760fa67ee1b0fe54dfa97d31955032f1'


def outer_repo(td):
    # Same layout as sdk-src/builder: a git repo ignoring managed_components/.
    outer = Path(td) / 'builder'
    outer.mkdir()
    subprocess.run(['git', 'init', '-q', str(outer)], check=True)
    (outer / '.gitignore').write_text('managed_components/\n')
    return outer


def tree_digest(root):
    h = hashlib.sha256()
    for p in sorted(Path(root).rglob('*')):
        h.update(p.relative_to(root).as_posix().encode() + b'\0')
        if p.is_symlink():
            h.update(b'L' + os.readlink(p).encode())
        elif p.is_file():
            h.update(hashlib.sha256(p.read_bytes()).digest())
    return h.hexdigest()


def file_map(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in Path(root).rglob('*') if p.is_file()}


def rewrite_checksums(comp):
    # Attacker-consistent metadata: CHECKSUMS.json regenerated for the mutated tree.
    data = json.loads((comp / 'CHECKSUMS.json').read_text())
    data['files'] = [{'path': p.relative_to(comp).as_posix(), 'size': p.stat().st_size,
                      'hash': hashlib.sha256(p.read_bytes()).hexdigest()}
                     for p in sorted(comp.rglob('*')) if p.is_file()
                     and p.relative_to(comp).as_posix() not in ('CHECKSUMS.json', '.component_hash')]
    (comp / 'CHECKSUMS.json').write_text(json.dumps(data))


class MetadataUnitTests(unittest.TestCase):
    """Pure-Python checks of strip_manager_metadata (no IDF needed)."""

    def make(self, td, expected='a' * 64):
        comp = Path(td) / 'c'
        (comp / 'sub').mkdir(parents=True)
        (comp / 'x.c').write_text('int x;\n')
        (comp / 'sub/y.h').write_text('#pragma once\n')
        (comp / 'CHECKSUMS.json').write_text(json.dumps({'version': '1.0', 'algorithm': 'sha256', 'files': []}))
        rewrite_checksums(comp)
        (comp / '.component_hash').write_text(expected)
        return comp

    def test_consistent_metadata_removed_content_untouched(self):
        with tempfile.TemporaryDirectory() as td:
            comp = self.make(td)
            sdk.strip_manager_metadata(comp, 'a' * 64)
            self.assertEqual(sorted(file_map(comp)), ['sub/y.h', 'x.c'])

    def test_absent_metadata_accepted(self):
        with tempfile.TemporaryDirectory() as td:
            comp = self.make(td)
            (comp / 'CHECKSUMS.json').unlink()
            (comp / '.component_hash').unlink()
            sdk.strip_manager_metadata(comp, 'a' * 64)
            self.assertEqual(sorted(file_map(comp)), ['sub/y.h', 'x.c'])

    def test_rejections(self):
        cases = {
            'hash marker': lambda c: (c / '.component_hash').write_text('b' * 64),
            'describe': lambda c: (c / 'x.c').write_text('int y;\n'),
            'describe-extra': lambda c: (c / 'extra.c').write_text(''),
            'describe-missing': lambda c: (c / 'sub/y.h').unlink(),
            'describe-version': lambda c: (c / 'CHECKSUMS.json').write_text(
                json.dumps(json.loads((c / 'CHECKSUMS.json').read_text()) | {'version': '2.0'})),
            'malformed': lambda c: (c / 'CHECKSUMS.json').write_text(json.dumps(
                {'version': '1.0', 'algorithm': 'sha256', 'files': [{'path': 'x.c'}, {'path': 'x.c'}]})),
            'symlink': lambda c: (c / 'link').symlink_to('x.c'),
        }
        for msg, mutate in cases.items():
            with self.subTest(msg), tempfile.TemporaryDirectory() as td:
                comp = self.make(td)
                mutate(comp)
                with self.assertRaisesRegex(ValueError, msg.split('-')[0]):
                    sdk.strip_manager_metadata(comp, 'a' * 64)

    def test_metadata_symlinks_rejected(self):
        for name in ('CHECKSUMS.json', '.component_hash'):
            with self.subTest(name), tempfile.TemporaryDirectory() as td:
                comp = self.make(td)
                (Path(td) / 'outside').write_text((comp / name).read_text())
                (comp / name).unlink()
                (comp / name).symlink_to(Path(td) / 'outside')
                with self.assertRaisesRegex(ValueError, 'regular file'):
                    sdk.strip_manager_metadata(comp, 'a' * 64)

    def test_nested_checksums_name_is_content(self):
        with tempfile.TemporaryDirectory() as td:
            comp = self.make(td)
            (comp / 'sub/CHECKSUMS.json').write_text('{}')
            with self.assertRaisesRegex(ValueError, 'describe'):
                sdk.strip_manager_metadata(comp, 'a' * 64)
            rewrite_checksums(comp)
            sdk.strip_manager_metadata(comp, 'a' * 64)
            self.assertIn('sub/CHECKSUMS.json', file_map(comp))


@unittest.skipUnless(MANAGED.is_dir() and (IDF / 'export.sh').is_file() and TOOLS.is_dir(),
                     'private Stage A tree / IDF tools unavailable')
class RealManagedComponentTests(unittest.TestCase):
    """End-to-end with real registry components and the real IDF hash_dir."""

    @classmethod
    def setUpClass(cls):
        cls.before = tree_digest(MANAGED)
        cls.env = dict(os.environ, IDF_PATH=str(IDF), IDF_TOOLS_PATH=str(TOOLS))
        cls.env.pop('VIRTUAL_ENV', None)
        cls.env['PYTHONDONTWRITEBYTECODE'] = '1'

    @classmethod
    def tearDownClass(cls):
        assert tree_digest(MANAGED) == cls.before, 'real managed_components root was modified'

    def scratch(self, td):
        builder = Path(td) / 'builder'
        shutil.copytree(MANAGED, builder / 'managed_components', symlinks=True)
        return builder

    def test_red_pre_fix_rejects_unmodified_manager_tree(self):
        with tempfile.TemporaryDirectory() as td:
            with self.assertRaisesRegex(ValueError, 'pinned registry hash: espressif__mdns'):
                pre_fix.verify_managed_components(self.scratch(td), self.env)

    def test_red_pre_fix_accepts_once_checksums_absent(self):
        # Isolates the mechanism: CHECKSUMS.json is the sole reason for RED.
        with tempfile.TemporaryDirectory() as td:
            builder = self.scratch(td)
            for comp in (builder / 'managed_components').iterdir():
                (comp / 'CHECKSUMS.json').unlink()
            pre_fix.verify_managed_components(builder, self.env)

    def test_green_unmodified_manager_tree(self):
        with tempfile.TemporaryDirectory() as td:
            sdk.verify_managed_components(self.scratch(td), self.env)

    def test_green_with_pinned_mdns_patch_applied(self):
        with tempfile.TemporaryDirectory() as td:
            builder = self.scratch(td)
            mdns = builder / 'managed_components/espressif__mdns'
            subprocess.run(['git', 'apply', str(RAWPATCH)], cwd=mdns, check=True)
            self.assertNotEqual(file_map(mdns)['mdns_send.c'], file_map(MANAGED / 'espressif__mdns')['mdns_send.c'])
            sdk.verify_managed_components(builder, self.env)

    def test_pristine_equals_registry_archive_exactly(self):
        for local, zname in ZIPS.items():
            with self.subTest(local), tempfile.TemporaryDirectory() as td:
                if not (REGISTRY / zname).is_file():
                    self.skipTest('registry archive unavailable')
                with zipfile.ZipFile(REGISTRY / zname) as z:
                    z.extractall(Path(td) / 'zip')
                copy = Path(td) / 'c'
                shutil.copytree(MANAGED / local, copy, symlinks=True)
                diff = set(file_map(copy).items()) ^ set(file_map(Path(td) / 'zip').items())
                self.assertEqual({p for p, _ in diff}, {'CHECKSUMS.json', '.component_hash'})
                sdk.strip_manager_metadata(copy, sdk.MANAGED_COMPONENT_HASHES[local])
                self.assertEqual(file_map(copy), file_map(Path(td) / 'zip'))

    def test_red_apply_patches_silently_skips_inside_builder_repo(self):
        for mod, want in ((pre_fix_apply, SEND_PRISTINE), (sdk, SEND_PATCHED)):
            with self.subTest(mod.__name__), tempfile.TemporaryDirectory() as td:
                managed = outer_repo(td) / 'managed_components'
                shutil.copytree(MANAGED, managed, symlinks=True)
                self.assertEqual(file_map(managed / 'espressif__mdns')['mdns_send.c'], SEND_PRISTINE)
                patches = S / 'patches/lib-builder/managed_components'
                mod.apply_patches(managed, patches)
                mod.apply_patches(managed, patches)  # idempotent re-run (Stage B re-enters prepare)
                self.assertEqual(file_map(managed / 'espressif__mdns')['mdns_send.c'], want)
                if mod is sdk:
                    # Patched in-repo tree still proves pristine registry content.
                    sdk.verify_managed_components(managed.parent, self.env)

    def test_reverse_in_repo_scratch_dir(self):
        # verify_managed_components' scratch copy inside a repo (TMPDIR in a worktree).
        for mod, ok in ((pre_fix_apply, False), (sdk, True)):
            with self.subTest(mod.__name__), tempfile.TemporaryDirectory() as td:
                builder = self.scratch(td)
                subprocess.run(['git', 'apply', str(RAWPATCH)], cwd=builder / 'managed_components/espressif__mdns',
                               env=sdk.patch_env(builder / 'managed_components/espressif__mdns'), check=True)
                (Path(td) / 'r').mkdir()
                saved, tempfile.tempdir = tempfile.tempdir, str(outer_repo(Path(td) / 'r'))
                try:
                    if ok:
                        mod.verify_managed_components(builder, self.env)
                    else:
                        with self.assertRaisesRegex(ValueError, 'espressif__mdns'):
                            mod.verify_managed_components(builder, self.env)
                finally:
                    tempfile.tempdir = saved

    def test_mutations_rejected(self):
        def byte(comp):
            p = comp / 'mdns_send.c'
            p.write_bytes(p.read_bytes().replace(b'static', b'Static', 1))
        cases = {
            # Content change with consistent CHECKSUMS.json: pinned hash_dir must still reject.
            'byte+checksums': ('espressif__mdns', lambda c: (byte(c), rewrite_checksums(c)), 'pinned registry hash'),
            'extra+checksums': ('espressif__libsodium', lambda c: ((c / 'extra.c').write_text('x'), rewrite_checksums(c)), 'pinned registry hash'),
            'nested-checksums+checksums': ('joltwallet__littlefs', lambda c: ((c / 'src').mkdir(exist_ok=True), (c / 'src/CHECKSUMS.json').write_text('{}'), rewrite_checksums(c)), 'pinned registry hash'),
            'byte': ('espressif__mdns', byte, 'does not describe'),
            'hash-marker': ('espressif__mdns', lambda c: (c / '.component_hash').write_text('0' * 64), 'marker differs'),
            'patch-plus-byte': ('espressif__mdns', lambda c: (subprocess.run(['git', 'apply', str(RAWPATCH)], cwd=c, check=True),
                                                              (c / 'mdns_cache.c').write_bytes((c / 'mdns_cache.c').read_bytes() + b'\n')), 'does not describe'),
        }
        for name, (local, mutate, msg) in cases.items():
            with self.subTest(name), tempfile.TemporaryDirectory() as td:
                builder = self.scratch(td)
                mutate(builder / 'managed_components' / local)
                with self.assertRaisesRegex(ValueError, msg + '.*' + local):
                    sdk.verify_managed_components(builder, self.env)


if __name__ == '__main__':
    unittest.main()
