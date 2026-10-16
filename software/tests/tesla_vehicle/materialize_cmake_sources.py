#!/usr/bin/env python3
"""Materialize only CMake's known generated source aliases after full preflight."""
import os
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from bench_confinement import lexical, output_plan

NAMES = ('version_features.c', 'error.c', 'psa_crypto_driver_wrappers_no_static.c',
         'ssl_debug_helpers_generated.c')

def main():
    if len(sys.argv) != 2: raise SystemExit('usage: materialize_cmake_sources.py LIBS')
    out = lexical(os.environ.get('TESLA_CLEAN_TEST_OUT', str(Path(os.environ['TMPDIR']) / 'tesla-clean-native')))
    root = lexical(sys.argv[1])
    expected = lexical(os.environ.get('TESLA_CLEAN_MBEDTLS_BUILD', str(out / 'mbedtls')))
    if root != expected:
        raise SystemExit('unexpected Mbed TLS build root')
    source = lexical(os.environ.get('TESLA_BLE_HOST_MBEDTLS_SOURCE',
        '/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/esp-idf/components/mbedtls/mbedtls'))
    pairs = tuple((source / 'library' / name, root / 'library' / name) for name in NAMES)
    links, sources = [], []
    for src, dst in pairs:
        if dst.is_symlink():
            if os.readlink(dst) != str(src):
                raise SystemExit('unexpected CMake source/destination pair: ' + str(dst))
            links.append(dst); sources.append(src)
    # The WHOLE output transaction, not just the subtree being materialized.
    # Unknown links (including a permitted basename at the wrong destination),
    # hardlinks and unsafe siblings reject before the first unlink/create.
    plan = output_plan(roots=(out, root), files=tuple(sources), links=tuple(links))
    plan.revalidate()
    if any(not src.is_file() or src.resolve() != src for src in sources):
        raise SystemExit('missing/noncanonical CMake source')
    payloads=[source.read_bytes() for source in sources]
    plan.revalidate()
    # The output tree is an exclusively owned scratch tree. Unlink only the
    # preflighted symlink, then create a new file with O_EXCL; never follow it.
    import os as _os
    for link, data in zip(links,payloads):
        link.unlink()
        fd=_os.open(link, _os.O_WRONLY|_os.O_CREAT|_os.O_EXCL, 0o600)
        try:
            offset=0
            while offset < len(data):
                count = _os.write(fd, data[offset:])
                if count <= 0: raise OSError('short CMake source materialization')
                offset += count
        finally: _os.close(fd)
if __name__ == '__main__': main()
