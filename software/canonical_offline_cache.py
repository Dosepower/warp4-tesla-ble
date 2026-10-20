"""Canonical-only offline cache attestation after PlatformIO build-dir cleanup.
Does not fetch packages or alter source/framework archives. Validates each cached
npm file before populating the ordinary hook's dependency digests.
"""
from pathlib import Path
import hashlib,json,os,tomllib
Import('env')
if env['PIOENV'] not in ('warp4_tesla_ble_psram_manual_canonical','warp4_tesla_ble_vehicle_canonical'):raise ValueError('canonical offline cache profile mismatch')
project=Path(env.subst('$PROJECT_DIR')).resolve();build=Path(env.subst('$BUILD_DIR')).resolve()
if not build.is_relative_to(project) or build==project:raise ValueError('nonprivate build output')
cache=project/'web/node_modules'
manifest=json.loads(Path(os.environ['CANONICAL_NODE_CACHE_MANIFEST']).read_text())
for rel,digest in manifest.items():
 p=cache/rel
 if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=digest:raise ValueError('offline npm cache drift: '+rel)
if not (cache/'tinkerforge.marker').is_file():raise ValueError('incomplete offline npm cache')
pins=tomllib.loads((project/'pyproject.toml').read_text())['tool']['pio-hooks']
h=hashlib.sha256((project/'util.py').read_bytes()+(project/'web/package-lock.json').read_bytes()+pins['requires-node'].encode()+pins['requires-npm'].encode()).hexdigest()
for dest in (project/'web/node_modules.digest',build/'web,node_modules.digest'):
 dest.parent.mkdir(parents=True,exist_ok=True)
 if dest.is_symlink():raise ValueError('symlink cache digest rejected')
 dest.write_text(h)
print('Canonical offline npm cache rehashed:',len(manifest),'files; no network/bootstrap substitution')
