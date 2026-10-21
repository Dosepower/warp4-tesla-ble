"""Source-bound native VFS acceptance; caller already verified runtime roots.

Independent from the review SDK verifier. Tests are native/fault-injection only,
not device certification. No stored success receipt can skip execution.
"""
import os
from pathlib import Path
import subprocess
import sys


def run_acceptance(project, sources):
    project = Path(project)
    from installer_process import clean_environment
    env = clean_environment({
        "TESLA_BLE_HOST_FRAMEWORK_SOURCE": sources["builder"],
        "TESLA_BLE_HOST_IDF_SOURCE": sources["idf"],
        "TESLA_BLE_HOST_MBEDTLS_SOURCE": str(Path(sources["idf"]) / "components/mbedtls/mbedtls"),
        "TESLA_CLEAN_TEST_OUT": str(project / ".pio/installer-vfs-acceptance"),
        "TMPDIR": "/tmp", "LD_LIBRARY_PATH": "/snapshot/loader_runtime", "PATH": "/snapshot/system_tools:/snapshot/python/bin",
        "TESLA_HOST_ARDUINOJSON_SOURCE": "/snapshot/libdeps/ArduinoJson"})
    tests = project / "tests/tesla_vehicle"
    subprocess.run([sys.executable, str(tests / "test_r13_target_vfs.py"), "sanitize"], env=env, close_fds=True, check=True)
    subprocess.run(["/bin/bash",  str(tests / "cleanroom/run_test.sh"), "test_warp_littlefs", "sanitize"], env=env, close_fds=True, check=True)
