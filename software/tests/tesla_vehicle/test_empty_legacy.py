"""Read-only unbound inert singleton allowlist; real POSIX faults, dummy VINs only."""
from pathlib import Path
import hashlib,itertools,json,os,shutil,subprocess,sys,tempfile
R=Path(__file__).resolve().parents[3]; T=R/'software/tests/tesla_vehicle'
from host_dependencies import cache, mbed
default={'enable':False,'vin':'','ble_address':'','ev_index':-1,'poll_interval':300,'wake_once':False}
# Synthetic test VIN only, never extracted from a vehicle or filesystem dump.
dummy_vin='5YJ3E1EA7KF000001'
inert=dict(default,vin=dummy_vin)
canonical='/config/tesla_vehicle_config'
# Kept explicit and independent of the implementation's table.
roots=['/config/tesla_ble_private_key','/config/tesla_ble_session_vcsec',
 '/config/tesla_ble_session_info','/config/tesla_ble_session_infotainment',
 '/config/tesla_ble_warp_ev_mac','/config/tesla_ble_vehicle_vin','/config/tesla_ble_authorized',
 '/config/tesla_ble_config','/config/.tesla_vehicle_config','/config/.tesla_ble_config',
 '/tesla_ble_bench_pair_record','/tesla_ble_bench_pair_exposed']
