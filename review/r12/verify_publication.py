#!/usr/bin/env python3
"""R12 inputs for immutable R9 exact private-index/sole-parent verifier."""
import argparse,hashlib,importlib.util,json
from pathlib import Path
import subprocess,sys

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2])
 p.add_argument('--allowlist',type=Path,default=Path(__file__).with_name('publication-allowlist.json'))
 p.add_argument('--index',type=Path,required=True)
 p.add_argument('--proposed-parent',required=True)
 p.add_argument('--candidate-commit')
 a=p.parse_args()
 try:
  source=Path(__file__).resolve().parents[1]/'r9/verify_publication.py'
  if hashlib.sha256(source.read_bytes()).hexdigest()!='c8f3e549b8860ecb5fb0b61a0f1ecb561f38bb3e510fef012d99561ffec17586':raise ValueError('retained R9 verifier changed')
  spec=importlib.util.spec_from_file_location('retained_r9',source);assert spec and spec.loader
  v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
  if not a.index.is_file() or a.index.is_symlink():raise ValueError('existing regular private index required')
  print(json.dumps(v.verify(a.repo,a.allowlist,a.index,a.proposed_parent,a.candidate_commit),indent=2))
 except (OSError,ValueError,KeyError,UnicodeError,subprocess.CalledProcessError) as ex:
  print('publication verification failed: '+str(ex),file=sys.stderr);return 2
 return 0
if __name__=='__main__':sys.exit(main())
