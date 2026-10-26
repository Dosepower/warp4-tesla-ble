"""RX notification bursts during an outstanding production WRITE.
Native worker -> NimBLE adapter -> RX queue -> FrameReceiver -> Vehicle; real
fixture crypto; deterministic schedule. Not an RF or vehicle recording."""
from pathlib import Path
import os,sys,tempfile,subprocess
T=Path(__file__).resolve().parent
BASE='rx-sync-9-200 rx-sync-16-1000 rx-yield-12-200 rx-yield-16-1000 rx-paced-12-1000 rx-yield-12-6900-pair rx-sync-16-6900-pair rx-max-frame rx-split-timer rx-stale-mixed rx-overload rx-deferred-overflow rx-identity-change rx-bad-only rx-late-approval'.split()
# H1 fragmentation matrix: notification PAYLOAD sizes (not ATT MTU) at chunk
# boundaries (22-byte queue chunk), the preferred-MTU payload 253 and the 512
# callback bound; -p = stream split, -f = each framed message split separately.
PAYLOADS=(20,22,23,24,43,44,45,253,512)
MATRIX=['rx-capacity-units']+[m%p for p in PAYLOADS for m in ('rx-max-frame-p%d','rx-max-frame-f%d','rx-max-frame-sync-p%d','rx-max-frame-sync-f%d','rx-sync-16-1000-p%d','rx-yield-12-6900-pair-f%d')]
MATRIX+=['rx-late-approval-p23','rx-late-approval-f45','rx-stale-mixed-p23','rx-overload-p23','rx-overload-p253','rx-deferred-overflow-f23','rx-yield-0-200-p2','rx-tiny-max-frame']
MODES=os.environ.get('TESLA_RX_BURST_MODES',' '.join(BASE+MATRIX)).split()
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
