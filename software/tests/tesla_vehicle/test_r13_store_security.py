#!/usr/bin/env python3
"""Native POSIX production-store regressions; all fixtures in a fresh scratch dir."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'software/tools'))
from bench_confinement import output_plan

def main():
    with tempfile.TemporaryDirectory(prefix='r13-store-', dir=os.environ['TMPDIR']) as tmp:
        out = Path(tmp)
        binary = out / 'store'
        output_plan(roots=(out,), files=(binary, out/'vfs-unavailable')).revalidate()
        flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror']
        if 'sanitize' in sys.argv:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-no-pie']
        subprocess.run(['g++', *flags, str(ROOT/'software/tests/tesla_vehicle/test_r13_store_security.cpp'),
                        '-Wl,--wrap=open,--wrap=read,--wrap=write,--wrap=close,--wrap=fsync,--wrap=rename,--wrap=unlink,--wrap=ftruncate',
                        '-o', str(binary)], check=True)
        cases = [f'{leaf}:{kind}' for leaf in ('canonical', 'tmp', 'bak')
                 for kind in ('symlink', 'dangling', 'hardlink', 'directory', 'fifo')]
        cases += ['ancestor', 'alias:ct', 'alias:cb', 'alias:tb', 'prefix', 'faults']
        failures = []
        for i, case in enumerate(cases):
            result = subprocess.run([str(binary), str(out / str(i)), case], text=True, capture_output=True, timeout=10)
            print(case, 'exit', result.returncode, result.stdout.strip(), result.stderr.strip(), flush=True)
            if result.returncode:
                failures.append(case)
        print('cases:', len(cases), 'failures:', failures)
        return int(bool(failures))
if __name__ == '__main__':
    sys.exit(main())
