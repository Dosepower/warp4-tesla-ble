#!/usr/bin/env python3
"""Read raw full application ELF/map/images; no loader, signing or packaging."""
import collections
import json
from pathlib import Path
import re
import struct
import subprocess
from elftools.elf.elffile import ELFFile
from r13_iram_config_build import OUT, WORK, ROOT, TOOLBIN, PYTHON, VARIANTS, sha, write

def raw_at(e,a,n):
 s=next(s for s in e.iter_sections() if s['sh_type']!='SHT_NOBITS' and s['sh_addr']<=a and a+n<=s['sh_addr']+s['sh_size'])
 return s.data()[a-s['sh_addr']:a-s['sh_addr']+n]

def empty(n):
 # Bound ESP32 TLSF ABI and LIGHT poisoning; not a runtime free reading.
 remain=n-20; sl=3 if remain<=16384 else 4 if remain<=262144 else 5
 fl=remain.bit_length()-(sl+2)+1; control=36+4*fl+4*fl*(1<<sl)
 raw=(remain-control-8)&-4; fit=raw&-(1<<(raw.bit_length()-1-sl))
 return dict(raw_region=n,multi_heap=20,tlsf_control=control,pool_headers=8,raw_free=raw,empty_reported_largest=fit-12)

def layout(e):
 sy={s.name:s for s in e.get_section_by_name('.symtab').iter_symbols()}; get=lambda n:sy[n]['st_value']
 a,b=get('soc_reserved_memory_region_start'),get('soc_reserved_memory_region_end'); reservations=list(struct.iter_unpack('<II',raw_at(e,a,b-a)))
 types=list(struct.iter_unpack('<IIII',raw_at(e,get('soc_memory_types'),sy['soc_memory_types']['st_size'])))
 regions=list(struct.iter_unpack('<IIIIB3x',raw_at(e,get('soc_memory_regions'),sy['soc_memory_regions']['st_size'])))
 chunks=[]
 for a,n,t,alias,stack in regions:
  pieces=[(a,a+n)]
  for ra,rb in reservations:
   if ra==rb:continue
   pieces=[x for pa,pb in pieces for x in ([(pa,min(pb,ra))] if pa<ra else [])+([(max(pa,rb),pb)] if pb>rb else []) if x[1]>x[0]]
  for pa,pb in pieces:
   if pb-pa>16:
    if chunks and chunks[-1][1]==pa and chunks[-1][2:]==[t,stack]:chunks[-1][1]=pb
    else:chunks.append([pa,pb,t,stack])
 def select(mask):
  return [dict(start=hex(a),end=hex(b),bytes=b-a,caps=hex(types[t][1]|types[t][2]|types[t][3]),startup_stack=bool(stack),empty_heap=empty(b-a)) for a,b,t,stack in chunks if (types[t][1]|types[t][2]|types[t][3])&mask==mask]
 return dict(symbols={n:hex(get(n)) for n in ('_iram_start','_iram_end','_heap_start')},reservations=[dict(start=hex(a),end=hex(b)) for a,b in reservations],strict_iram=select(1<<29),exec_internal=select(1|1<<11),internal8=select((1<<11)|(1<<2)),manual_arena=dict(start='0x4009e000',end='0x400a0000',raw_bytes=8192,usable_initial_bytes=8176,not_sdk_heap=True),runtime_metrics=None)

