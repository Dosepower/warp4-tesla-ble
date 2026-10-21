"""Run real native backend with controlled NimBLE SDK/RTOS boundaries, no radio."""
from pathlib import Path
import subprocess,tempfile,os,sys
S=Path(__file__).resolve().parents[2];T=S/'tests/tesla_vehicle'
with tempfile.TemporaryDirectory(prefix='nimble-native-',dir=os.environ['TMPDIR']) as td:
 flags=['-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-pthread','-DTESLA_NIMBLE_HOST_TEST=1','-DTESLA_BLE_USE_NIMBLE=1','-I'+str(T/'host'),'-I'+str(S/'src/modules/tesla_vehicle')]
 if 'sanitize' in sys.argv:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
 exe=Path(td)/'test';subprocess.run(['g++',*flags,str(T/'test_nimble_native.cpp'),str(S/'src/modules/tesla_vehicle/tesla_nimble_central.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=30)
