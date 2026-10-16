#!/usr/bin/env python3
"""Read-only R12 compact packet verification; no staging or network."""
from pathlib import Path
import hashlib,importlib.util,json,os,subprocess,sys
R=Path(__file__).resolve().parents[2]

def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def canonical(x):return hashlib.sha256(json.dumps(x,sort_keys=True,separators=(',',':')).encode()).hexdigest()
def inventory(root):
 result={}
 def fail(error):raise error
 for directory,dirs,files in os.walk(root,followlinks=False,onerror=fail):
  dirs[:]=sorted(n for n in dirs if n!='hardware-evidence')
  for n in sorted(dirs+files):
   if n=='hardware-evidence':continue
   p=Path(directory)/n;s=p.lstat();row={'mode':s.st_mode}
   if p.is_symlink():row['link']=str(p.readlink())
   elif p.is_file():row.update(sha256=sha(p),bytes=s.st_size)
   result[str(p.relative_to(root))]=row
 return result

def verify(packet):
 d=json.loads(packet.read_text())
 assert d['revision']=='R12'
 assert sha(R/'review/r12/software-evidence-summary.json')==d['summary_sha256']
 for row in d['delta_files']:
  p=R/row['path'];assert not any(q.is_symlink() for q in [p,*p.parents])
  assert sha(p)==row['sha256'] and p.stat().st_size==row['bytes'],row['path']
 for row in d['protected_inventories']:
  root=Path(row['root']);got=inventory(root)
  assert len(got)==row['entries'] and canonical(got)==row['inventory_sha256'],row['root']
  if row['kind']=='seal':
   assert sha(root/'manifest.json')==row['manifest_sha256'] and not root.stat().st_mode&0o222
   assert all('link' not in x and not x['mode']&0o222 for x in got.values())
 for name,want in d['baseline_source_sha256'].items():assert sha(Path(d['baseline_root'])/name)==want,name
 for row in d['evidence']+d['reused_evidence']['files']:
  p=Path(row['path']);assert sha(p)==row['sha256'],str(p)
 allow=json.loads((R/'review/r11/publication-allowlist.json').read_text())
 unchanged=[row for row in allow['source_test_docs']+allow['generated_evidence'] if row['path'] not in d['changed_runtime_paths']]
 controls=[{'path':p,'sha256':sha(R/p),'bytes':(R/p).stat().st_size,'mode':'100755' if (R/p).stat().st_mode&0o111 else '100644'} for p in allow['control_files']]
 assert canonical(sorted(unchanged+controls,key=lambda x:x['path']))==d['unchanged_r11_source_inventory_sha256']
 for row in unchanged:
  p=R/row['path'];assert not p.is_symlink() and sha(p)==row['sha256'] and p.stat().st_size==row['bytes'],row['path']
  assert ('100755' if p.stat().st_mode&0o111 else '100644')==row['mode']
 env=dict(os.environ,GIT_OPTIONAL_LOCKS='0',GIT_NO_REPLACE_OBJECTS='1')
 git=lambda *a:subprocess.check_output(['git','-C',str(R),*a],env=env)
 assert sha(R/'.git/index')==d['real_index']['sha256']
 assert hashlib.sha256(git('ls-files','--stage','-z')).hexdigest()==d['real_index']['entries_sha256']
 assert not git('diff','--cached','--name-status','HEAD','--').strip()
 assert git('rev-parse','HEAD').decode().strip()==d['real_index']['head']
 assert git('branch','--show-current').decode().strip()==d['real_index']['branch']
 print(json.dumps({'R12_packet':'PASS','source_delta':True,'prior_inventories':True,'reused_evidence_hashes':True,'real_index_unchanged_and_staged_delta_empty':True,'qualification':'independent bounded review required; exact proposed index is separately verified'}))

if __name__=='__main__':verify(Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).with_name('delta-review-packet.json'))