artifacts=[p+s for p in roots for s in ('','.tmp','.bak')]+[canonical+'.tmp',canonical+'.bak']
results=[]
with tempfile.TemporaryDirectory(prefix='empty-legacy-',dir=os.environ['TMPDIR']) as td:
 d=Path(td); store=d/'store';store.mkdir()
 flags=['-std=c++17','-Wall','-Wextra','-Werror','-fno-exceptions','-fno-rtti',
  '-DTESLA_BLE_CLOSE_QUARANTINE_TEST','-DTESLA_CLEAN_STORAGE_PREFIX="'+str(store)+'"',
  '-I'+str(R/'software/lib/TeslaBLEClean/include'),'-I'+str(mbed/'include')]
 if 'sanitize' in sys.argv: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
 src=[T/'test_empty_legacy.cpp',R/'software/src/modules/tesla_vehicle/tesla_ble_storage.cpp']+list((R/'software/lib/TeslaBLEClean/src').glob('*.cpp'))
 libs=[cache/'mbedtls'/p for p in ('library/libmbedcrypto.a','3rdparty/everest/libeverest.a','3rdparty/p256-m/libp256m.a')]
 subprocess.run(['g++',*flags,*map(str,src),*map(str,libs),'-Wl,--wrap=stat,--wrap=open,--wrap=fstat,--wrap=read,--wrap=close,--wrap=write,--wrap=unlink,--wrap=rename','-o',str(d/'test')],check=True)
 def run(name,content=None,extra=None,blocked=True,fault='none',target=canonical,directory=False):
  shutil.rmtree(store);store.mkdir()
  files={} if content is None else {canonical:content}
  files.update(extra or {})
  for path,data in files.items():
   p=store/path.lstrip('/');p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
  if directory:(store/target.lstrip('/')).mkdir(parents=True,exist_ok=True)
  def snapshot():return {str(p.relative_to(store)):('file',p.read_bytes(),p.stat().st_mode) if p.is_file() else ('dir',p.stat().st_mode) for p in store.rglob('*')}
  before=snapshot()
  p=subprocess.run([str(d/'test'),'blocked' if blocked else 'clear',fault,str(store)+target],capture_output=True,text=True)
  assert snapshot()==before, name+': filesystem changed'
  assert p.returncode==0, name+'\n'+p.stdout+p.stderr
  results.append({'name':name,'classification':'blocked' if blocked else 'clear','boots':3,'files_unchanged':True})
  print('PASS',name,flush=True)
 compact=json.dumps(default,separators=(',',':')).encode()
 unbound=json.dumps(inert,separators=(',',':')).encode()
 assert len(unbound)==111
 run('unbound-inert-dummy-vin-111-bytes',unbound,blocked=False)
 run('no-legacy',blocked=False)
 for variant,config in (('empty',default),('unbound',inert)):
  body=json.dumps(config,separators=(',',':')).encode()
  run(variant+'-exact-predecessor',body,blocked=False)
  run(variant+'-pretty-whitespace',json.dumps(config,indent=2).encode()+b'\r\n\t ',blocked=False)
  # Exercise every legal member order; no implicit field defaults.
  for i,keys in enumerate(itertools.permutations(config)):
   run(variant+'-field-order-'+str(i),json.dumps({k:config[k] for k in keys},separators=(',',':')).encode(),blocked=False)
  for k,values in {'enable':[True,0,'false',None], 'vin':[' ',None,0,False,[],{}],
   'ble_address':['01:02:03:04:05:06',' ',None,0], 'ev_index':[0,1,-2,'-1',None],
   'poll_interval':[30,301,0,'300',None], 'wake_once':[True,0,'false',None]}.items():
   for i,v in enumerate(values):run(variant+'-nondefault-'+k+'-'+str(i),json.dumps(dict(config,**{k:v})).encode())
  for k in config:
   run(variant+'-missing-'+k,json.dumps({n:v for n,v in config.items() if n!=k}).encode())
   run(variant+'-duplicate-'+k,body[:-1]+b','+json.dumps(k).encode()+b':'+json.dumps(config[k]).encode()+b'}')
  for k,v in [('ev_mac',''),('private_key',''),('unknown',False)]:run(variant+'-extra-'+k,json.dumps(dict(config,**{k:v})).encode())
  for name,data in {'empty':b'','object':b'{}','array':b'[]','null':b'null','truncated':body[:-1],
   'trailing-comma':body[:-1]+b',}','trailing-value':body+b'false','nul':body+b'\x00',
   'comment':b'/*empty*/'+body,'bom':b'\xef\xbb\xbf'+body,
   'escaped-name':body.replace(b'enable',b'enabl\\u0065'),
   'float-index':body.replace(b':-1',b':-1.0'),'float-interval':body.replace(b':300',b':300.0'),
   'exponent':body.replace(b':300',b':3e2'),'oversized':b' '*513+body,
   'non-json-whitespace':b'\v'+body}.items():run(variant+'-parse-'+name,data)
  for i,path in enumerate(artifacts):
   for data in (b'',b'\x00\x01\xff'):
    run(variant+'-artifact-'+str(i)+'-'+str(len(data)),body,{path:data})
  # Both legacy aliases, even exact-inert, remain outside the exception.
  run(variant+'-alias-alone',extra={'/config/tesla_ble_config':body})
  run(variant+'-both-configs',body,{'/config/tesla_ble_config':body})
  # Unrelated files and new multi-profile material are not singleton evidence.
  run(variant+'-profiles-coexist',body,{'/config/tesla_vehicle_profiles':b'{"entries":[]}',
   '/config/tesla_p00_112233445500_private_key':b'new profile material'},blocked=False)
  for i,path in enumerate([canonical,*artifacts]):
   run(variant+'-presence-error-'+str(i),body,fault='stat',target=path)
   run(variant+'-presence-nonregular-'+str(i),None if path==canonical else body,target=path,directory=True)
  for f in ('open','fstat','read','eof','truncated','close','grown','size-shrunk','nonregular'):
   run(variant+'-io-error-'+f,body,fault=f)
 # Prove artifact/error blocking does not depend on the singleton's existence.
 for i,path in enumerate(artifacts):
  run('absent-config-artifact-'+str(i),extra={path:b''})
  run('absent-config-presence-error-'+str(i),fault='stat',target=path)
 run('missing-config-presence-error',fault='stat')
 # Raw byte alphabet checked independently of the production validator: all
 # 256 byte values at every one of 17 positions, including NUL/quotes/escapes.
 allowed=b'0123456789ABCDEFGHJKLMNPRSTUVWXYZ'
 for pos in range(17):
  for byte in range(256):
   vin=bytearray(dummy_vin.encode());vin[pos]=byte
   run('vin-byte-'+str(pos)+'-'+str(byte),unbound.replace(dummy_vin.encode(),vin),blocked=byte not in allowed)
 for length in (*range(1,17),18,19,256):
  run('vin-length-'+str(length),unbound.replace(dummy_vin.encode(),b'X'*length))
 for pos in range(17):
  # Even an escape decoding to a valid ASCII VIN byte must be rejected.
  escaped=('\\u%04x'%ord(dummy_vin[pos])).encode()
  run('vin-escaped-ascii-'+str(pos),unbound.replace(dummy_vin.encode(),dummy_vin[:pos].encode()+escaped+dummy_vin[pos+1:].encode()))
 for i,value in enumerate(['5YJ3E1EA7KF00000\u00e9','5YJ3E1EA7KF00000\uff11','5YJ3E1EA7KF00000\u2028',dummy_vin.lower(),dummy_vin+' ', ' '+dummy_vin]):
  for escaped in (False,True):
   run('vin-unicode-or-padding-'+str(i)+'-'+str(escaped),json.dumps(dict(inert,vin=value),ensure_ascii=escaped).encode())
 for i,token in enumerate([b'null',b'false',b'true',b'12345678901234567',b'[]',b'{}',b'["'+dummy_vin.encode()+b'"]',b'{"vin":"'+dummy_vin.encode()+b'"}',b'"\\u0000"',b'"\\n"',b'"\\t"',b'"\\\\"',b'"\\\""']):
  run('vin-type-or-escape-'+str(i),unbound.replace(b'"'+dummy_vin.encode()+b'"',token))
 for k,old,tokens in [('enable',b'false',[b'0',b'False',b'falsee']),('wake_once',b'false',[b'0',b'False']),
  ('ev_index',b'-1',[b'-01',b'-1e0',b'-1.0',b'+-1',b'0']),('poll_interval',b'300',[b'0300',b'3e2',b'300.0',b'+300'])]:
  for i,token in enumerate(tokens):run('strict-token-'+k+'-'+str(i),unbound.replace(b'"'+k.encode()+b'":'+old,b'"'+k.encode()+b'":'+token))
 out={'suite':'unbound-inert-legacy-posix','sanitize':'sanitize' in sys.argv,'cases':len(results),'boots':sum(r['boots'] for r in results),'passed':len(results),'failed':0,'results':results}
 if os.environ.get('TESLA_TEST_REPORT_DIR'):
  (Path(os.environ['TESLA_TEST_REPORT_DIR'])/('EMPTY-LEGACY'+('-sanitize' if 'sanitize' in sys.argv else '')+'.json')).write_text(json.dumps(out,indent=2)+'\n')
 print('EMPTY_LEGACY_SUMMARY',json.dumps({k:v for k,v in out.items() if k!='results'}))
