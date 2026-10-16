#!/usr/bin/env python3
"""No generic file/core dump API may export bench private material."""
import subprocess
import re
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
class BenchPairPrivacy(unittest.TestCase):
    def test_private_material_export_routes_excluded(self):
        for name,needle in [('debug/debug.cpp','"/debug/fs/*"'),('coredump/coredump.cpp','"/coredump/coredump.elf"'),('esp32_common/esp32_common.cpp','"/esp32/dump_flash"')]:
            source=(ROOT/'software/src/modules'/name).read_text()
            source='\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
            macros=set(re.findall(r'^#(?:if|elif).*?\b([A-Z][A-Z0-9_]*)\(',source,re.M))
            source='\n'.join('#define '+m+'(...) 0' for m in macros)+'\n'+source
            def preprocess(defines):
                p=subprocess.run(['g++','-E','-P','-x','c++','-','-DDEBUG_FS_ENABLE',*['-D'+d for d in defines]],input=source,capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stderr)
                return p.stdout
            self.assertNotIn(needle,preprocess(('TESLA_BLE_BENCH_ONLY=1','TESLA_BLE_IRAM_DIAGNOSTICS=1')))
            for defines in ((),('TESLA_BLE_BENCH_ONLY=1',),('TESLA_BLE_IRAM_DIAGNOSTICS=1',),('TESLA_BLE_BENCH_ONLY=0','TESLA_BLE_IRAM_DIAGNOSTICS=1')):
                self.assertIn(needle,preprocess(defines))
if __name__=='__main__': unittest.main()
