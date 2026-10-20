#!/usr/bin/env python3
"""Current WARP4 host/frontend wrappers; no firmware upload or device access.

The legacy SDK/image/packaging checks below require deliberately unported
canonical testboard tools. They are not coverage for the real WARP4 path.
Run run_host_tests.sh separately for C++ core/helper and ASan/UBSan coverage.
"""
from pathlib import Path
import json
import subprocess
import sys

T=Path(__file__).resolve().parent
S=T.parents[1]
LEGACY_ONLY={
    'test_canonical_storage.py': 'canonical app-only mount/layout is not a WARP4 production path',
    'test_iram_bench_http_auth.py': 'old UART/testboard HTTP fixture and paths',
    'test_iram_bench_memory.py': 'old diagnostic allocator policy, not enabled on WARP4',
    'test_iram_bench_pair_privacy.py': 'old testboard pairing packet/tool paths',
    'test_iram_bluedroid_memory.py': 'old diagnostic SDK archive hashes/tools',
    'test_iram_diagnostics.py': 'old testboard diagnostic build hooks',
    'test_iram_map_report.py': 'old map reporter; replaced by test_resource_report.py',
    'test_r9_driver_confinement.py': 'obsolete canonical build/export tool security tests',
    'test_r10_confinement.py': 'obsolete publication/build tool security tests',
    'test_r11_confinement.py': 'obsolete publication/export tool security tests',
}

def main():
    out=S/'.pio/tesla-host-secure'
    out.mkdir(parents=True,exist_ok=True)
    results={}
    for path in sorted(T.glob('test_*.py'))+sorted(T.glob('test_*.mjs')):
        if path.name in LEGACY_ONLY:
            print('EXCLUDED',path.name,LEGACY_ONLY[path.name],flush=True)
            continue
        print('RUN',path.name,flush=True)
        with (out/(path.name+'.log')).open('w') as log:
            code=subprocess.run(['node' if path.suffix=='.mjs' else sys.executable,str(path)],stdout=log,stderr=subprocess.STDOUT).returncode
        results[path.name]=code
        print('PASS' if code==0 else 'FAIL',path.name,flush=True)
    (out/'upstream-wrapper-results.json').write_text(json.dumps({'results':results,'excluded':LEGACY_ONLY},indent=2)+'\n')
    failed=[name for name,code in results.items() if code]
    print('Wrappers:',len(results),'passed:',len(results)-len(failed),'failed:',failed,'legacy exclusions:',len(LEGACY_ONLY))
    return bool(failed)
if __name__=='__main__': raise SystemExit(main())
