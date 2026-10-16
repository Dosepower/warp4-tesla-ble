#!/usr/bin/env python3
"""Fail-closed operation-specific Xtensa audit. Never turn an unknown into zero.

Static frame accounting is evidence, not runtime high-water. A resource/stack
approval needs complete edges, fixed stack, and >=1024 bytes after context
reserves. This tool deliberately emits NO-GO when that cannot be established.
"""
from pathlib import Path
import argparse,bisect,hashlib,json,re,struct,subprocess,sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bench_confinement import output_plan

# Pinned SDK stack_check.c:26-28 calls esp_system_abort; chip port:85-87
# declares it noreturn and calls panic_abort; panic.c:486-506 traps/loops.
# The callee edge is ALWAYS retained and audited, including fatal/logging work.
# Only impossible post-return caller padding is excluded, not the fatal path.
NORETURN={'__stack_chk_fail','esp_system_abort','panic_abort'}

def bound(graph, roots, reserve, worker_bytes):
    seen=set();unknown={};cycles=set();active=[];memo={}
    def visit(name):
        if name in active:
            cycles.add(tuple(active[active.index(name):]+[name]));return (0,[])
        if name in memo:return memo[name]
        seen.add(name)
        if name not in graph:
            unknown[name]=['missing linked body'];return (0,[])
        row=graph[name]
        if row.get('unknown'):unknown[name]=list(row['unknown'])
        if not row.get('coverage_complete',True):unknown.setdefault(name,[]).append('reachable instruction coverage failure')
        if row.get('dynamic'):unknown.setdefault(name,[]).append('dynamic/unclassified stack adjustment')
        active.append(name)
        children=[visit(child) for child in row['calls']]
        best=max(children,default=(0,[]),key=lambda x:x[0])
        active.pop()
        result=(row['frame']+best[0],[name]+best[1]);memo[name]=result;return result
    best=max((visit(root) for root in roots),default=(0,[]),key=lambda x:x[0])
    complete=not unknown and not cycles
    total=best[0]+reserve
    return {'complete':complete,'bounded_bytes':total if complete else None,
            'worker_bytes':worker_bytes,
            'margin_at_least_1024':complete and worker_bytes-total>=1024,
            'remaining_bytes':worker_bytes-total if complete else None,
            'accounted_candidate_bytes_including_context':total,
            'candidate_path':[{'symbol':n,'entry_bytes':graph[n]['frame']} for n in best[1]],
            'unknown':unknown,'cycles':[list(c) for c in sorted(cycles)],'seen_functions':len(seen)}

def sections(path):
    data=path.read_bytes();h=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
    rows=[struct.unpack_from('<IIIIIIIIII',data,h[6]+i*h[11]) for i in range(h[12])]
    names=rows[h[13]];strings=data[names[4]:names[4]+names[5]]
    return {strings[s[0]:].split(b'\0')[0].decode():(s[3],s[5],b'' if s[1]==8 else data[s[4]:s[4]+s[5]]) for s in rows if s[2]&2}

def branch_target(arg):
    match=re.search(r'\b([0-9a-f]{4,8})\s+<',arg)
    return int(match[1],16) if match else None

def walk_blocks(start,end,decode):
    """Walk exact PCs, not indexes into a linear, possibly misaligned dump.

    decode must validate code/data boundaries and bytes. Missing reachable PCs
    are coverage failures, including the real entry and branch destinations.
    Fallthrough is PC+instruction-size; it never skips data/alignment holes.
    """
    reachable={};failures=set();pending=[start];external=set();landing=set()
    while pending:
        pc=pending.pop()
        if pc in reachable or pc in failures:continue
        if not start<=pc<end:failures.add(pc);continue
        ins=decode(pc)
        if not ins or ins['size'] not in (2,3) or pc+ins['size']>end:
            failures.add(pc);continue
        reachable[pc]=ins;op,arg=ins['op'],ins['arg']
        branch=op.startswith(('b','j','loop')) and not op.startswith(('break','jx'))
        if branch:
            target=branch_target(arg)
            if target is None:failures.add(pc)
            elif start<=target<end:pending.append(target);landing.add(target)
            else:external.add(target)
        if ins.get('noreturn') or op.startswith(('ret','jx','ill','break')) or op in ('j','j.n'):continue
        pending.append(pc+ins['size'])
    return {'reachable':reachable,'landing':landing,'external':external,
            'coverage_complete':not failures,'coverage_failures':sorted(failures)}

