#!/usr/bin/env python3
"""Native source-bound VFS acceptance for a verified WARP4 SDK; no device IO.

Always rerun: no checkpoint hash or stale success receipt grants support.
This validates fault semantics, not flash timing or power-loss durability.
"""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
S=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('tesla_sdk',S/'lib-builder/tesla_sdk.py')
sdk=importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)
package=Path(os.environ['TESLA_BLE_FRAMEWORK_LIBS'])
sdk.verify_package(package, policy=os.environ.get('TESLA_BLE_SDK_POLICY', 'tesla-psram-first-v1'))
root=Path(os.environ.get('TESLA_BLE_SDK_SOURCE_ROOT', S/'.pio/tesla-sdk'))
if not root.is_absolute() or package != root/'builder/out/tools/esp32-arduino-libs':
    raise ValueError('VFS acceptance requires the exact explicit SDK source/package tree')
# These are exactly the sources used by the pinned SDK preparation script.
env=dict(os.environ,
    TESLA_BLE_HOST_FRAMEWORK_SOURCE=str(root/'builder'),
    TESLA_BLE_HOST_IDF_SOURCE=str(root/'esp-idf-secure'),
    TESLA_BLE_HOST_MBEDTLS_SOURCE=str(root/'esp-idf-secure/components/mbedtls/mbedtls'),
    TESLA_CLEAN_TEST_OUT=os.environ.get('TESLA_VFS_TEST_OUT', str(S/'.pio/tesla-vfs-acceptance')))
T=S/'tests/tesla_vehicle'
subprocess.run([sys.executable,str(T/'test_r13_target_vfs.py'),'sanitize'],env=env,check=True)
subprocess.run(['bash',str(T/'cleanroom/run_test.sh'),'test_warp_littlefs','sanitize'],env=env,check=True)
print('WARP4 native VFS fault and real LittleFS/IDF close-ownership acceptance passed; not hardware certification')
