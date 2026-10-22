from pathlib import Path
import os, subprocess,sys,tempfile
R=Path(__file__).resolve().parents[3];T=R/'software/tests/tesla_vehicle';from host_dependencies import cache, mbed
with tempfile.TemporaryDirectory(prefix='multi-tesla-',dir=os.environ['TMPDIR']) as td:
 d=Path(td)
 flags=['-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fno-exceptions','-fno-rtti','-pthread','-DTESLA_BLE_MODULE_HOST_TEST','-DTESLA_BLE_HOST_FILESYSTEM','-DTESLA_BLE_MANUAL_VEHICLE_UI=1','-DBLUETOOTH_CONFIG_PATH="'+str(d/'config')+'"','-I'+str(T/'host'),'-I'+str(R/'software/lib/TeslaBLEClean/include'),'-I'+str(mbed/'include')]
 if 'sanitize' in sys.argv:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
 if os.environ.get('TESLA_MEMORY_PHASE_TEST')=='1':flags+=['-DWARP_TESLA_MEMORY_PHASES=1','-DTESLA_MEMORY_HOST_TEST=1']
 src=[T/'test_multi_tesla.cpp']+[R/'software/src/modules/tesla_vehicle'/n for n in ('tesla_ble_storage.cpp','tesla_ble_transport.cpp','tesla_ble_gatt.cpp','bluetooth_lifecycle.cpp')]+list((R/'software/lib/TeslaBLEClean/src').glob('*.cpp'))
 libs=[cache/'mbedtls'/n for n in ('library/libmbedcrypto.a','3rdparty/everest/libeverest.a','3rdparty/p256-m/libp256m.a')]
 subprocess.run(['g++',*flags,*map(str,src),*map(str,libs),'-o',str(d/'test')],check=True)
 for mode in ([x for x in sys.argv[1:] if x!='sanitize'] or ('guided','guided-adopt-disabled','guided-poll','guided-repair','guided-no-key','guided-generation','guided-old-generation','guided-wrong-config','guided-timeout','guided-ttl','guided-config','guided-pending','guided-cancel','guided-wrong-request','permission-only','unpair-revokes','idle-enabled','indicate-only','notify-only','indicate-and-notify','neither','wrong-descriptor','stale-setup','reconnect','reconnect-reverse','unbound-inert-legacy','empty-legacy','multi','pairing','generate','delete-first','legacy-key','legacy-session','legacy-config','corrupt-profiles','legacy-bench')):
  print('MULTI MODE',mode,flush=True)
  subprocess.run([str(d/'test'),mode],check=True,timeout=60)
