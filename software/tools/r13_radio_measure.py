#!/usr/bin/env python3
"""Audit real raw Phase-A ELF/map and immutable source/config inputs."""
import collections
import json
import re
import subprocess
from pathlib import Path
from r13_radio_spike import ROOT, OUT, BASE, IDF, TOOLBIN, sha, walk


def measure(kind):
    d=OUT/'candidate-UNTESTED'/kind
    elf=d/'firmware.elf'
    text=(d/'firmware.map').read_text()
    nm=subprocess.check_output([str(TOOLBIN/'xtensa-esp32-elf-nm'),'-n','-C',str(elf)],text=True)
    sizes=subprocess.check_output([str(TOOLBIN/'xtensa-esp32-elf-size'),'-A',str(elf)],text=True)
    (OUT/(kind+'-nm.txt')).write_text(nm)
    (OUT/(kind+'-size.txt')).write_text(sizes)
    sections={m[1]:int(m[2]) for m in re.finditer(r'^(\.\S+)\s+(\d+)\s+\d+$',sizes,re.M)}
    symbol={m[2]:int(m[1],16) for m in re.finditer(r'^([0-9a-f]+)\s+\w\s+(\S+)$',nm,re.M)}
    start=symbol.get('__init_array_start',symbol.get('_init_array_start'))
    end=symbol.get('__init_array_end',symbol.get('_init_array_end'))
    if start is None:
        for key in ('__init_array_start','__init_array_end','_init_array_start','_init_array_end'):
            m=re.search(r'^\s*(0x[0-9a-f]+)\s+'+key+r'\s*=',text,re.M)
            if m:symbol[key]=int(m[1],16)
        start=symbol.get('__init_array_start',symbol.get('_init_array_start'))
        end=symbol.get('__init_array_end',symbol.get('_init_array_end'))
    # Restrict contributions to actual output IRAM section, never discarded input.
    region=text.split('\n.iram0.text ',1)[1].split('\n.dram0.data ',1)[0]
    contributions=[]
    pending=''
    for line in region.splitlines():
        if re.match(r'^\s+\.\S+\s*$',line):pending=line.strip()
        m=re.match(r'^\s+(?:(\.\S+)\s+)?(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(.+?\.a\([^)]+\)|\S+\.o)\s*$',line)
        if m:
            contributions.append({'section':m[1] or pending,'address':int(m[2],16),'size':int(m[3],16),'object':m[4]})
    totals=collections.Counter()
    for r in contributions:
        name=Path(r['object'].split('(')[0]).name
        totals[name]+=r['size']
    head=text.split('Discarded input sections',1)[0]
    members=sorted(set(re.findall(r'^(\S+\.a\([^)]+\))\s*$',head,re.M)))
    bt_members=[x for x in members if re.search(r'/(?:libbt|libbtdm_app|libcoexist|libesp_coex)\.a\(',x)]
    init_region=''
    m=re.search(r'(?ms)^\s*0x[0-9a-f]+\s+__init_array_start.*?^\s*0x[0-9a-f]+\s+__init_array_end.*?$',text)
    if m:init_region=m[0]
    mqtt=[]
    live_text=text.split('Linker script and memory map',1)[1]
    for line in live_text.splitlines():
        m=re.match(r'^\s+(?:\.\S+\s+)?(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S*/modules/mqtt/\S+)',line)
        if m and int(m[1],16)>=0x3f000000 and int(m[2],16)>0:
            mqtt.append(line)
    assert mqtt, 'Stock MQTT must be retained, not just present as discarded input'
    result={'kind':kind,'elf_sha256':sha(elf),'map_sha256':sha(d/'firmware.map'),
       'sections':{k:sections[k] for k in ('.iram0.text','.dram0.data','.dram0.bss')},
       'heap_start':hex(symbol['_heap_start']), 'init_array_count':None if start is None or end is None else (end-start)//4,
       'init_array_start':start,'init_array_end':end,'bt_archive_members_count':len(bt_members),
       'bt_archive_members':bt_members,'iram_archive_bytes':dict(totals),
       'contributions':contributions,'mqtt_live_contributions':mqtt,'init_array_map':init_region,
       'spike_dispatch_retained':'tesla_radio_spike_dispatch' in symbol,
       'nimble_functions':[k for k in symbol if k.startswith(('nimble_port_','ble_gattc_','ble_gap_'))],
       'no_bluedroid_lifecycle':not any(k.startswith('esp_bluedroid_') for k in symbol),
       'no_spike_constructor':not any('_GLOBAL__sub_I_' in l and 'spike' in l for l in nm.splitlines())}
    (OUT/(kind+'-elf-map-audit.json')).write_text(json.dumps(result,indent=2)+'\n')
    return {k:result[k] for k in ('sections','heap_start','init_array_count','bt_archive_members_count','iram_archive_bytes','spike_dispatch_retained','no_bluedroid_lifecycle','no_spike_constructor')}


def main():
    output={k:measure(k) for k in ('bluedroid','nimble')}
    old=ROOT/'offline-r13-extension-only-lean-off-sol'
    historical=json.loads((old/'footprint-initial-comparison.json').read_text())
    (OUT/'historical-comparison-copy.json').write_text(json.dumps(historical,indent=2)+'\n')
    # Explicitly identify generated private build output, not forbidden source changes.
    generated={'src/build.cpp','src/index_html.digest','src/index_html.embedded.cpp'}
    checks={}
    for kind in output:
        prior=json.loads((OUT/('app-'+kind+'-core-binding.json')).read_text())
        unexpected=set(prior['mismatches'])-generated
        assert not unexpected,unexpected
        sdk=OUT/'work'/('sdk-'+kind)/'out/tools/esp32-arduino-libs'
        archive=sdk/'esp32/ld/libbtdm_app.a'
        assert sha(archive)==sha(BASE/'esp32/ld/libbtdm_app.a')==sha(IDF/'components/bt/controller/lib_esp32/esp32/libbtdm_app.a')
        checks[kind]={'only_private_generated_differences':prior['mismatches'],
            'all_original_core_sources_unchanged':True, 'controller_archive_sha256':sha(archive),
            'rebuilt_libbt_sha256':sha(sdk/'esp32/lib/libbt.a')}
    (OUT/'compiled-source-and-controller-proof.json').write_text(json.dumps(checks,indent=2)+'\n')
    (OUT/'measured-summary.json').write_text(json.dumps(output,indent=2)+'\n')
    print(json.dumps(output,indent=2))

if __name__=='__main__':main()
