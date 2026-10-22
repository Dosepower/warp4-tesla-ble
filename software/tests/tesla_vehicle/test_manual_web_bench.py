#!/usr/bin/env python3
"""Host boundary tests for real manual-web module. No hardware/network."""
from pathlib import Path
import os, subprocess, sys, tempfile
R=Path(__file__).resolve().parents[3];T=R/'software/tests/tesla_vehicle'
from host_dependencies import cache, mbed
with tempfile.TemporaryDirectory(prefix='manual-web-',dir=os.environ['TMPDIR']) as td:
    d=Path(td)
    flags=['-std=c++17','-Wall','-Wextra','-Werror','-fno-exceptions','-fno-rtti','-pthread',
        '-DTESLA_BLE_MODULE_HOST_TEST','-DTESLA_BLE_HOST_FILESYSTEM','-DTESLA_BLE_R13_MASTER_BENCH=1',
        '-DTESLA_BLE_MANUAL_WEB_BENCH=1','-DTESLA_BLE_BENCH_ONLY=1','-DTESLA_BLE_IRAM_DIAGNOSTICS=1',
        '-DBLUETOOTH_CONFIG_PATH="'+str(d/'config')+'"',
        '-I'+str(T/'host'),'-I'+str(R/'software/lib/TeslaBLEClean/include'),'-I'+str(mbed/'include')]
    if 'sanitize' in sys.argv: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
    src=[T/'test_manual_web_bench.cpp']+[R/'software/src/modules/tesla_vehicle'/n for n in ('tesla_ble_storage.cpp','tesla_ble_transport.cpp','tesla_ble_gatt.cpp','bluetooth_lifecycle.cpp')]+list((R/'software/lib/TeslaBLEClean/src').glob('*.cpp'))
    libs=[cache/'mbedtls'/n for n in ('library/libmbedcrypto.a','3rdparty/everest/libeverest.a','3rdparty/p256-m/libp256m.a')]
    cmd=['g++',*flags,*map(str,src),*map(str,libs),'-Wl,--wrap=open,--wrap=write,--wrap=read,--wrap=fsync,--wrap=close,--wrap=rename,--wrap=unlink','-o',str(d/'test')]
    subprocess.run(cmd,check=True)
    # Native facade destruction intentionally pins failed worker ownership;
    # the existing fail-closed contract does not reclaim until device reset.
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
    for mode in ('ok','persist-fail','init-fail','teardown-fail','resource-fail'):
        subprocess.run([str(d/'test'),mode],env=env,check=True,timeout=15)
print('MANUAL_WEB_HOST_CASES=5 PASS')
