"""WARP4 Tesla production build wiring (pre-script). No SDK, signing, partition or upstream-hook changes.
Excludes Tesla bench/diagnostic sources (same set as the qualified production guard) and rejects lab/fixture
switches or linker interception in this production target."""
from pathlib import Path
Import('env')
flags = ' '.join(env.GetProjectOption('build_flags', []))
for token in ('BENCH', 'CANONICAL', 'IRAM_DIAGNOSTICS', 'RADIO_SPIKE', '--wrap=', 'TESLA_REAL_LAB', 'TESLA_MEM_FIXTURE', 'TESLA_NIMBLE_DISPATCH_HOST_BINDING', 'TESLA_WORKER_TEST_HOOKS',
              '_LAB', 'WARP_TESLA_MEMORY_PHASES'):
    if token in flags:
        raise ValueError('Nonproduction feature in WARP4 Tesla target: ' + token)
# LSP1: the lifecycle stack probe is allowed ONLY in the isolated measurement env, never in warp4_tesla/production.
if 'TESLA_LIFECYCLE_STACK_PROBE' in flags and env['PIOENV'] != 'warp4_tesla_stack_probe':
    raise ValueError('Lifecycle stack probe outside the isolated warp4_tesla_stack_probe env')
if env['PIOENV'] == 'warp4_tesla_stack_probe' and '-DTESLA_LIFECYCLE_STACK_PROBE=1' not in flags:
    raise ValueError('warp4_tesla_stack_probe requires -DTESLA_LIFECYCLE_STACK_PROBE=1')
# HAP1: the heap/arena walk probe is allowed ONLY in the isolated measurement env.
if 'WARP_HEAP_ARENA_PROBE' in flags and env['PIOENV'] != 'warp4_tesla_stack_probe':
    raise ValueError('Heap/arena probe outside the isolated warp4_tesla_stack_probe env')
EXCLUDED = ('tesla_ble_radio_spike.cpp', 'tesla_ble_iram_diagnostics.cpp', 'bluetooth_master_bench.cpp', 'r13_psram_policy.cpp')
def source_review(env, node):
    name = Path(str(node)).name
    if name.startswith('tesla_ble_bench_') or name in EXCLUDED:
        return None
    return node
env.AddBuildMiddleware(source_review)
