#!/usr/bin/env python3
"""Read real ELF32/MAP/stack-use artifacts. Never infer runtime heap measurements."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def validate_regions(regions):
    aligned=[]
    for name,start,end in regions:
        if end < start:
            raise ValueError('Reversed reserved region: '+name)
        if end == start:
            continue
        aligned.append((start & ~3, (end+3)&~3, name))
    aligned.sort()
    for old,new in zip(aligned,aligned[1:]):
        if new[0] < old[1]:
            raise ValueError('Overlapping heap reservations: '+old[2]+' / '+new[2])

class Elf:
    def __init__(self,path):
        self.raw=Path(path).read_bytes()
        if self.raw[:6] != b'\x7fELF\x01\x01':
            raise ValueError('Expected little-endian ELF32')
        h=struct.unpack_from('<16sHHIIIIIHHHHHH',self.raw)
        headers=[struct.unpack_from('<IIIIIIIIII',self.raw,h[6]+i*h[11]) for i in range(h[12])]
        names=headers[h[13]]
        names=self.raw[names[4]:names[4]+names[5]]
        self.sections={names[s[0]:].split(b'\0',1)[0].decode():s for s in headers}
        self.symbols={}
        for s in headers:
            if s[1]!=2: continue
            strings=headers[s[6]]
            strings=self.raw[strings[4]:strings[4]+strings[5]]
            for at in range(s[4],s[4]+s[5],s[9]):
                name,value,size,info,other,section=struct.unpack_from('<IIIBBH',self.raw,at)
                name=strings[name:].split(b'\0',1)[0].decode()
                self.symbols[name]=(value,size)
    def read(self,address,size):
        for section in self.sections.values():
            if section[1]!=8 and section[3]<=address and address+size<=section[3]+section[5]:
                at=section[4]+address-section[3]
                return self.raw[at:at+size]
        raise ValueError('ELF address is not file-backed')

def report(elf_path,map_path=None,stack_root=None):
    elf=Elf(elf_path)
    sections={name:{'address':s[3],'size':s[5],'type':s[1]}
              for name,s in elf.sections.items() if s[2]&2 and s[5]}
    # These named ranges are deliberately not summed with general capability
    # heaps. IRAM/DRAM aliases and reserved regions can overlap in capabilities.
    dram=sum(s['size'] for name,s in sections.items() if name in ('.dram0.data','.dram0.bss','.noinit'))
    iram=sum(s['size'] for name,s in sections.items() if name.startswith('.iram0.'))
    flash=sum(s['size'] for name,s in sections.items() if name.startswith('.flash.'))
    reserves=[]
    lo=elf.symbols.get('soc_reserved_memory_region_start')
    hi=elf.symbols.get('soc_reserved_memory_region_end')
    if lo and hi:
        names={v[0]:k for k,v in elf.symbols.items() if k.startswith('reserved_region_')}
        for address in range(lo[0],hi[0],8):
            start,end=struct.unpack('<II',elf.read(address,8))
            reserves.append((names.get(address,hex(address)),start,end))
        validate_regions(reserves)
    stacks=[]
    if stack_root:
        for path in Path(stack_root).rglob('*.su'):
            for line in path.read_text(errors='replace').splitlines():
                parts=line.split('\t')
                if len(parts)>=3 and parts[1].isdigit():
                    stacks.append({'function':parts[0],'bytes':int(parts[1]),'qualifier':parts[2]})
    stacks.sort(key=lambda s:s['bytes'],reverse=True)
    result={'installable':False,'elf_sha256':hashlib.sha256(elf.raw).hexdigest(),
            'elf_file_bytes':len(elf.raw),'static_dram_bytes':dram,'iram_sections_bytes':iram,
            'mapped_flash_sections_bytes':flash,'sections':sections,
            'heap_reservations':reserves,'largest_compiler_stack_frames':stacks[:30],
            'runtime_internal_free_bytes':None,'runtime_internal_largest_block':None,
            'runtime_psram_use_bytes':None,'runtime_stack_low_water_bytes':None,
            'limitations':['Static section sizes are not free heap.',
                'Compiler frames are not transitive stack bounds; SDK/ROM/indirect calls remain unknown.',
                'Bluetooth buffers, explicit INTERNAL/DMA requests and task stacks must remain internal.',
                'No device execution or installation compatibility established.']}
    if map_path:
        result['map_sha256']=hashlib.sha256(Path(map_path).read_bytes()).hexdigest()
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf',type=Path)
    parser.add_argument('--map',type=Path)
    parser.add_argument('--stacks',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    text=json.dumps(report(args.elf,args.map,args.stacks),indent=2)+'\n'
    if args.output: args.output.write_text(text)
    print(text,end='')
