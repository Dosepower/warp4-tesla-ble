"""Independently encoded official protobufs through native production RX/worker/API."""
from pathlib import Path
import os,sys,json,tempfile,subprocess
T=Path(__file__).resolve().parent
cases=json.loads((T/'host/official_pair_responses.json').read_text())['cases']
with tempfile.TemporaryDirectory(prefix='pair-status-',dir=os.environ['TMPDIR']) as tmp:
 d=Path(tmp);binary=d/'native';env={**os.environ,'TESLA_NIMBLE_MODULE_BINARY_OUT':str(binary),'TESLA_NIMBLE_MODULE_MODES':''}
 subprocess.run([sys.executable,str(T/'test_nimble_module.py'),*sys.argv[1:]],env=env,check=True)
 terminal=[c for c in cases if c['terminal']]
 groups=[terminal]+[[c]for c in cases if not c['terminal']]
 for i,group in enumerate(groups):
  f=d/('cases-'+str(i));f.write_text(''.join(f"{c['code']} {int(c['terminal'])} {c['hex']}\n"for c in group))
  print('OFFICIAL_PAIR_GROUP',i,[c['name']for c in group],flush=True)
  subprocess.run([str(binary),'pair-response'],env={**env,'TESLA_PAIR_RESPONSE_CASES':str(f)},check=True,timeout=60)
 print('OFFICIAL_PAIR_MATRIX_PASS',len(cases),'cases, all terminal denials sequentially in same runtime with same key')
