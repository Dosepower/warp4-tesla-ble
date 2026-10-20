"""Strict SDK identity gate for raw bench compile; no merge/export/package."""
from pathlib import Path
import sys
Import('env')
sys.path.insert(0, str(Path(env.subst('$PROJECT_DIR')) / 'tools'))
from bench_build_identity import verify_sdk
sdk = env.PioPlatform().get_package_dir('framework-arduinoespressif32-libs')
verify_sdk(sdk, env.GetProjectOption('custom_bench_builder'))
