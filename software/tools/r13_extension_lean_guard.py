"""Extension-only source/evidence guard. Read-only outside new evidence directory."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'offline-r13-extension-only-lean-off-sol'
FORBIDDEN = [*(f'software/src/modules/{n}' for n in ('mqtt', 'certs', 'api', 'event', 'network', 'config')), 'software/patches/lib-builder/esp-idf']

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def matrix():
    result = {}
    for rel in FORBIDDEN:
        base = ROOT / rel
        for p in sorted(base.rglob('*')):
            if p.is_file():
                result[str(p.relative_to(ROOT))] = digest(p)
    return result

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['freeze', 'verify'])
    args = parser.parse_args()
    OUT.mkdir(exist_ok=True)
    current = matrix()
    baseline = OUT / 'forbidden-pre.json'
    if args.mode == 'freeze':
        assert not baseline.exists(), 'Never replace frozen baseline'
        baseline.write_text(json.dumps(current, indent=2) + '\n')
        status = subprocess.check_output(['git', 'status', '--porcelain', '-uall'], cwd=ROOT, text=True)
        (OUT / 'initial-git-status.txt').write_text(status)
        bad = []
        for rel, sha in current.items():
            head = subprocess.run(['git', 'show', f'HEAD:{rel}'], cwd=ROOT, capture_output=True)
            if head.returncode != 0:
                # Ignored generated outputs have no HEAD blob; they are still
                # protected byte-for-byte by the pre/post manifest above.
                tracked = subprocess.run(['git', 'ls-files', '--error-unmatch', rel], cwd=ROOT, capture_output=True)
                if tracked.returncode == 0:
                    bad.append(rel)
            elif hashlib.sha256(head.stdout).hexdigest() != sha:
                bad.append(rel)
        (OUT / 'forbidden-head-check.json').write_text(json.dumps({'files': len(current), 'mismatches': bad}, indent=2) + '\n')
        assert not bad, bad
    else:
        expected = json.loads(baseline.read_text())
        (OUT / 'forbidden-post.json').write_text(json.dumps(current, indent=2) + '\n')
        assert expected == current, 'FORBIDDEN SOURCE MUTATION'
    print(json.dumps({'mode': args.mode, 'forbidden_files': len(current), 'unchanged': True}))

if __name__ == '__main__':
    main()
