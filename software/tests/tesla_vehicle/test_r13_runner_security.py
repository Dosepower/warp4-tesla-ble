#!/usr/bin/env python3
"""Invoke the actual shell runner; poisoned outputs must reject before tools/IO."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
RUNNER = ROOT / 'software/tests/tesla_vehicle/run_r13_tests.sh'

def main():
    failures = []
    leaves = ('module', 'mbedtls-configure.log', 'mbedtls-build.log',
              'module_config', 'module_config.tmp', 'module_config.bak',
              'native/mbedtls/library/libmbedcrypto.a')
    cases = [(p, k) for p in leaves for k in ('symlink', 'dangling', 'hardlink', 'directory')]
    cases += [('ancestor', 'symlink'), ('out', 'symlink'), ('alias', 'hardlink')]
    with tempfile.TemporaryDirectory(prefix='r13-runner-', dir=os.environ['TMPDIR']) as tmp:
        base = Path(tmp)
        for i, (target, kind) in enumerate(cases):
            case = base / str(i); case.mkdir()
            out = case / 'warp-r13'; out.mkdir()
            victim = case / 'victim'; victim.write_bytes(b'preserve victim\x00bytes')
            marker = case / 'executed'
            tools = case / 'bin'; tools.mkdir()
            for tool in ('g++', 'cmake'):
                fake = tools / tool
                fake.write_text('#!/bin/sh\nprintf called > "$R13_SENTINEL"\nexit 79\n')
                fake.chmod(0o700)
            # Retained config siblings prove cleanup cannot run before preflight.
            for suffix in ('', '.tmp', '.bak'):
                (out / ('module_config' + suffix)).write_bytes(b'retained ' + suffix.encode())
            if target == 'ancestor':
                real = case / 'elsewhere'; real.mkdir(); (out / 'native').symlink_to(real, target_is_directory=True)
            elif target == 'out':
                out.rename(case / 'elsewhere'); out.symlink_to(case / 'elsewhere', target_is_directory=True)
            elif target == 'alias':
                (out / 'module').hardlink_to(out / 'module_config')
            else:
                leaf = out / target; leaf.parent.mkdir(parents=True, exist_ok=True)
                leaf.unlink(missing_ok=True)
                if kind == 'symlink': leaf.symlink_to(victim)
                elif kind == 'dangling': leaf.symlink_to(case / 'absent')
                elif kind == 'hardlink': leaf.hardlink_to(victim)
                else: leaf.mkdir()
            def snapshot():
                return sorted((str(p.relative_to(case)), p.lstat().st_mode, p.readlink().as_posix() if p.is_symlink() else p.read_bytes() if p.is_file() else b'')
                              for p in case.rglob('*'))
            before = snapshot()
            env = dict(os.environ, TMPDIR=str(case), PATH=str(tools)+os.pathsep+os.environ['PATH'],
                       R13_SENTINEL=str(marker), PYTHONDONTWRITEBYTECODE='1')
            result = subprocess.run(['bash', str(RUNNER)], env=env, text=True, capture_output=True, timeout=15)
            ok = result.returncode != 0 and not marker.exists() and before == snapshot()
            print(target, kind, 'exit', result.returncode, 'pre-mutation rejection', ok, flush=True)
            if not ok: failures.append((target, kind))
        # Positive control: safe targets must reach the deliberately failing tool.
        safe = base / 'safe'; safe.mkdir(); marker = safe / 'tool-called'
        env = dict(os.environ, TMPDIR=str(safe), PATH=str(tools)+os.pathsep+os.environ['PATH'], R13_SENTINEL=str(marker), PYTHONDONTWRITEBYTECODE='1')
        result = subprocess.run(['bash', str(RUNNER)], env=env, capture_output=True)
        assert result.returncode == 79 and marker.exists(), result.stderr
    print('cases', len(cases), 'failures', failures)
    return int(bool(failures))
if __name__ == '__main__':
    raise SystemExit(main())
