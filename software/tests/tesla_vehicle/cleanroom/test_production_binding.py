# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
root=Path(__file__).resolve().parents[4]
module=root/'software/src/modules/tesla_vehicle'
for p in sorted(module.glob('*')):
    if p.suffix not in ('.h','.cpp'): continue
    text=p.read_text()
    assert 'TeslaBLE::' not in text, str(p)
    for header in ('<vehicle.h>','<adapters.h>','<checked_mutex.h>','<checked_function.h>'):
        assert header not in text, (str(p),header)
    for op in ('start_charging','charging_start','ACTION_CHARGE_START'):
        assert op not in text, (str(p),op)
# charge_start: any identifier containing it is forbidden in .cpp/.h/.inc except
# the exact AutoPoll scheduler flag/due-lambda forms (charge_start_guard.py).
import sys; sys.path.insert(0, str(Path(__file__).resolve().parent))
from charge_start_guard import violations, module_files
module_tu = module_files(module)
assert any(n.endswith('.inc') for n, _ in module_tu)
bad = violations(module_tu)
assert not bad, bad
for p, text in module_tu:
    for op in ('start_charging','charging_start','ACTION_CHARGE_START'):
        assert op not in text, (p, op)
main=(module/'tesla_vehicle.cpp').read_text()
assert 'tesla_clean::Vehicle' in main
assert 'tesla_clean::WakePolicy::NoWakeSkip' in main
assert 'result.authenticated' in main and 'result.generation' in main
assert 'ev.get_active_ev_mac()' in main and 'ev.set_soc(soc)' in main
ini=(root/'software/warp4_tesla_ble.ini').read_text()
assert 'lib_ignore = TeslaBLE' in ini
assert 'TeslaBLEClean' in ini
print('Clean production source contract passed (not interoperability evidence)')
