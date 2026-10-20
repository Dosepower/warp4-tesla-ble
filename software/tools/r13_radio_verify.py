#!/usr/bin/env python3
"""Offline regression execution; outputs only in new radio evidence/sandbox."""
import argparse
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import time
from r13_radio_spike import ROOT,OUT,IDF,env_offline

resource.setrlimit(resource.RLIMIT_CORE,(0,0))
D=OUT/'work/software'
V=OUT/'validation'
V.mkdir(exist_ok=True)
TMP=V/'tmp'
TMP.mkdir(exist_ok=True)
ENV=env_offline()
ENV.update(TMPDIR=str(TMP),TESLA_BLE_HOST_MBEDTLS_SOURCE=str(IDF/'components/mbedtls/mbedtls'),
           TESLA_BLE_HOST_FRAMEWORK_SOURCE=str(OUT/'work/sdk-bluedroid'),
           TESLA_CLEAN_TEST_OUT=str(V/'native'),TESLA_CLEAN_MBEDTLS_BUILD=str(V/'mbedtls'))
# Sanitized executables must load libasan first. The enclosing network namespace
# already denies all external IP access, without injecting a library ahead of ASan.
ENV.pop('LD_PRELOAD',None)


def run(name,cmd,cwd=D,timeout=550):
    label=name
    attempt=1
    while (V/(name+'.log')).exists():
        name=label+'-attempt'+str(attempt)
        attempt+=1
    log=V/(name+'.log')
    started=time.monotonic()
    with log.open('w') as f:
        try:
            p=subprocess.run(['unshare','-Urn',*cmd],cwd=cwd,env=ENV,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
            code=p.returncode
        except subprocess.TimeoutExpired:
            code=124
    record={'name':name,'command':cmd,'cwd':str(cwd),'exit':code,'seconds':time.monotonic()-started,
            'offline':True,'log':str(log),'fno_exceptions_native':True}
    (V/(name+'-result.json')).write_text(json.dumps(record,indent=2)+'\n')
    print(name, 'exit='+str(code), 'seconds='+str(round(record['seconds'],2)),flush=True)
    return record


def main():
    p=argparse.ArgumentParser()
    p.add_argument('mode',choices=['spike','python','native','sanitize','frontend','generators','scenarios'])
    mode=p.parse_args().mode
    # Sync ONLY new extension tests. Original and copied core stay untouched.
    shutil.copy2(ROOT/'software/tests/tesla_vehicle/test_radio_spike.py',D/'tests/tesla_vehicle/test_radio_spike.py')
    if mode=='spike':
        run('spike-red-green',['python3','-B','tests/tesla_vehicle/test_radio_spike.py'])
    elif mode=='python':
        run('python-full',['python3','-B','-m','unittest','discover','-v','-s','tests/tesla_vehicle','-p','test_*.py'])
    elif mode in ('native','sanitize'):
        entries=sorted((D/'tests/tesla_vehicle/cleanroom').glob('test_*.cpp'))
        results=[]
        skipped=[]
        for source in entries:
            if 'astra' in source.name:
                skipped.append(source.name);continue
            cmd=['bash','tests/tesla_vehicle/cleanroom/run_test.sh',source.stem]
            if mode=='sanitize':cmd+=['sanitize']
            results.append(run(mode+'-'+source.stem,cmd))
        summary=V/(mode+'-summary.json')
        index=1
        while summary.exists():
            summary=V/(mode+'-summary-attempt'+str(index)+'.json');index+=1
        summary.write_text(json.dumps({'results':results,'skipped_astra':skipped,
             'passed':sum(x['exit']==0 for x in results),'failed':sum(x['exit']!=0 for x in results)},indent=2)+'\n')
    elif mode=='scenarios':
        import re
        import shlex
        # All run_suite.sh post-compilation scenario loops, including nested R6.
        text=(D/'tests/tesla_vehicle/cleanroom/run_suite.sh').read_text()
        groups=re.findall(r'for mode in (.*?); do\n(.*?)\ndone',text,re.S)
        results=[]
        for suffix in ('','-sanitized'):
            for modes,body in groups:
                entries=re.findall(r'/((?:test_warp_)[a-z0-9_]+)\$',body)
                if 'for entry in' in body:
                    entries=shlex.split(re.search(r'for entry in (.*?); do',body)[1])
                for case in shlex.split(modes):
                    for entry in sorted(set(entries)):
                        results.append(run('scenario-'+entry+suffix+'-'+case,[str(V/'native'/(entry+suffix)),case],timeout=30))
            results.append(run('scenario-disconnect-close'+suffix,[str(V/'native'/('test_warp_module'+suffix)),'r3-disconnect-close'],timeout=30))
        (V/'scenario-summary.json').write_text(json.dumps({'results':results,'passed':sum(r['exit']==0 for r in results),'failed':sum(r['exit']!=0 for r in results)},indent=2)+'\n')
    elif mode=='frontend':
        run('web-actions',['node','tests/tesla_vehicle/test_web_actions.mjs'])
        run('web-integration',['node','tests/tesla_vehicle/test_web_integration.mjs'])
        run('typescript-preprocessed',['web/node_modules/.bin/tsc','--noEmit','--project','web/src_tfpp/tsconfig.json'])
    else:
        for name in ('make_vectors','wire_vectors','test_safety','test_production_binding'):
            run('generator-'+name,['python3','-B','tests/tesla_vehicle/cleanroom/'+name+'.py'])

if __name__=='__main__':main()
