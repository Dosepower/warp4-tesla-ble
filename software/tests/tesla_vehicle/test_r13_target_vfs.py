#!/usr/bin/env python3
"""Run the production ESP_PLATFORM adapter, then compile with bound real headers."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import runpy
sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from r13_vfs_binding import BINDING as EXPECTED_BINDING, CORE_HASHES, SDK_HASHES, verified
ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT/'software/packages/esp32-arduino-libs#tf-esp-secure-20260922_4f0ab20592113e12c1792ba670c48aad04ceb288/esp32'
CORE = Path('/home/hermes/.platformio/packages/framework-arduinoespressif32')
CC = Path('/home/hermes/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-g++')
MOD = ROOT/'software/src/modules/tesla_vehicle'
BINDING = '-DWARP_BLUETOOTH_LITTLEFS_BINDING=0x52313302'
SHIM = '''#pragma once
extern "C" bool r13_backend_mounted();
namespace fs { class LittleFSFS {
public:
    const char *mountpoint() { return r13_backend_mounted() ? "/data" : nullptr; }
}; }
inline fs::LittleFSFS LittleFS;
'''

def main():
    assert EXPECTED_BINDING == 0x52313302
    assert verified(CORE, SDK.parent), 'The real compile must use the attested packages'
    assert not verified(CORE, SDK/'absent'), 'Unknown packages must not bind'
    failures = []
    with tempfile.TemporaryDirectory(prefix='r13-target-vfs-', dir=os.environ['TMPDIR']) as td:
        out = Path(td); shim = out/'shim'; shim.mkdir()
        bound_core, bound_sdk = out/'core', out/'sdk'
        for actual, dest, manifest in ((CORE, bound_core, CORE_HASHES), (SDK.parent, bound_sdk, SDK_HASHES)):
            for relative in manifest:
                path = dest/relative; path.parent.mkdir(parents=True,exist_ok=True)
                path.write_bytes((actual/relative).read_bytes())
        class GuardEnv:
            def __init__(self): self.defines = []
            def subst(self, _): return str(ROOT/'software')
            def PioPlatform(self): return self
            def get_package_dir(self, name): return str(bound_core if name == 'framework-arduinoespressif32' else bound_sdk)
            def Append(self, **values): self.defines += values['CPPDEFINES']
        guard = GuardEnv()
        runpy.run_path(str(ROOT/'software/r13_vfs_guard.py'),init_globals={'env':guard,'Import':lambda _:None})
        assert guard.defines == [('WARP_BLUETOOTH_LITTLEFS_BINDING',hex(EXPECTED_BINDING))]
        changed = bound_core/'libraries/LittleFS/src/LittleFS.cpp'
        changed.write_bytes(changed.read_bytes()+b'\n// changed backend\n')
        assert not verified(bound_core, bound_sdk)
        guard = GuardEnv()
        runpy.run_path(str(ROOT/'software/r13_vfs_guard.py'),init_globals={'env':guard,'Import':lambda _:None})
        assert not guard.defines
        print('production build guard: exact artifacts bind; changed backend refuses')
        (shim/'FS.h').write_text(SHIM)
        (shim/'LittleFS.h').write_text('#pragma once\n#include "FS.h"\n')
        (shim/'esp_littlefs.h').write_text('#pragma once\nextern "C" bool r13_backend_mounted();\ninline bool esp_littlefs_mounted(const char *) { return r13_backend_mounted(); }\n')
        flags = ['-std=c++17','-Wall','-Wextra','-Werror','-DESP_PLATFORM','-I'+str(shim),'-I'+str(MOD)]
        if 'sanitize' in sys.argv:
            flags += ['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
        wrap = '-Wl,'+','.join('--wrap='+s for s in ('open','close','fstat','read','write','fsync','ftruncate','rename','unlink','opendir','readdir','closedir'))
        binary = out/'contract'
        subprocess.run(['g++',*flags,BINDING,str(Path(__file__).with_suffix('.cpp')),wrap,'-o',str(binary)],check=True)
        cases = ('missing','save','journal','bak-open','bak-open-nomem','bak-open-emfile','bak-open-noent','canonical-open','mount','ancestor','ancestor-missing','directory','read','read-eof','size','bad-size','long-prefix','short-read','readdir','closedir','close','write','fsync','rename','unlink','rollback','positions')
        for case in cases:
            fixture = out/case; fixture.mkdir()
            result = subprocess.run([str(binary), str(fixture), case],capture_output=True,text=True)
            print(case, 'exit', result.returncode, result.stdout, result.stderr, flush=True)
            if result.returncode: failures.append(case)
        subprocess.run(['g++',*flags,str(Path(__file__).with_suffix('.cpp')),wrap,'-o',str(binary)],check=True)
        fixture = out/'unbound'; fixture.mkdir()
        result = subprocess.run([str(binary), str(fixture), 'unbound'],capture_output=True,text=True)
        print('unbound exit',result.returncode,result.stderr,flush=True)
        if result.returncode: failures.append('unbound')
        probe = out/'probe.cpp'
        probe.write_text('#include "bluetooth_config_store.h"\nbool exercise(bool &v) { BluetoothConfigStore s; return s.load(v) && s.save(v); }\n')
        paths = (SDK/'flags/includes').read_text().split('-iwithprefixbefore ')
        inc = [MOD, CORE/'cores/esp32',CORE/'variants/esp32',CORE/'libraries/FS/src',CORE/'libraries/LittleFS/src',SDK/'qio_qspi/include']
        inc += [SDK/'include'/p.strip() for p in paths if p.strip() and not p.strip().startswith('bt/')]
        cmd = [str(CC),'-std=gnu++2b','-Wall','-Wextra','-Werror','-fno-exceptions','-fno-rtti','-DARDUINO=10819','-DESP_PLATFORM','-DARDUINO_ARCH_ESP32',BINDING,*('-I'+str(p) for p in inc),'-c',str(probe),'-o',str(out/'probe.o')]
        subprocess.run(cmd,check=True)
        print('real Xtensa production adapter compile exit 0; failures:',failures)
    return int(bool(failures))
if __name__ == '__main__': raise SystemExit(main())
