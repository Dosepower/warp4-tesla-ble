#!/usr/bin/env python3
"""R10 inputs for the unchanged, retained R9 exact-index/parent verifier."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--allowlist', type=Path, default=Path(__file__).with_name('publication-allowlist.json'))
    parser.add_argument('--index', type=Path, required=True)
    parser.add_argument('--proposed-parent', required=True)
    parser.add_argument('--candidate-commit')
    args = parser.parse_args()
    try:
        source = Path(__file__).resolve().parents[1] / 'r9/verify_publication.py'
        # R9 reviewed verifier is retained byte-for-byte; R10 changes its inputs,
        # not the already independently tested boundary/ancestry implementation.
        if hashlib.sha256(source.read_bytes()).hexdigest() != 'c8f3e549b8860ecb5fb0b61a0f1ecb561f38bb3e510fef012d99561ffec17586':
            raise ValueError('retained R9 verifier identity changed')
        spec = importlib.util.spec_from_file_location('retained_r9_verifier', source)
        assert spec is not None and spec.loader is not None
        verifier = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(verifier)
        if not args.index.is_file() or args.index.is_symlink():
            raise ValueError('existing regular proposed index required')
        print(json.dumps(verifier.verify(args.repo, args.allowlist, args.index,
                                         args.proposed_parent, args.candidate_commit), indent=2))
    except (OSError, ValueError, KeyError, UnicodeError, subprocess.CalledProcessError) as exc:
        print('publication verification failed: ' + str(exc), file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