def disassembly_rows(text):
    result={}
    for line in text.splitlines():
        m=re.match(r'^\s*([0-9a-f]+):\s+([0-9a-f]+)\s+([a-z0-9_.]+)\s*(.*)',line)
        if not m or m[3].startswith('.'):continue
        raw=m[2]
        if len(raw) not in (4,6):continue
        result[int(m[1],16)]={'size':len(raw)//2,'op':m[3],'arg':m[4],
                              'bytes':bytes.fromhex(raw)[::-1]}
    return result

def rehearsal_reachability(graph,root='BenchPairing::rehearse_local()'):
    """Known linked-call closure; source capability checks remain mandatory.

    Vendor indirect leaves stay explicit. This does NOT use absence of a known
    forbidden edge as a vendor-bound or all-indirect-calls proof.
    """
    forbidden=('pair_charging_manager(', 'BenchPairing::send_pair(', 'send_frame(',
               'pair_write(', 'pair_send(', 'esp_ble_gattc_write', 'encode_session',
               'DomainAuthentication::', 'session_info_tag(', 'encode_charge(',
               'encode_lock(', 'wake_payload(', 'status_payload(')
    seen=set();pending=[root];open_edges={};coverage={}
    while pending:
        name=pending.pop()
        if name in seen:continue
        seen.add(name);row=graph.get(name)
        if row is None:open_edges[name]=['missing body'];continue
        if row.get('unknown'):open_edges[name]=row['unknown']
        if not row.get('coverage_complete',True):coverage[name]=row.get('coverage_failures',[])
        pending.extend(row['calls'])
    return {'root':root,'forbidden_reachable':sorted(n for n in seen if any(f in n for f in forbidden)),
            'seen_functions':len(seen),'reachable_symbols':sorted(seen),
            'open_edges':open_edges,'coverage_failures':coverage,
            'scope':'Actual linked known-call closure plus selected finite application callbacks; vendor indirect leaves are NOT proven closed.'}

