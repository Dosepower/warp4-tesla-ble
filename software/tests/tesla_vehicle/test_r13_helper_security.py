#!/usr/bin/env python3
"""Poison the actual new helper/runner paths; no victim or sentinel may change."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[3]
HELPERS = ROOT/'software/tests/tesla_vehicle'
NAMES = ('version_features.c','error.c','psa_crypto_driver_wrappers_no_static.c','ssl_debug_helpers_generated.c')

def snapshot(root):
    return sorted((str(p.relative_to(root)), p.lstat().st_mode,
                   str(p.readlink()) if p.is_symlink() else p.read_bytes() if p.is_file() else b'')
                  for p in root.rglob('*'))

def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix='r13-helper-security-',dir=os.environ['TMPDIR']) as td:
        base = Path(td)
        cases = ('foreign-source','wrong-destination','source-chain','source-hardlink','source-ancestor','sibling-symlink','sibling-hardlink','alias','buildroot','positive')
        for case in cases:
            root = base/case; root.mkdir()
            out = root/'out'; libs = out/'mbedtls'; (libs/'library').mkdir(parents=True)
            source = root/'source'; (source/'library').mkdir(parents=True)
            victim = root/'victim'; victim.write_bytes(b'victim bytes unchanged')
            sentinel = out/'sentinel'; sentinel.write_bytes(b'unchanged')
            for name in NAMES:
                (source/'library'/name).write_bytes(name.encode())
                (libs/'library'/name).symlink_to(source/'library'/name)
            link = libs/'library'/NAMES[0]
            if case == 'foreign-source': link.unlink(); link.symlink_to(victim)
            if case == 'wrong-destination': link.rename(libs/NAMES[0])
            if case == 'source-chain':
                (source/'library'/NAMES[0]).unlink(); (source/'library'/NAMES[0]).symlink_to(victim)
            if case == 'source-hardlink':
                (source/'library'/NAMES[0]).unlink(); (source/'library'/NAMES[0]).hardlink_to(victim)
            if case == 'source-ancestor':
                (source/'library').rename(source/'real'); (source/'library').symlink_to(source/'real')
            if case == 'sibling-symlink': (out/'later-output').symlink_to(victim)
            if case == 'sibling-hardlink': (out/'later-output').hardlink_to(victim)
            if case == 'alias': (out/'later-output').hardlink_to(sentinel)
            expected_libs = libs if case != 'buildroot' else out/'other-mbedtls'
            env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', TESLA_CLEAN_TEST_OUT=str(out),
                       TESLA_CLEAN_MBEDTLS_BUILD=str(expected_libs), TESLA_BLE_HOST_MBEDTLS_SOURCE=str(source))
            before = snapshot(root)
            result = subprocess.run(['python3','-B',str(HELPERS/'materialize_cmake_sources.py'),str(libs)],env=env,capture_output=True,text=True)
            ok = result.returncode == 0 if case == 'positive' else result.returncode != 0 and snapshot(root) == before
            if case == 'positive': ok = ok and all(not (libs/'library'/n).is_symlink() and (libs/'library'/n).read_bytes() == n.encode() for n in NAMES)
            print('materializer',case,'exit',result.returncode,'safe',ok,flush=True)
            if not ok: failures.append(case)
        for kind in ('missing','empty-dir','symlink','hardlink','ancestor','alias'):
            root = base/('dependency-'+kind); root.mkdir(); dep = root/'dep'; dep.mkdir()
            (dep/'src').mkdir(); header = dep/'src/ArduinoJson.h'; header.write_text('// dependency')
            victim = root/'victim'; victim.write_bytes(b'preserve')
            if kind == 'missing': dep = root/'absent'
            elif kind == 'empty-dir': header.unlink()
            elif kind == 'symlink': header.unlink(); header.symlink_to(victim)
            elif kind == 'hardlink': header.unlink(); header.hardlink_to(victim)
            elif kind == 'ancestor': (dep/'src').rename(dep/'real'); (dep/'src').symlink_to(dep/'real')
            elif kind == 'alias': (dep/'src/alias.h').hardlink_to(header)
            before = snapshot(root)
            result = subprocess.run(['python3','-B',str(HELPERS/'r13_host_dependency.py'),str(dep)],capture_output=True,text=True)
            ok = result.returncode != 0 and snapshot(root) == before
            print('dependency',kind,'exit',result.returncode,'safe',ok,flush=True)
            if not ok: failures.append('dependency-'+kind)
        # Execute actual runners with failing/empty dependency checks. A fake
        # python interprets only the dependency selection; g++ marks any escape.
        for mode in ('nonzero','empty'):
            for runner in ('run_host_tests.sh','cleanroom/run_test.sh'):
                root = base/(mode+'-'+Path(runner).stem); root.mkdir(); tools = root/'bin'; tools.mkdir()
                marker = root/'executed'
                python = tools/'python3'
                python.write_text('#!/bin/sh\ncase "$*" in *r13_host_dependency.py*) '+('exit 73' if mode == 'nonzero' else 'exit 0')+';; esac\nexec '+sys.executable+' "$@"\n')
                python.chmod(0o700)
                compiler = tools/'g++'; compiler.write_text('#!/bin/sh\nprintf called > "$R13_SENTINEL"\nexit 79\n'); compiler.chmod(0o700)
                env = dict(os.environ, PATH=str(tools)+os.pathsep+os.environ['PATH'],TESLA_CLEAN_TEST_OUT=str(root/'out'),
                           TESLA_CLEAN_MBEDTLS_BUILD=str(root/'out/mbedtls'),R13_SENTINEL=str(marker), PYTHONDONTWRITEBYTECODE='1')
                before = snapshot(root)
                args = ['bash',str(HELPERS/runner)] + (['test_acceptance_schedules'] if '/' in runner else [])
                result = subprocess.run(args,env=env,capture_output=True,text=True,timeout=20)
                ok = result.returncode != 0 and snapshot(root) == before
                print('runner',runner,mode,'exit',result.returncode,'pre-mutation',ok,flush=True)
                if not ok: failures.append(runner+'-'+mode)
        # A valid CMake alias must remain an alias if a later sibling poisons
        # the complete transaction. Exercise BOTH actual shell entry points.
        for runner in ('run_host_tests.sh','cleanroom/run_test.sh'):
            root = base/('sibling-'+Path(runner).stem); root.mkdir()
            out = root/'out'; libs = out/'mbedtls'; (libs/'library').mkdir(parents=True)
            source = root/'source'; (source/'library').mkdir(parents=True)
            dep = root/'dependency'; (dep/'src').mkdir(parents=True)
            (dep/'src/ArduinoJson.h').write_text('// valid entry')
            for name in NAMES:
                (source/'library'/name).write_text(name)
                (libs/'library'/name).symlink_to(source/'library'/name)
            victim = root/'victim'; victim.write_bytes(b'do not overwrite')
            (out/'z-later-output').symlink_to(victim)
            env = dict(os.environ, TESLA_CLEAN_TEST_OUT=str(out),TESLA_CLEAN_MBEDTLS_BUILD=str(libs),
                       TESLA_BLE_HOST_MBEDTLS_SOURCE=str(source),TESLA_HOST_ARDUINOJSON_SOURCE=str(dep),PYTHONDONTWRITEBYTECODE='1')
            before = snapshot(root)
            args = ['bash',str(HELPERS/runner)] + (['test_acceptance_schedules'] if '/' in runner else [])
            result = subprocess.run(args,env=env,capture_output=True,text=True,timeout=20)
            ok = result.returncode != 0 and snapshot(root) == before
            print('runner',runner,'unsafe sibling exit',result.returncode,'pre-mutation',ok,flush=True)
            if not ok: failures.append(runner+'-sibling')
    print('helper security failures:',failures)
    return int(bool(failures))
if __name__ == '__main__': raise SystemExit(main())