def measure(kind,bench=False,probe=False):
 suffix='-probe' if probe else '-bench' if bench else ''
 profile='warp4_tesla_ble_iram_probe' if probe else 'warp4_tesla_ble_lean_bench' if bench else 'warp4_tesla_ble'
 d=WORK/'software/.pio'/('build-'+kind+suffix)/profile
 t=(d/'firmware.map').read_text(); elf=d/'firmware.elf'; binary=d/'firmware.bin'
 nm=subprocess.check_output([str(TOOLBIN/'xtensa-esp32-elf-nm'),'-n','-C',str(elf)],text=True)
 write(OUT/('measure-'+kind+suffix+'-nm.txt'),nm)
 sizes=subprocess.check_output([str(TOOLBIN/'xtensa-esp32-elf-size'),'-A',str(elf)],text=True);write(OUT/('measure-'+kind+suffix+'-size.txt'),sizes)
 with elf.open('rb') as f:
  e=ELFFile(f); sections={s.name:dict(address=hex(s['sh_addr']),size=s['sh_size']) for s in e.iter_sections() if s.name.startswith(('.iram0.','.dram0.','.flash.'))}; memory=layout(e)
 region=t.split('\n.iram0.text ',1)[1].split('\n.dram0.data ',1)[0]
 contributions=[];pending=''
 for l in region.splitlines():
  if re.match(r'^\s+\.\S+\s*$',l):pending=l.strip()
  m=re.match(r'^\s+(?:(\.\S+)\s+)?(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(.+?\.a\([^)]+\)|\S+\.o)\s*$',l)
  if m:contributions.append(dict(section=m[1] or pending,address=int(m[2],16),size=int(m[3],16),object=m[4]))
 totals=collections.Counter()
 for r in contributions:totals[Path(r['object'].split('(')[0]).name]+=r['size']
 syms={m[2]:int(m[1],16) for m in re.finditer(r'^([0-9a-f]+)\s+\w\s+(.+)$',nm,re.M)}
 live=t.split('Linker script and memory map',1)[1];mqtt=[]
 for l in live.splitlines():
  m=re.match(r'^\s+(?:\.\S+\s+)?(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S*/modules/mqtt/\S+)',l)
  if m and int(m[1],16)>=0x3f000000 and int(m[2],16)>0:mqtt.append(l)
 assert mqtt,'Stock MQTT must be linked'
 assert not any('tesla_radio_spike' in n for n in syms)
 assert not any(n.startswith(('nimble_port_','ble_gap_','ble_gattc_')) for n in syms)
 if not bench and not probe:
  for stem in ('BluetoothLifecycle::run(','TeslaVehicle::start_runtime(','Mqtt::register_events('):assert any(n.startswith(stem) for n in syms),stem
  assert sha(WORK/('sdk-'+kind)/'out/tools/esp32-arduino-libs/esp32/ld/libbtdm_app.a')==sha(ROOT/'offline-r13-extension-only-iram-config-sol/work/sdk-baseline/out/tools/esp32-arduino-libs/esp32/ld/libbtdm_app.a')
  # GCC -O2 can alter Xtensa linker relaxation against the unchanged controller
  # input (16 bytes here); it must not be mistaken for vendor source relocation.
  if kind!='compiler-perf':assert totals['libbtdm_app.a']==22729,totals['libbtdm_app.a']
 # esptool verifies segment/checksum/appended digest; no image transformation.
 cmd=[str(PYTHON),'-m','esptool','--chip','esp32','image_info',str(binary)]
 check=subprocess.run(['unshare','-Urn',*cmd],capture_output=True,text=True)
 write(OUT/('image-'+kind+suffix+'.txt'),check.stdout+check.stderr);assert check.returncode==0,check.stderr
 assert 'valid' in check.stdout.lower(),check.stdout
 # Payload segments overlap ELF sections exactly by address, ignoring padding.
 import esptool.bin_image
 with binary.open('rb') as image_file:
  image=esptool.bin_image.ESP32FirmwareImage(image_file)
 seg=[]
 for s in image.segments:
  seg.append(dict(address=hex(s.addr),size=len(s.data)))
  assert not (s.addr<0x400a0000 and s.addr+len(s.data)>0x400a0000 and s.addr>=0x40080000)
 parts=(WORK/'software/default_16MB_secure.csv').read_text()
 app_sizes=[int(m[1],0) for m in re.finditer(r'^\s*[^#\n,]+,\s*app,\s*[^,]+,\s*[^,]*,\s*(0x[0-9a-fA-F]+|\d+)\s*,?',parts,re.M)]
 assert app_sizes,parts
 assert binary.stat().st_size<=min(app_sizes)
 result=dict(kind=kind+suffix,sections=sections,heap_layout=memory,iram_archive_bytes=dict(totals),iram_contributions=contributions,mqtt_live_contributions=mqtt,mqtt_source_hashes=json.loads((OUT/('app-'+kind+suffix+('-final-core-binding.json' if probe else '-core-binding.json'))).read_text())['mqtt'],no_spike_or_nimble=True,full_tesla_lifecycle=any(n.startswith('BluetoothLifecycle::run(') for n in syms),images=dict(bin_sha256=sha(binary),elf_sha256=sha(elf),map_sha256=sha(d/'firmware.map'),raw_bin_bytes=binary.stat().st_size,segments=seg,esptool_checksum_digest_valid=True,production_app_slots_bytes=app_sizes,fits_partition=True,signed=False,installable=False,hardware_tested=False))
 write(OUT/('measure-'+kind+suffix+'.json'),result)
 return result

if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser();p.add_argument('kind',choices=[*VARIANTS,'all']);p.add_argument('--bench',action='store_true');p.add_argument('--probe',action='store_true');a=p.parse_args()
 if a.kind!='all':print(json.dumps(measure(a.kind,a.bench,a.probe),indent=2))
 else:
  rows=[measure(k) for k in VARIANTS]
  base=rows[0]; comparison=[]
  for row in rows:
   comparison.append(dict(kind=row['kind'],iram_text=row['sections']['.iram0.text']['size'],iram_saved=base['sections']['.iram0.text']['size']-row['sections']['.iram0.text']['size'],dram_data=row['sections']['.dram0.data']['size'],dram_bss=row['sections']['.dram0.bss']['size'],heap_start=row['heap_layout']['symbols']['_heap_start'],strict_iram=row['heap_layout']['strict_iram'],archive_delta={k:row['iram_archive_bytes'].get(k,0)-base['iram_archive_bytes'].get(k,0) for k in row['iram_archive_bytes'].keys()|base['iram_archive_bytes'].keys() if row['iram_archive_bytes'].get(k,0)!=base['iram_archive_bytes'].get(k,0)}))
  write(OUT/'matrix.json',comparison);print(json.dumps(comparison,indent=2))
