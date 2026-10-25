"""RX notification bursts during an outstanding production WRITE.
Native worker -> NimBLE adapter -> RX queue -> FrameReceiver -> Vehicle; real
fixture crypto; deterministic schedule. Not an RF or vehicle recording."""
from pathlib import Path
import os,sys,tempfile,subprocess
T=Path(__file__).resolve().parent
MODES=os.environ.get('TESLA_RX_BURST_MODES','rx-sync-9-200 rx-sync-16-1000 rx-yield-12-200 rx-yield-16-1000 rx-paced-12-1000 rx-yield-12-6900-pair rx-sync-16-6900-pair rx-max-frame rx-split-timer rx-stale-mixed rx-overload rx-deferred-overflow rx-identity-change rx-bad-only rx-late-approval').split()
with tempfile.TemporaryDirectory(prefix='rx-burst-',dir=os.environ['TMPDIR']) as tmp:
 binary=Path(tmp)/'native';env={**os.environ,'TESLA_NIMBLE_MODULE_BINARY_OUT':str(binary),'TESLA_NIMBLE_MODULE_MODES':''}
 subprocess.run([sys.executable,str(T/'test_nimble_module.py'),*sys.argv[1:]],env=env,check=True)
 failed=[]
 for mode in MODES:
  print('RX_BURST_MODE',mode,flush=True)
  r=subprocess.run([str(binary),mode],env=env,timeout=120)
  print('RX_BURST_RESULT',mode,'PASS' if r.returncode==0 else 'FAIL exit=%d'%r.returncode,flush=True)
  if r.returncode:failed.append(mode)
 print('RX_BURST_SUMMARY executed=%d failed=%d %s'%(len(MODES),len(failed),' '.join(failed)),flush=True)
 raise SystemExit(1 if failed else 0)
