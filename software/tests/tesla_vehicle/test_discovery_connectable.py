"""Discovery: VIN-matching but non-connectable Tesla beacon must be reported as present-but-not-connectable.

Production path: tesla_vehicle/scan command -> BluetoothLifecycle -> TeslaNimbleCentral::scan ->
TeslaBleDiscovery -> publish_discovery (native host build of the real sources).
Official reference: teslamotors/vehicle-command a4b43c1e pkg/connector/ble/ble.go
(tryToConnect: !target.Connectable -> ErrMaxConnectionsExceeded).
Controls: connectable match/adoption admission unchanged; non-connectable never admitted.

usage: test_discovery_connectable.py [sanitize] [--expect-red]
"""
from pathlib import Path
import os, sys, tempfile, subprocess
T = Path(__file__).resolve().parent
expect_red = '--expect-red' in sys.argv
args = [a for a in sys.argv[1:] if a != '--expect-red']
with tempfile.TemporaryDirectory(prefix='discovery-connectable-', dir=os.environ['TMPDIR']) as tmp:
    binary = Path(tmp) / 'native'
    env = {**os.environ, 'TESLA_NIMBLE_MODULE_BINARY_OUT': str(binary), 'TESLA_NIMBLE_MODULE_MODES': ''}
    subprocess.run([sys.executable, str(T / 'test_nimble_module.py'), *args], env=env, check=True)
    r = subprocess.run([str(binary), 'discovery-connectable'], env=env, timeout=300, capture_output=True, text=True)
    sys.stdout.write(r.stdout); sys.stderr.write(r.stderr)
    red = r.returncode == 3 and 'DISCOVERY_CONNECTABLE_RED' in r.stdout
    passed = r.returncode == 0 and 'DISCOVERY_CONNECTABLE_PASS' in r.stdout
    ok = red if expect_red else passed
    print('DISCOVERY_CONNECTABLE_RESULT', 'sanitize' if 'sanitize' in args else 'normal', 'rc=%d' % r.returncode,
          'RED' if red else 'PASS' if passed else 'FAIL', 'OK' if ok else 'UNEXPECTED', flush=True)
    raise SystemExit(0 if ok else 1)
