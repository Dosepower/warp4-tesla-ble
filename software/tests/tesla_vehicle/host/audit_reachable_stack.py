#!/usr/bin/env python3
"""Current ELF reachable ENTRY-frame evidence, deliberately NOT a static bound.
Only direct calls and cross-symbol direct jumps are inferred from disassembly.
Literal-loaded CALLX targets are resolved only within straight-line instructions
without register overwrite/control transfer; remaining CALLX stays unknown.
No implementation-specific callback edges are inferred. Unknown indirect
targets, ROM/library functions and recursive cycles are retained.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
from typing import Any
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
from bench_confinement import output_plan

root = Path(__file__).resolve().parents[4]
elf = root / 'software/.pio/build/warp4_tesla_ble/firmware.elf'
out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(os.environ['TMPDIR']) / 'tesla-phase1-twelfth-stack'
output_plan(roots=(out,)).revalidate()
out.mkdir(parents=True, exist_ok=True)
tools = Path('/home/hermes/.platformio/packages/toolchain-xtensa-esp-elf/bin')
raw = elf.read_bytes()
assert raw[:6] == b'\x7fELF\x01\x01'
h = struct.unpack_from('<16sHHIIIIIHHHHHH', raw, 0)
headers = [struct.unpack_from('<IIIIIIIIII', raw, h[6] + i * h[11]) for i in range(h[12])]
strings = headers[h[13]]
strings = raw[strings[4]:strings[4] + strings[5]]
sections = {strings[s[0]:].split(b'\0', 1)[0].decode(): s for s in headers}
code = []
for name in ('.flash.text', '.iram0.text', '.iram0.vectors'):
    if name in sections:
        s = sections[name]
        path = out / (name.strip('.') + '.bin')
        path.write_bytes(raw[s[4]:s[4] + s[5]])
        code.append((s[3], s[3] + s[5], path))
nm = subprocess.check_output([str(tools / 'xtensa-esp32-elf-nm'), '-SC', str(elf)], text=True)
(out / 'symbols.txt').write_text(nm)
functions: dict[int, dict[str, Any]] = {}
for line in nm.splitlines():
    m = re.match(r'^([0-9a-f]+)\s+([0-9a-f]+)\s+[tTwW]\s+(.+)$', line)
    if m and int(m[2], 16):
        a = int(m[1], 16)
        if any(lo <= a < hi for lo, hi, _ in code):
            functions[a] = {'name': m[3], 'size': int(m[2], 16)}
addresses = sorted(functions)
by_name = {f['name']: a for a, f in functions.items()}
roots = ['TeslaVehicle::task_entry(void*)', 'TeslaVehicle::teardown_entry(void*)']
assert all(name in by_name for name in roots)
source_edges = {}  # No assumed callback targets; inspect current ELF only.
cache: dict[int, dict[str, Any]] = {}
cfg_mode = sys.argv[2:] == ['cfg']
decode_unknown = []
decode_resynchronized = []

def cfg_lines(a, f, section, initial):
    """Decode only reachable instructions; resync at branch targets into padding.

    Symbol extents, indirect jumps and exception traps remain explicit limitations.
    No source-specific symbol/address exception is assumed.
    """
    end = a + f['size']
    decoded, following, seen = {}, {}, set()

    def load(start, text=None):
        if text is None:
            text = subprocess.check_output([str(tools / 'xtensa-esp32-elf-objdump'), '-D', '-b', 'binary', '-m', 'xtensa',
                f'--adjust-vma={section[0]}', f'--start-address={start}', f'--stop-address={end}', str(section[2])], text=True)
        records = []
        for line in text.splitlines():
            m = re.match(r'^\s*([0-9a-f]+):\s+[0-9a-f]+\s+(\S+)(?:\s+(.*))?$', line)
            if m:
                records.append((int(m[1], 16), m[2], m[3] or '', line))
        for i, (addr, op, args, line) in enumerate(records):
            if addr not in seen:
                decoded[addr] = (op, args, line)
                following[addr] = records[i + 1][0] if i + 1 < len(records) else end

    load(a, initial)
    pending = [a]
    while pending:
        addr = pending.pop()
        if addr in seen or not a <= addr < end:
            continue
        if addr not in decoded:
            decode_resynchronized.append(dict(function=f['name'], address=hex(addr)))
            load(addr)
        if addr not in decoded:
            decode_unknown.append(dict(function=f['name'], address=hex(addr), reason='missing instruction'))
            continue
        seen.add(addr)
        op, args, line = decoded[addr]
        if op.startswith(('ret', 'rfe', 'rfde', 'rfwo', 'rfwu')):
            continue
        if op in ('ill', '.byte', '.word', '???', 'jx'):
            decode_unknown.append(dict(function=f['name'], address=hex(addr), reason=op))
            continue
        branch = op == 'j' or op.startswith('b') and op not in ('break', 'break.n') or op.startswith('loop')
        if branch:
            targets = re.findall(r'0x[0-9a-f]+', args)
            if targets:
                target = int(targets[-1], 16)
                if a <= target < end:
                    pending.append(target)
            if op == 'j':
                continue
        pending.append(following[addr])
    return '\n'.join(decoded[x][2] for x in sorted(seen)) + '\n'

def inspect(a: int) -> dict[str, Any]:
    if a in cache:
        return cache[a]
    f = functions[a]
    section = next(s for s in code if s[0] <= a < s[1])
    d = subprocess.check_output([str(tools / 'xtensa-esp32-elf-objdump'), '-D', '-b', 'binary', '-m', 'xtensa',
        f'--adjust-vma={section[0]}', f'--start-address={a}', f'--stop-address={a + f["size"]}', str(section[2])], text=True)
    if cfg_mode:
        d = cfg_lines(a, f, section, d)
    instructions = [line for line in d.splitlines() if re.match(r'^\s*[0-9a-f]+:\s', line)]
    entry = re.search(r'\bentry\s+a1,\s*(0x[0-9a-f]+|\d+)', '\n'.join(instructions[:12]))
    calls = set()
    unknown_direct = []
    for m in re.finditer(r'\b(call(?:0|4|8|12)|j)\s+(0x[0-9a-f]+)', d):
        target = int(m[2], 16)
        if target in functions and target != a:
            calls.add(target)
        elif m[1] != 'j':
            if target == a:
                calls.add(target)
            else:
                unknown_direct.append(hex(target))
    explicit = []
    literals: dict[str, int] = {}
    resolved_literal_calls = []
    unresolved_indirect = 0
    for line in instructions:
        load = re.search(r'\bl32r\s+(a\d+),.*\((0x[0-9a-f]+)\)', line)
        if load:
            literals[load[1]] = int(load[2], 16)
            continue
        call = re.search(r'\bcallx(?:0|4|8|12)\s+(a\d+)', line)
        if call:
            target = literals.get(call[1])
            if target in functions:
                calls.add(target)
                resolved_literal_calls.append(hex(target))
            else:
                unresolved_indirect += 1
            literals.clear()
            continue
        instruction = re.search(r':\s+[0-9a-f]+\s+(\S+)\s+(a\d+)', line)
        if instruction:
            op, reg = instruction.groups()
            if not op.startswith(('s8i', 's16i', 's32i', 'b', 'call', 'j')):
                literals.pop(reg, None)
        if re.search(r'\b(?:call(?:0|4|8|12)|j|b\w+)\s', line):
            literals.clear()
    for target in source_edges.get(f['name'], []):
        assert target in by_name, target
        calls.add(by_name[target])
        explicit.append(target)
    rec = dict(f, address=hex(a), frame=int(entry[1], 0) if entry else None,
               calls=sorted(calls), unresolved_indirect=unresolved_indirect, resolved_literal_calls=resolved_literal_calls,
               unknown_direct=unknown_direct, source_verified_edges=explicit,
               other_sp_adjustments=[line for line in instructions if re.search(r'\b(?:addi|addmi|sub|mov)\S*\s+a1,', line)])
    cache[a] = rec
    (out / f'{a:08x}.dis').write_text(d)
    return rec

reachable = set()
pending = [by_name[n] for n in roots]
while pending:
    a = pending.pop()
    if a in reachable:
        continue
    reachable.add(a)
    pending.extend(inspect(a)['calls'])

# A longest discovered acyclic direct-call path, not a maximum executable stack.
# Cycle edges are omitted and flagged; missing ENTRY contributes zero, NOT safety.
cycles = set()
mem: dict[int, tuple[int, list[int]]] = {}
def chain_path(a: int, active: set[int]) -> tuple[int, list[int]]:
    if a in active:
        cycles.add(a)
        return 0, []
    if a in mem:
        return mem[a]
    r = inspect(a)
    options = [chain_path(b, active | {a}) for b in r['calls']]
    best = max(options, default=(0, []), key=lambda x: x[0])
    result = ((r['frame'] or 0) + best[0], [a] + best[1])
    mem[a] = result
    return result

summary: dict[str, Any] = {'elf_sha256': hashlib.sha256(raw).hexdigest(),
           'map_sha256': hashlib.sha256(elf.with_suffix('.map').read_bytes()).hexdigest(),
           'method': 'ENTRY frame sums on discovered direct-call/jump graph and straight-line literal calls; no assumed callback edges, recursion edges omitted; NOT a static bound',
           'roots': {}, 'complete_static_bound': False,
           'stack_usage_files': [str(p.relative_to(root)) for p in (root / 'software/.pio').rglob('*.su')],
           'required': 'Hardware worker/teardown high-water and guards under auth/RX/error/key/storage/stop pressure; include RTOS/interrupt reserve. No hardware accessed.'}
for name, budget in zip(roots, (12288, 4096)):
    score, chain = chain_path(by_name[name], set())
    summary['roots'][name] = {'configured_bytes': budget, 'discovered_entry_sum_bytes': score,
        'chain': [{'name': inspect(a)['name'], 'frame': inspect(a)['frame'], 'address': hex(a)} for a in chain]}
summary['reachable_functions'] = len(reachable)
summary['resolved_literal_call_sites'] = sum(len(inspect(a)['resolved_literal_calls']) for a in reachable)
summary['recursive_cycle_nodes'] = [inspect(a)['name'] for a in sorted(cycles)]
summary['unresolved_indirect_sites'] = sum(inspect(a)['unresolved_indirect'] for a in reachable)
summary['unknown_direct_targets'] = sorted({b for a in reachable for b in inspect(a)['unknown_direct']})
summary['functions_without_entry'] = [inspect(a)['name'] for a in sorted(reachable) if inspect(a)['frame'] is None]
summary['non_entry_sp_adjustment_functions'] = [inspect(a)['name'] for a in sorted(reachable) if inspect(a)['other_sp_adjustments']]
summary['cfg_reachable_instruction_mode'] = cfg_mode
summary['resynchronized_instruction_targets'] = decode_resynchronized
summary['unknown_decode_or_indirect_jump_sites'] = decode_unknown
(out / 'callgraph.json').write_text(json.dumps({hex(a): inspect(a) for a in sorted(reachable)}, indent=2) + '\n')
(out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print('Current ELF:', summary['elf_sha256'])
print('Reachable functions:', len(reachable), 'indirect sites:', summary['unresolved_indirect_sites'], 'cycle nodes:', len(cycles))
for name, result in summary['roots'].items():
    print(name, 'discovered ENTRY sum', result['discovered_entry_sum_bytes'], 'configured', result['configured_bytes'])
    print(' -> '.join(f'{item["name"]} [{item["frame"]}]' for item in result['chain']))
print('Stack-usage files:', len(summary['stack_usage_files']))
print('NOT a complete static bound. Unknown callbacks/library recursion/ROM/non-ENTRY adjustments/RTOS margin remain. Hardware high-water required.')
print('Artifacts:', out)
