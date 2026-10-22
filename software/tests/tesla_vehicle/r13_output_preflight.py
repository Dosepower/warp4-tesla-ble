#!/usr/bin/env python3
"""Complete R13 runner output/cleanup transaction; reuse bench confinement.

The test workspace is exclusively owned. As with bench_confinement this rejects
static aliases/substitution, not hostile concurrent rename/mount races. SDK source
inputs are deliberately not output roots (they may have legitimate source links).
"""
import argparse
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from bench_confinement import lexical, output_plan

FILES = ('mbedtls-configure.log', 'mbedtls-build.log', 'module',
         'module_config', 'module_config.tmp', 'module_config.bak',
         'native/mbedtls/library/libmbedcrypto.a',
         'native/mbedtls/3rdparty/everest/libeverest.a',
         'native/mbedtls/3rdparty/p256-m/libp256m.a')

def prepare(out, cleanup=False):
    out = lexical(out)
    plan = output_plan(roots=(out / 'native/mbedtls',), directories=(out,),
                       files=tuple(out / p for p in FILES))
    plan.revalidate()
    if cleanup:
        for suffix in ('', '.tmp', '.bak'):
            (out / ('module_config' + suffix)).unlink(missing_ok=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('out')
    parser.add_argument('--cleanup', action='store_true')
    args = parser.parse_args()
    try:
        prepare(args.out, args.cleanup)
    except (OSError, ValueError) as exc:
        print('R13 output confinement rejected: ' + str(exc), file=sys.stderr)
        return 2
    return 0
if __name__ == '__main__':
    sys.exit(main())
