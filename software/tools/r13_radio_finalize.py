#!/usr/bin/env python3
"""Write-once final Phase-A proof and evidence sealing; no packaging."""
import argparse
import collections
import json
import re
import shutil
import subprocess
from pathlib import Path
from r13_radio_spike import ROOT,OUT,BASE,IDF,sha,walk


def save(name,data):
    p=OUT/name
    assert not p.exists(), 'Evidence is write-once: '+str(p)
    p.parent.mkdir(parents=True,exist_ok=True)
    p.write_text(json.dumps(data,indent=2)+'\n' if not isinstance(data,str) else data)


def prove():
    measured=json.loads((OUT/'measured-summary.json').read_text())
    history=json.loads((ROOT/'offline-r13-extension-only-lean-off-sol/footprint-initial-comparison.json').read_text())
    rows={}
    for name in ('C1','C2','lean'):
        r=history[name]
        rows[name]={'iram':r['sections']['.iram0.text']['size'],'data':r['sections']['.dram0.data']['size'],
            'bss':r['sections']['.dram0.bss']['size'],'heap_start':r['heap_start'],
            'init_array':r['init_array_count'],'bt_archive_members':r['bt_archive_count']}
    controller=[]
    binding={}
    for kind,r in measured.items():
        audit=json.loads((OUT/(kind+'-elf-map-audit.json')).read_text())
        rows[kind]={'iram':r['sections']['.iram0.text'],'data':r['sections']['.dram0.data'],
            'bss':r['sections']['.dram0.bss'],'heap_start':r['heap_start'],'init_array':r['init_array_count'],
            'bt_archive_members':r['bt_archive_members_count'],'libbtdm_iram':r['iram_archive_bytes']['libbtdm_app.a'],
            'libbt_iram':r['iram_archive_bytes']['libbt.a']}
        rows[kind]['iram_vs_C2']=rows[kind]['iram']-rows['C2']['iram']
        rows[kind]['iram_vs_C1']=rows[kind]['iram']-rows['C1']['iram']
        rows[kind]['iram_vs_lean']=rows[kind]['iram']-rows['lean']['iram']
        assert sum(r['iram_archive_bytes'].values())<=rows[kind]['iram']
        histogram=collections.Counter()
        for c in audit['contributions']:
            if '/libbtdm_app.a(' in c['object']:
                histogram[(c['object'].split('/libbtdm_app.a(')[1],c['section'],c['size'])]+=1
        controller.append(histogram)
        text=(OUT/'candidate-UNTESTED'/kind/'firmware.map').read_text().split('Linker script and memory map',1)[1]
        mqtt=[]
        for line in text.splitlines():
            m=re.match(r'^\s+(?:\.\S+\s+)?(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S*/modules/mqtt/\S+)',line)
            if m and int(m[1],16)>=0x3f000000 and int(m[2],16)>0:
                mqtt.append({'address':m[1],'size':int(m[2],16),'object':m[3]})
        assert mqtt
        source_file=OUT/('app-bluedroid-attempt1-source-binding.json' if kind=='bluedroid' else 'app-nimble-source-binding.json')
        source=json.loads(source_file.read_text())
        relevant={rel:h for rel,h in source.items() if rel.startswith(('src/modules/tesla_vehicle/','lib/TeslaBLEClean/','web/src/modules/tesla_vehicle/')) or rel in ('warp4_tesla_ble.ini','r13_lean_link.py','r13_vfs_guard.py')}
        mismatch=[rel for rel,h in relevant.items() if not (ROOT/'software'/rel).is_file() or sha(ROOT/'software'/rel)!=h]
        assert not mismatch,mismatch
        missing=[]; changed=[]
        d=OUT/'work/software'
        generated={'src/build.cpp','src/index_html.digest','src/index_html.embedded.cpp'}
        for p in walk(ROOT/'software/src'):
            rel=str(p.relative_to(ROOT/'software'))
            if rel.startswith('src/modules/tesla_vehicle/') or '/generated/' in rel or rel in generated:continue
            if not (d/rel).is_file():missing.append(rel)
            elif sha(p)!=sha(d/rel):changed.append(rel)
        assert not missing and not changed,(missing,changed)
        binding[kind]={'current_compiled_extension_inputs':len(relevant),'mismatches':mismatch,
           'core_missing':missing,'core_changed':changed,'private_generated_exceptions':sorted(generated),
           'mqtt_hashes':{str(p.relative_to(ROOT)):sha(p) for p in walk(ROOT/'software/src/modules/mqtt')},
           'mqtt_live_input_sections':mqtt,'mqtt_live_total_bytes':sum(x['size'] for x in mqtt)}
        sdk=OUT/'work'/('sdk-'+kind)
        shutil.copy2(sdk/'sdkconfig',OUT/('effective-'+kind+'.sdkconfig'))
    assert controller[0]==controller[1], 'Controller input section closure differs'
    save('FINAL-comparison.json',rows)
    save('FINAL-current-source-and-mqtt-binding.json',binding)
    save('FINAL-controller-identity.json',{'same_input_section_multiset':True,'input_sections':sum(controller[0].values()),
        'iram_bytes':rows['nimble']['libbtdm_iram'],'archive_sha256':sha(BASE/'esp32/ld/libbtdm_app.a'),
        'contributions':[{'member':k[0],'section':k[1],'size':k[2],'count':v} for k,v in sorted(controller[0].items())]})
    old=json.loads((ROOT/'offline-r13-extension-only-lean-off-sol/allowed-changed-files.json').read_text())
    delta=[rel for rel,h in old.items() if not (ROOT/rel).exists() or sha(ROOT/rel)!=h]
    save('FINAL-vs-failed-lean-source.json',{'changed_existing_files':delta,
        'production_transport_lifecycle_unchanged':all(not x.startswith('software/src/modules/tesla_vehicle/') for x in delta)})
    commands=[['git','show-ref','--heads'],['git','rev-parse','origin/main'],
              ['git','show','origin/main:TESLA_BLE_HANDOFF.md'],
              ['git','show','origin/main:software/lib-builder/defconfig.esp32ethernetbrick']]
    text=''
    for cmd in commands:
        p=subprocess.run(cmd,cwd=ROOT,text=True,capture_output=True)
        text+='COMMAND '+repr(cmd)+'\nEXIT '+str(p.returncode)+'\n'+p.stdout+p.stderr+'\n'
    save('old-github-local-ref-audit.txt',text)
    paths=['components/bt/controller/CMakeLists.txt','components/bt/controller/esp32/Kconfig.in',
           'components/bt/host/nimble/Kconfig.in','components/bt/controller/linker_rw_bt_controller.lf']
    for name in paths:
        p=IDF/name
        q=OUT/'sdk-source-proof'/name
        q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
    v=OUT/'validation'
    status={}
    for mode,file in (('normal','native-summary-attempt1.json'),('sanitized','sanitize-summary.json'),('scenarios','scenario-summary.json')):
        s=json.loads((v/file).read_text())
        status[mode]={'passed':s['passed'],'failed':s['failed'],
              'failures':[r['name'] for r in s['results'] if r['exit']!=0],
              'skipped_astra':s.get('skipped_astra',[])}
    log=(v/'python-full-attempt1.log').read_text()
    status['python']={'ran':int(re.search(r'Ran (\d+) tests',log)[1]),
        'result':re.search(r'^FAILED.*$',log,re.M)[0],
        'failures':re.findall(r'^(?:FAIL|ERROR):.*$',log,re.M)}
    for name in ('spike-red-green-attempt1','web-actions-attempt1','web-integration-attempt1','typescript-preprocessed',
                 'generator-make_vectors','generator-wire_vectors','generator-test_safety','generator-test_production_binding'):
        status[name]=json.loads((v/(name+'-result.json')).read_text())['exit']
    save('FINAL-validation-summary.json',status)
    print(json.dumps({'comparison':rows,'tests':{k:v for k,v in status.items() if k!='python'}},indent=2))


