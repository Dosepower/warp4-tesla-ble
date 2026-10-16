#!/usr/bin/env python3
"""Check exactly allowlisted source; no recursive private/build-tree scan."""
from pathlib import Path
import hashlib,json
R=Path(__file__).resolve().parents[2]
A=R/'review/r7/publication-allowlist.json'
data=json.loads(A.read_text())
assert data['base_revision']=='a114447c4582456d123a0832eaa2d3ce5dd29f88'
for row in data['source_test_docs']+data['generated_evidence']:
 p=Path(row['path']);assert not p.is_absolute() and '..' not in p.parts
 assert not any(n in p.parts for n in ['.pio','hardware-evidence','.env','__pycache__'])
 assert not p.suffix in ['.bin','.elf','.map','.a','.o','.pcap','.log']
 f=R/p;assert f.is_file() and not f.is_symlink()
 assert hashlib.file_digest(f.open('rb'),'sha256').hexdigest()==row['sha256'],str(p)
 assert f.stat().st_size==row['bytes'],str(p)
print(json.dumps({'allowlisted_source_test_docs':len(data['source_test_docs']),'compact_generated_evidence':len(data['generated_evidence']),'control_files':data['control_files'],'hashes_verified':True,'scope':'source-publication preparation, not license/distribution approval'},indent=2))