def linked_graph(elf,out,tool):
    output_plan(roots=(out,), files=(out/'inspection-only.elf', out/'final-disassembly.log')).revalidate()
    audit=out/'inspection-only.elf'
    subprocess.run([tool+'objcopy','--remove-section=.xt.prop','--remove-section=.xt.lit',str(elf),str(audit)],check=True)
    allocated=sections(elf)
    assert allocated==sections(audit),'Inspection copy changed loadable bytes'
    # Metadata is not trusted to suppress reachable code. The first listing is
    # only a cache; re-decode from each missing actual entry/branch PC below.
    text=subprocess.check_output([tool+'objdump','-d','-z','-C',str(audit)],text=True)
    (out/'final-disassembly.log').write_text(text)
    nm=subprocess.check_output([tool+'nm','-C','-S','--defined-only',str(elf)],text=True)
    symbols={m[4]:(int(m[1],16),int(m[2],16)) for line in nm.splitlines() if (m:=re.match(r'^([0-9a-f]+)\s+([0-9a-f]+)\s+([tTwW])\s+(.+)$',line))}
    address_to_name={address:name for name,(address,size) in symbols.items()}
    data_ranges=[(int(m[1],16),int(m[1],16)+int(m[2],16)) for line in nm.splitlines() if (m:=re.match(r'^([0-9a-f]+)\s+([0-9a-f]+)\s+[dDrRbB]\s+(.+)$',line))]
    data_ranges.sort();data_starts=[a for a,b in data_ranges];data_ends=[]
    for a,b in data_ranges:data_ends.append(max(b,data_ends[-1] if data_ends else 0))
    # Allocated executable section boundaries come from ELF, never section-name
    # guesses. Literal words discovered by reachable l32r are data, not code.
    data=elf.read_bytes();h=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
    executable=[]
    for i in range(h[12]):
        s=struct.unpack_from('<IIIIIIIIII',data,h[6]+i*h[11])
        if s[2]&6==6:executable.append((s[3],s[3]+s[5]))
    literals=set();literal_bytes=set();cache=disassembly_rows(text);decoded_log=[];graph={}
    def raw_at(pc,length):
        return next((body[pc-base:pc-base+length] for base,size,body in allocated.values()
                     if body and base<=pc and pc+length<=base+size),None)
    def is_code(pc,size):
        i=bisect.bisect_left(data_starts,pc+size)-1
        return any(a<=pc and pc+size<=b for a,b in executable) and not (
            i>=0 and data_ends[i]>pc) and not any(byte in literal_bytes for byte in range(pc,pc+size))
    def decode(pc,end):
        if not is_code(pc,2):return None
        if pc not in cache:
            block=subprocess.check_output([tool+'objdump','-D','-z','-C',
                '--start-address='+hex(pc),'--stop-address='+hex(min(end,pc+256)),str(audit)],text=True)
            decoded_log.append(block);cache.update(disassembly_rows(block))
        ins=cache.get(pc)
        if not ins or not is_code(pc,ins['size']) or raw_at(pc,ins['size'])!=ins['bytes']:return None
        if ins['op']=='l32r':
            literal=branch_target(ins['arg'])
            if literal is not None:literals.add(literal);literal_bytes.update(range(literal,literal+4))
        if ins['op'].startswith('call'):
            target=branch_target(ins['arg']) if not ins['op'].startswith('callx') else None
            if ins['op'].startswith('callx'):
                # Narrow proven case only: immediately preceding, byte-checked
                # l32r into the call register. No assumed cross-block registers.
                prev=cache.get(pc-3)
                if prev and prev['op']=='l32r' and prev['arg'].split(',')[0]==ins['arg'].strip() and raw_at(pc-3,3)==prev['bytes']:
                    literal=branch_target(prev['arg']);raw=raw_at(literal,4) if literal is not None else None
                    if raw:target=struct.unpack('<I',raw)[0]
            if target is not None and address_to_name.get(target) in NORETURN:ins=dict(ins,noreturn=True)
        return ins
    for name,(start,size) in symbols.items():
        if start in graph:continue
        if address_to_name[start]!=name:continue
        end=start+size;row=walk_blocks(start,end,lambda pc:decode(pc,end))
        calls=set();unknown=[hex(pc)+':undecoded reachable destination/code-data boundary' for pc in row['coverage_failures']]
        indirect=[];regs={};dynamic=False;frame=0
        previous_end=None
        for pc,ins in sorted(row['reachable'].items()):
            op,arg=ins['op'],ins['arg']
            if pc in row['landing'] or pc!=previous_end:regs.clear()
            previous_end=pc+ins['size']
            if op=='entry':
                m=re.match(r'a1,\s*(0x[0-9a-f]+|[0-9]+)',arg)
                if m:frame=max(frame,int(m[1],0))
                else:unknown.append(hex(pc)+':unclassified entry')
            elif op.startswith('callx'):
                target=regs.get(arg.split()[0])
                if target:calls.add(target)
                else:indirect.append(hex(pc)+':indirect call')
                for register in ['a'+str(i) for i in range(8,16)]:regs.pop(register,None)
            elif op.startswith('call'):
                target=branch_target(arg)
                if target in address_to_name:calls.add(address_to_name[target])
                else:
                    m=re.search(r'<([^\n]+)>',arg)
                    calls.add(m[1] if m else hex(target or pc)+':missing direct target')
                for register in ['a'+str(i) for i in range(8,16)]:regs.pop(register,None)
            elif op=='l32r':
                reg=arg.split(',')[0];literal=branch_target(arg);raw=raw_at(literal,4) if literal is not None else None
                target=struct.unpack('<I',raw)[0] if raw else None
                if target in address_to_name:regs[reg]=address_to_name[target]
                else:
                    m=re.search(r'\(([0-9a-f]+) <([^\n]+)>\)',arg)
                    if m and target==int(m[1],16):regs[reg]=m[2]
                    else:regs.pop(reg,None)
            elif op in ('mov','mov.n') or (op=='or' and len(set(arg.replace(' ','').split(',')[1:]))==1):
                dst,src=map(str.strip,arg.split(',')[:2])
                if src in regs:regs[dst]=regs[src]
                else:regs.pop(dst,None)
                if dst=='a1' and src!='a1':dynamic=True
            elif op.startswith(('b','j','loop')):
                if op.startswith('jx'):unknown.append(hex(pc)+':indirect jump')
                regs.clear()
            elif op.startswith(('ill','break')):unknown.append(hex(pc)+':trap/exception descendants not bounded')
            elif op.startswith(('s8i','s16i','s32i','ret','memw','rsync','esync','nop')):pass
            else:
                dst=arg.split(',')[0].strip();regs.pop(dst,None)
                if dst=='a1':dynamic=True
        for target in row['external']:
            if target in address_to_name:calls.add(address_to_name[target])
            else:unknown.append(hex(target)+':branch outside symbol extent not a function entry')
        unknown.extend(indirect)
        graph[name]={'address':start,'frame':frame,'calls':sorted(calls),'unknown':unknown,
                     'indirect_calls':indirect,'dynamic':dynamic,'coverage_complete':row['coverage_complete'],
                     'coverage_failures':row['coverage_failures'],'reachable_instructions':len(row['reachable'])}
    # Later-discovered l32r data cannot retroactively become code in earlier
    # rows. Detect that overlap by checking the cached reachable instruction PCs.
    literal_starts=sorted(literals)
    for name,row in graph.items():
        start,size=symbols[name];end=start+size
        i=bisect.bisect_left(literal_starts,start)
        if i<len(literal_starts) and literal_starts[i]<end:
            walked=walk_blocks(start,end,lambda pc:decode(pc,end))
            if not walked['coverage_complete']:
                row['coverage_complete']=False;row['coverage_failures']=walked['coverage_failures']
                row['unknown'].append('reachable instruction overlaps literal/data or missing decode')
    for alias,(address,size) in symbols.items():
        if alias not in graph:
            graph[alias]={'frame':0,'calls':[address_to_name[address]],'unknown':[],'dynamic':False,'alias':address_to_name[address]}
    (out/'reachable-block-decodes.log').write_text('\n'.join(decoded_log))
    return graph,nm,text

