#!/usr/bin/env python3
"""Confinement-checked, read-only host dependency selection.

The frontend generator may clean the worktree's .pio/libdeps tree. Host-only
harnesses therefore use this separately owned, immutable ArduinoJson source and
never create a symlink/copy in the repository.
"""
import os
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from bench_confinement import output_plan

def main():
    source = Path(sys.argv[1] if len(sys.argv) > 1 else os.environ.get(
        'TESLA_HOST_ARDUINOJSON_SOURCE',
        str(Path(__file__).resolve().parents[2] / '.pio/libdeps/warp4/ArduinoJson')))
    source = source.absolute()
    if not source.is_dir() or source.is_symlink():
        raise SystemExit('host dependency is not an owned regular directory: ' + str(source))
    # Validate the complete read-only source tree and all ancestors. No output
    # leaf is created, copied, linked, or cleaned by this provisioner.
    output_plan(roots=(source,)).revalidate()
    if not (source / 'src/ArduinoJson.h').is_file():
        raise SystemExit('missing ArduinoJson entry header')
    print(source)
if __name__ == '__main__':
    main()
