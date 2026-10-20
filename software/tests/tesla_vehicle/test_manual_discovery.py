#!/usr/bin/env python3
"""Native real discovery state machine. No network or target access."""
from pathlib import Path
import os,subprocess,tempfile,sys
R=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='manual-discovery-',dir=os.environ['TMPDIR']) as td:
 exe=Path(td)/'test'
 flags=['-std=c++17','-Wall','-Wextra','-Werror','-pthread','-fno-exceptions','-fno-rtti']
 if 'sanitize' in sys.argv:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
 subprocess.run(['g++',*flags,str(R/'software/tests/tesla_vehicle/test_manual_discovery.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=20)