def bound_worker_constants(header,manifest,nm):
    """Refuse an unbound/current-header budget for an older compiled ELF."""
    inputs=json.loads(manifest.read_text())
    digest=hashlib.sha256(json.dumps(inputs,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    if re.findall(r'\bbench_inputs_([0-9a-f]{64})\b',nm)!=[digest]:
        raise ValueError('worker budget manifest is not bound to exact ELF')
    checksum=hashlib.sha256(header.read_bytes()).hexdigest()
    if inputs['files'].get(str(header.resolve()))!=checksum:
        raise ValueError('worker budget header differs from compiled input')
    constants={}
    for name in ('PAIR_WORKER_BYTES','REHEARSAL_WORKER_BYTES','DISCOVERY_WORKER_BYTES'):
        match=re.search(r'\b'+name+r'=(\d+)',header.read_text())
        if match is None or int(match[1])<=0:raise ValueError('unresolved worker budget '+name)
        constants[name]=int(match[1])
    return constants,{'path':str(header),'sha256':checksum,'constants':constants,'build_identity':digest,'manifest':str(manifest)}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--elf',type=Path,required=True);parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--budget-header',type=Path,default=Path(__file__).resolve().parents[1]/'src/modules/tesla_vehicle/tesla_ble_bench_discovery.h')
    args=parser.parse_args()
    output_plan(roots=(args.out,)).revalidate()
    args.out.mkdir(parents=True,exist_ok=True)
    tool='/home/hermes/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-'
    graph,nm,text=linked_graph(args.elf,args.out,tool)
    # Offline installed Espressif ROM ELF code; no target memory reads. Union
    # both classic ESP32 revisions rather than assume the operator's silicon.
    rom_evidence=[];rom_names={}
    for revision in ('rev0','rev300'):
        rom=Path('/home/hermes/.espressif/tools/esp-rom-elfs/20260528')/('esp32_'+revision+'_rom.elf')
        if not rom.is_file():continue
        directory=args.out/('rom-'+revision);directory.mkdir(exist_ok=True)
        rg,_,_=linked_graph(rom,directory,tool)
        for name,row in rg.items():
            full='ROM_'+revision+'::'+name
            rom_names.setdefault(name,[]).append(full)
            graph[full]=dict(row,calls=['ROM_'+revision+'::'+c if c in rg else c for c in row['calls']])
            if row.get('alias'):graph[full]['alias']='ROM_'+revision+'::'+row['alias']
        rom_evidence.append({'path':str(rom),'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'functions':len(rg)})
    for name,targets in rom_names.items():
        if name not in graph:graph[name]={'frame':0,'calls':targets,'unknown':[],'dynamic':False}
    def pick(prefix):
        if prefix in graph:return prefix
        names=[n for n in graph if n.startswith(prefix)]
        if len(names)!=1:raise ValueError((prefix,names))
        return names[0]
    # Selected concrete callback sets; no generic user/plugin callbacks are
    # silently attributed to these bench functions. Unhandled sites stay open.
    bindings=[]
    def bind(prefix,targets,why):
        matches=[n for n in graph if n==prefix or n.startswith(prefix)]
        for name in matches:
            while graph[name].get('alias'):name=graph[name]['alias']
            sites=graph[name].get('indirect_calls',[])
            if not sites:continue
            selected=[pick(t) for t in targets]
            bindings.append({'caller':name,'sites':sites[:],'targets':selected,'basis':why})
            graph[name]['calls']=sorted(set(graph[name]['calls']+selected))
            graph[name]['unknown']=[site for site in graph[name]['unknown'] if site not in sites]
            graph[name]['indirect_calls']=[]
    bind('BenchPairing::prepare()', ['BenchPairDisk::load(','BenchPairDisk::exposure('], 'Only concrete BenchPairDisk member passed by Discovery')
    bind('BenchPairing::RecordWriter::save(',['BenchPairDisk::save('], 'RecordWriter owner.disk is the same concrete BenchPairDisk')
    bind('BenchPairing::send_pair(',['BenchPairDisk::exposure(','TeslaBleBenchDiscovery::pair_send('], 'Only worker caller; exact disk and Send target')
    bind('BenchPairing::verify(',['TeslaBleBenchDiscovery::pair_send(','(anonymous namespace)::bench_random()::{lambda'], 'Worker Send and entropy callback; challenge captured separately')
    bind('BenchPairing::delete_local(',['BenchPairDisk::remove()'], 'Only concrete disk member')
    bind('tesla_clean::IdentityStore::save(',['BenchPairing::RecordWriter::save('], 'One synchronous writer; writer never reenters IdentityStore (depth=1 here)')
    bind('tesla_clean::DomainAuthentication::begin(',['BenchPairing::challenge(','(anonymous namespace)::bench_random()::{lambda'], 'Only verification capture callback and configured RNG; rehearsal has no authentication state machine')
    bind('BenchPairing::rehearse_local()', ['(anonymous namespace)::bench_random()::{lambda'], 'Only configured bench RNG for generic local byte filling')
    bind('tesla_clean::send_frame(',['TeslaBleBenchDiscovery::pair_write(','TeslaBleBenchDiscovery::pair_owns('], 'Selected MTU writer/ownership callback')
    bind('tesla_clean::FrameReceiver::receive(',['TeslaBleBenchDiscovery::pair_frame('], 'Receive callback only captures; crypto after unwinding')
    bind('mbedtls_pk_setup',[n for n in graph if not n.startswith('ROM_') and n.endswith('_alloc_wrap')], 'Finite compiled PK allocation vtable union, including rejected non-P256 PEM')
    bind('mbedtls_pk_free',[n for n in graph if not n.startswith('ROM_') and n.endswith('_free_wrap')], 'Finite compiled PK teardown vtable union, including rejected non-P256 PEM')
    bind('ecp_modp',[n for n in graph if not n.startswith('ROM_') and n.startswith('ecp_mod_p')], 'All compiled curve reducers, including non-P256 PEM parsed before checked_key rejection')
    bind('mbedtls_mpi_core_fill_random',['(anonymous namespace)::bench_random()::{lambda'], 'Only concrete bench RNG: checks controller enabled, esp_fill_random')
    bind('mbedtls_platform_zeroize',['memset'], 'Configured non-ALT implementation const-volatile memset pointer')
    bind('tesla_clean::IdentityLease::create(',['tesla_clean::Allocation::get('], 'Default Allocation passed by bench IdentityStore constructor')
    bind('tesla_clean::IdentityLease::reset()', ['tesla_clean::Allocation::put('], 'Same default checked identity allocation owner')
    assert 'mbedtls_platform_set_calloc_free' not in graph,'Allocator setter is linked: require explicit target re-review'
    bind('mbedtls_calloc',['esp_mbedtls_mem_calloc'],'Configured MBEDTLS_EXTERNAL_MEM_ALLOC; allocator setter is absent from final ELF')
    bind('mbedtls_free',['esp_mbedtls_mem_free'],'Configured MBEDTLS_EXTERNAL_MEM_ALLOC; allocator setter is absent from final ELF')
    for revision in ('rev0','rev300'):
        for target in ('__getreent','_open_r','_read_r','_write_r','_close_r','_malloc_r','_free_r','_realloc_r','_calloc_r'):
            bind('ROM_'+revision+'::'+target,[target],'SDK newlib_init.c s_stub_table initialized on both CPUs; aliases resolved by linked address')
    # Explicitly selected /data context callbacks. All synchronous descendants,
    # logging/error paths, flash and allocator callbacks remain in traversal.
    for caller,target in [('esp_vfs_open','vfs_littlefs_open'),('esp_vfs_write','vfs_littlefs_write'),('esp_vfs_read','vfs_littlefs_read'),('esp_vfs_fsync','vfs_littlefs_fsync'),('esp_vfs_close','vfs_littlefs_close'),('esp_vfs_fstat','vfs_littlefs_fstat'),('esp_vfs_stat','vfs_littlefs_stat'),('esp_vfs_unlink','vfs_littlefs_unlink'),('esp_vfs_rename','vfs_littlefs_rename')]:
        bind(caller,[target],'/data mount uses LittleFS context-pointer VFS registration')
    for caller,targets in [('lfs_bd_read',['littlefs_esp_part_read']),('lfs_bd_prog',['littlefs_esp_part_write']),('lfs_bd_erase',['littlefs_esp_part_erase']),('lfs_bd_sync',['littlefs_esp_part_sync']),('lfs_dir_fetchmatch',['lfs_dir_find_match','lfs_fs_parent_match']),('lfs_dir_traverse',['lfs_dir_commit_commit','lfs_dir_commit_size','lfs_dir_traverse_filter']),('lfs_ctz_traverse',['lfs_alloc_lookahead','lfs_fs_size_count']),('lfs_fs_traverse_',['lfs_alloc_lookahead','lfs_fs_size_count'])]:
        bind(caller,targets,'Pinned LittleFS /data config; traversal uses bounded explicit stack, not C recursion')
    # Context projection of the action switch, not a global-firmware closure.
    root_names=[pick(p) for p in ['vPortTaskWrapper','TeslaBleBenchDiscovery::task_entry(','TeslaBleBenchDiscovery::run()']]
    root_frames=sum(graph[n]['frame'] for n in root_names)
    root_coverage=all(graph[n].get('coverage_complete',True) for n in root_names)
    action_calls=('BenchPairing::prepare(', 'BenchPairing::delete_local(', 'BenchPairing::rehearse_local(',
                  'TeslaBleBenchDiscovery::scan(', 'TeslaBleBenchDiscovery::probe(', 'TeslaBleBenchDiscovery::pair_transport(')
    # Retain every shared root helper/error call and unresolved site. Only the
    # disjoint explicit action bodies are projected below, never unknown calls.
    root_children=sorted({c for n in root_names for c in graph[n]['calls']
                          if c not in root_names and not c.startswith(action_calls)})
    root_unknown=[n+': '+site for n in root_names for site in graph[n]['unknown']]
    measure=[pick(p) for p in ['TeslaBleBenchDiscovery::stage(','TeslaBleBenchDiscovery::result(','TeslaBleBenchDiscovery::measure(']]
    controller=[pick(p) for p in ['__wrap_esp_bt_controller_init','__wrap_esp_bt_controller_enable','esp_bt_controller_disable','esp_bt_controller_deinit']]
    host=[pick(p) for p in ['__wrap_esp_bluedroid_init','__wrap_esp_bluedroid_enable','esp_bluedroid_disable','esp_bluedroid_deinit']]
    transport=pick('TeslaBleBenchDiscovery::pair_transport()')
    def project(name,exclude):
        graph[name]=dict(graph[transport],calls=[c for c in graph[transport]['calls'] if c!=pick(exclude)])
        return name
    scopes={
        'A_fresh_and_import_persist_readback_controller_teardown':[pick('BenchPairing::prepare()')]+controller+measure,
        'B_present_encode_marker_gatt_response_teardown':[project('@present_transport','BenchPairing::verify('),pick('TeslaBleBenchDiscovery::probe()')]+controller+host+measure,
        'C_verify_capture_transmit_p256_hmac_receive_teardown':[project('@verify_transport','BenchPairing::send_pair('),pick('TeslaBleBenchDiscovery::probe()')]+controller+host+measure,
        'D_delete_partial_failure_marker_restoration':[pick('BenchPairing::delete_local(')]+measure,
        'A_resource_rehearsal_no_transport':[pick('BenchPairing::rehearse_local()')]+controller+host+measure,
    }
    # Close/unregister are common teardown branches only in B/C, not A or D.
    for key in [k for k in scopes if k.startswith(('B_','C_'))]:
        scopes[key]+=[pick(p) for p in ['esp_ble_gattc_close','esp_ble_gattc_cancel_open','esp_ble_gattc_app_unregister','TeslaBleBenchDiscovery::wait(']]
    cp_symbol=re.search(r'^([0-9a-f]+)\s+[0-9a-f]+\s+[DR]\s+offset_cpsa$',nm,re.M);assert cp_symbol
    cp_address=int(cp_symbol[1],16)
    cp=next(struct.unpack_from('<I',data,cp_address-address)[0] for address,size,data in sections(args.elf).values() if data and address<=cp_address<address+size)
    lowint=re.search(r'^([0-9a-f]+) <_xt_lowint1>:\n(.*?)(?=^[0-9a-f]+ <|\Z)',text,re.M|re.S)[2]
    adjust=int(re.search(r'\baddmi\s+a1, a1, (0x[0-9a-f]+)',lowint)[1],0);adjust-=1<<32 if adjust&(1<<31) else 0
    l1=-(adjust+int(re.search(r'\baddi\s+a1, a1, (\d+)',lowint)[1]));assert (cp,l1)==(96,192)
    constants,budget_evidence=bound_worker_constants(args.budget_header,args.elf.parent/'bench_build_inputs.unchecked.json',nm)
    worker_budgets={name:constants['REHEARSAL_WORKER_BYTES' if name=='A_resource_rehearsal_no_transport' else 'PAIR_WORKER_BYTES'] for name in scopes}
    rows={}
    for name,children in scopes.items():
        graph[name]={'frame':root_frames,'calls':sorted(set(children+root_children)),'unknown':root_unknown,'dynamic':any(graph[n]['dynamic'] for n in root_names),'coverage_complete':root_coverage}
        rows[name]=bound(graph,[name],cp+l1,worker_budgets[name])
    report={'elf_sha256':hashlib.sha256(args.elf.read_bytes()).hexdigest(),'rom_code_evidence':rom_evidence,'worker_budgets':worker_budgets,'budget_source':budget_evidence,'required_margin_bytes':1024,'runtime_worker_watermark_minimum_bytes':2048,'common_root_calls':root_children,'common_root_unknown':root_unknown,'context_reserves':{'coprocessor':cp,'L1_frame':l1},'operations':rows,'selected_finite_targets':bindings,
            'noreturn_contracts':{'symbols':sorted(NORETURN),'basis':'Pinned SDK stack_check.c -> noreturn esp_system_abort in port/esp_system_chip.c -> noreturn panic_abort in panic.c; fatal callee edges retained; trap/exception descendants remain unbounded.'},
            'rehearsal_known_call_reachability':rehearsal_reachability(graph),
            'application_decode_coverage':{'functions':sum(not n.startswith('ROM_') for n in graph),'failed_functions':{n:r.get('coverage_failures',[]) for n,r in graph.items() if not n.startswith('ROM_') and not r.get('coverage_complete',True)}},
            'all_operations_proven':all(r['complete'] and r['margin_at_least_1024'] for r in rows.values()),
            'limitations':['Candidate path sums are NOT bounds when any edge/recursion/dynamic frame is unresolved.', 'Generic non-P256 PEM rejection branches must be accounted as well as successful P256 import.', 'ROM and synchronous controller/flash descendants remain open until backed by correct revision code/callback closure.', 'Fatal abort/stack-canary paths and logging are not silently discarded.']}
    (args.out/'operation-stack.json').write_text(json.dumps(report,indent=2)+'\n')
    (args.out/'operation-callgraph.json').write_text(json.dumps(graph,indent=2)+'\n')
    print(json.dumps({k:{'complete':v['complete'],'bounded_bytes':v['bounded_bytes'],'margin_at_least_1024':v['margin_at_least_1024'],'accounted_candidate':v['accounted_candidate_bytes_including_context'],'unknown_functions':len(v['unknown']),'cycles':len(v['cycles'])} for k,v in rows.items()},indent=2))
    # Audit generation succeeds; safety approval is ONLY all_operations_proven.
if __name__=='__main__':main()
