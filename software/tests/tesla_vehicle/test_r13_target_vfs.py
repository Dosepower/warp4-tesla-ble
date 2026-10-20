#!/usr/bin/env python3
"""Exercise the production POSIX adapter with synthetic filesystem faults.

This is not an attestation for a new SDK. The source-bound close-ownership
harness in run_host_tests.sh must also pass; native POSIX alone is insufficient.
The actual target headers are compiled by the source-review firmware build.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
MOD = ROOT/'software/src/modules/tesla_vehicle'
SHIM = '''#pragma once
extern "C" bool r13_backend_mounted();
namespace fs { class LittleFSFS {
public:
    const char *mountpoint() { return r13_backend_mounted() ? "/data" : nullptr; }
}; }
inline fs::LittleFSFS LittleFS;
'''

def main():
    with tempfile.TemporaryDirectory(prefix='secure-target-vfs-', dir=os.environ['TMPDIR']) as td:
        out=Path(td); shim=out/'shim'; shim.mkdir()
        (shim/'FS.h').write_text(SHIM)
        (shim/'LittleFS.h').write_text('#pragma once\n#include "FS.h"\n')
        (shim/'esp_littlefs.h').write_text('#pragma once\nextern "C" bool r13_backend_mounted();\ninline bool esp_littlefs_mounted(const char *) { return r13_backend_mounted(); }\n')
        flags=['-std=c++17','-Wall','-Wextra','-Werror','-DESP_PLATFORM','-I'+str(shim),'-I'+str(MOD)]
        if 'sanitize' in sys.argv:
            flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
        wrap='-Wl,'+','.join('--wrap='+s for s in ('open','close','fstat','read','write','fsync','ftruncate','rename','unlink','opendir','readdir','closedir'))
        binary=out/'contract'
        subprocess.run(['g++',*flags,'-DWARP_BLUETOOTH_LITTLEFS_BINDING=0x52313302',str(Path(__file__).with_suffix('.cpp')),wrap,'-o',str(binary)],check=True)
        cases=('missing','save','journal','bak-open','bak-open-nomem','bak-open-emfile','bak-open-noent','canonical-open','mount','ancestor','ancestor-missing','directory','read','read-eof','size','bad-size','long-prefix','short-read','readdir','closedir','close','write','fsync','rename','unlink','rollback','positions')
        for case in cases:
            fixture=out/case; fixture.mkdir()
            subprocess.run([str(binary),str(fixture),case],check=True)
        subprocess.run(['g++',*flags,str(Path(__file__).with_suffix('.cpp')),wrap,'-o',str(binary)],check=True)
        fixture=out/'unbound'; fixture.mkdir()
        subprocess.run([str(binary),str(fixture),'unbound'],check=True)
        print('Production VFS adapter: 27 fault/transaction cases and unbound rejection passed')
    return 0
if __name__=='__main__': raise SystemExit(main())