def seal():
    assert json.loads((OUT/'guard-pre.json').read_text())==json.loads((OUT/'guard-post.json').read_text())
    snapshot=OUT/'extension-source-snapshot'
    paths=['software/warp4_tesla_ble.ini','software/r13_lean_link.py',
           'software/src/modules/tesla_vehicle/tesla_ble_radio_spike.cpp','software/tests/tesla_vehicle/test_radio_spike.py',
           'software/tools/r13_radio_spike.py','software/tools/r13_radio_measure.py','software/tools/r13_radio_verify.py',
           'software/tools/r13_radio_finalize.py']
    for rel in paths:
        p=snapshot/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/rel,p)
    # Intermediate build directories/native executables are NOT sealed deliverables.
    selected=[]
    for p in walk(OUT):
        rel=p.relative_to(OUT)
        if rel.parts[0]=='work':continue
        if rel.parts[0]=='validation' and len(rel.parts)>1 and rel.parts[1] in ('tmp','native','mbedtls'):continue
        if p.is_symlink():continue
        selected.append(p)
    save('EVIDENCE-SHA256.json',{str(p.relative_to(OUT)):sha(p) for p in sorted(selected)})
    for p in selected+[OUT/'EVIDENCE-SHA256.json']:
        p.chmod(0o444)
    manifest=json.loads((OUT/'EVIDENCE-SHA256.json').read_text())
    assert all(sha(OUT/rel)==h for rel,h in manifest.items())
    print(json.dumps({'sealed_files':len(manifest),'manifest_sha256':sha(OUT/'EVIDENCE-SHA256.json'),
                      'guarded_files':len(json.loads((OUT/'guard-pre.json').read_text())),'verified':True}))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['prove','seal']);a=p.parse_args()
    prove() if a.mode=='prove' else seal()
