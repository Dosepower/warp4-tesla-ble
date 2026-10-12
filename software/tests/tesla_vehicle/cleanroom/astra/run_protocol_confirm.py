from pathlib import Path
import json,subprocess,sys,hashlib
O=Path('/home/hermes/.hermes/cache/scratch/astra-phase2-review1')
sanitized=sys.argv[1:]==['sanitize']; suffix='-sanitized' if sanitized else ''
base=json.loads((O/('protocol-probes'+suffix+'.json')).read_text())
cmd=[s.replace(str(O/'protocol_probe.cpp'),str(O/'protocol_probe_confirm.cpp')).replace(str(O/('protocol-probe'+suffix)),str(O/('protocol-probe-confirm'+suffix))) for s in base['compile']]
subprocess.run(cmd,check=True); results=[]
for mode in ['replay-vcsec','replay-info','update-vcsec','update-info']:
    command=[str(O/('protocol-probe-confirm'+suffix)),mode]
    p=subprocess.run(command,capture_output=True,text=True)
    result=dict(command=command,exit_code=p.returncode,stdout=p.stdout,stderr=p.stderr)
    print(json.dumps(result),flush=True);results.append(result)
output=O/('protocol-confirm'+suffix+'.json');assert not output.exists()
output.write_text(json.dumps(dict(compile=cmd,source_sha256=hashlib.sha256((O/'protocol_probe_confirm.cpp').read_bytes()).hexdigest(),results=results),indent=2)+'\n')
assert all(r['exit_code']==1 and not r['stderr'] for r in results), 'A probe did not reproduce the expected behavioral defect; inspect results'
print('All four product failures reproduced with controls; diagnostic wrapper exit zero is NOT product success')
